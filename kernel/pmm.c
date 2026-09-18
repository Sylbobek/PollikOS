#include "pmm.h"
#include "vmm.h"
#include "klog.h"

extern u8 __bss_end[];

static u32 pmm_bitmap[PMM_BITMAP_DWORDS];
static u32 pmm_total_pages = 0;
static u32 pmm_usable_bytes = 0;
static u32 pmm_unmanaged_bytes = 0;
static u32 pmm_reserved_bytes = 0;
static u32 pmm_free_page_count = 0;
static u32 pmm_used_page_count = 0;
static u32 pmm_highest_addr = 0;

static void pmm_set_bit(u32 page_idx) {
    if (page_idx < PMM_MAX_PAGES) {
        pmm_bitmap[page_idx / 32u] |= (1u << (page_idx % 32u));
    }
}

static void pmm_clear_bit(u32 page_idx) {
    if (page_idx < PMM_MAX_PAGES) {
        pmm_bitmap[page_idx / 32u] &= ~(1u << (page_idx % 32u));
    }
}

static int pmm_test_bit(u32 page_idx) {
    if (page_idx >= PMM_MAX_PAGES)
        return 1;
    return (pmm_bitmap[page_idx / 32u] & (1u << (page_idx % 32u))) != 0;
}

void pmm_reserve_range(uintptr_t start, uintptr_t end) {
    u32 start_page = (u32)(start / PMM_PAGE_SIZE);
    u32 end_page = (u32)((end + PMM_PAGE_SIZE - 1u) / PMM_PAGE_SIZE);
    if (end_page > PMM_MAX_PAGES)
        end_page = PMM_MAX_PAGES;

    for (u32 i = start_page; i < end_page; i++) {
        if (!pmm_test_bit(i)) {
            pmm_set_bit(i);
            if (pmm_free_page_count > 0)
                pmm_free_page_count--;
            pmm_used_page_count++;
        }
    }
}

void pmm_free_range(uintptr_t start, uintptr_t end) {
    u32 start_page = (u32)((start + PMM_PAGE_SIZE - 1u) / PMM_PAGE_SIZE);
    u32 end_page = (u32)(end / PMM_PAGE_SIZE);
    if (end_page > PMM_MAX_PAGES)
        end_page = PMM_MAX_PAGES;

    for (u32 i = start_page; i < end_page; i++) {
        if (pmm_test_bit(i)) {
            pmm_clear_bit(i);
            pmm_free_page_count++;
            if (pmm_used_page_count > 0)
                pmm_used_page_count--;
        }
    }
}

void pmm_init(void) {
    /* 1. Mark entire 4 GiB space as reserved initially */
    memset(pmm_bitmap, 0xFF, sizeof(pmm_bitmap));
    pmm_total_pages = 0;
    pmm_usable_bytes = 0;
    pmm_reserved_bytes = 0;
    pmm_free_page_count = 0;
    pmm_used_page_count = PMM_MAX_PAGES;
    pmm_highest_addr = 0;

    u32 entry_count = *(const u32 *)0x6000;
    const E820Entry *entries = (const E820Entry *)0x6004;

    KLOG_INFO(KLOG_CAT_PMM, "Parsing BIOS E820 memory map");

    if (entry_count > 0 && entry_count <= 64) {
        klog_dec(KLOG_CAT_PMM, "Valid E820 entries: ", entry_count);

        for (u32 i = 0; i < entry_count; i++) {
            u64 base = entries[i].base;
            u64 len = entries[i].length;
            u32 type = entries[i].type;
            u32 ext = entries[i].acpi_ext;

            /* Validate ACPI 3.0 extended attribute: if bit 0 is cleared, entry must be ignored */
            if ((ext & 1) == 0) {
                KLOG_WARN(KLOG_CAT_PMM, "Ignoring entry with ACPI 3.0 bit 0 cleared");
                continue;
            }

            /* Validate length */
            if (len == 0)
                continue;

            /* Guard against 64-bit integer overflow */
            if (base + len < base)
                continue;

            /* Ignore regions completely above 32-bit address space */
            if (base >= 0x100000000ULL)
                continue;

            /* Clamp regions spanning above 4 GiB to 4 GiB limit */
            if (base + len > 0x100000000ULL)
                len = 0x100000000ULL - base;

            u32 top = (u32)(base + len);
            if (top > pmm_highest_addr)
                pmm_highest_addr = top;

            if (type == 1) { /* Usable RAM */
                /* Frames at or above KERNEL_DIRECT_MAP_TOP have no kernel
                 * mapping in this 32-bit layout (the range belongs to user
                 * space), so they stay reserved and are reported separately. */
                u64 managed_end = base + len;
                if (managed_end > KERNEL_DIRECT_MAP_TOP)
                    managed_end = KERNEL_DIRECT_MAP_TOP;
                if (base < managed_end) {
                    pmm_free_range((uintptr_t)base, (uintptr_t)managed_end);
                    pmm_usable_bytes += (u32)(managed_end - base);
                }
                if (base + len > managed_end)
                    pmm_unmanaged_bytes += (u32)(base + len - managed_end);
            }
        }
    } else {
        /* Fallback if BIOS E820 was unavailable: assume 64 MiB RAM */
        KLOG_WARN(KLOG_CAT_PMM, "E820 not available, using 64 MiB fallback");
        pmm_free_range(0x100000, 0x4000000);
        pmm_highest_addr = 0x4000000;
        pmm_usable_bytes = 0x3F00000;
    }

    pmm_total_pages = pmm_highest_addr / PMM_PAGE_SIZE;
    if (pmm_highest_addr > pmm_usable_bytes + pmm_unmanaged_bytes)
        pmm_reserved_bytes = pmm_highest_addr - pmm_usable_bytes - pmm_unmanaged_bytes;

    /* 2. Mark ALL used system structures and critical areas as RESERVED:
     * - First 1 MiB: IVT, BDA, Stage 1 MBR, Stage 2, E820 buffer (0x6000),
     *   VBE structures (0x7000), stage-2 chunk buffer (0x10000), boot stack (0x9FC00), VGA, BIOS ROM */
    pmm_reserve_range(0x0, 0x100000);

    /* - Kernel image and BSS (0x100000 .. __bss_end): .text/.rodata/.data loaded by
     *   stage 2, followed by BSS (PMM bitmap, boot page tables, DMA rings, worker stacks) */
    pmm_reserve_range(0x100000, (uintptr_t)__bss_end);

    /* - Kernel Heap and Guard Zone (__bss_end .. 0x800000):
     * Includes heap (up to 0x7F0000) and unmapped guard page (0x7FF000) */
    pmm_reserve_range((uintptr_t)__bss_end, 0x800000);

    /* - Ring 3 worker processes (0x800000 .. 0x820000) */
    pmm_reserve_range(0x800000, 0x820000);

    /* Software scene/wallpaper buffers are allocated by kernel_main() after
     * framebuffer_init() selects the actual mode. Window surfaces are allocated
     * by wm_init(). PMM owns all these allocations; no fixed GUI arena remains. */

    /* - Hardware VBE Linear Framebuffer MMIO */
    const u8 *vbe_info = (const u8 *)0x7000;
    u32 lfb_addr = *(const u32 *)(vbe_info + 40);
    if (lfb_addr >= 0x10000000) {
        pmm_reserve_range(lfb_addr, lfb_addr + 32u * 1024u * 1024u);
    }

    /* Diagnostics */
    klog_dec(KLOG_CAT_PMM, "Total physical RAM (MiB): ", pmm_highest_addr / (1024u * 1024u));
    klog_dec(KLOG_CAT_PMM, "Usable RAM (MiB):         ", pmm_usable_bytes / (1024u * 1024u));
    klog_dec(KLOG_CAT_PMM, "Reserved RAM (MiB):       ", pmm_reserved_bytes / (1024u * 1024u));
    if (pmm_unmanaged_bytes)
        klog_dec(KLOG_CAT_PMM, "Unmanaged RAM above 1 GiB direct map (MiB): ", pmm_unmanaged_bytes / (1024u * 1024u));
    klog_dec(KLOG_CAT_PMM, "Free pages count:         ", pmm_free_page_count);
    klog_dec(KLOG_CAT_PMM, "Used pages count:         ", pmm_used_page_count);
    klog_dec(KLOG_CAT_PMM, "Free RAM (MiB):           ", (pmm_free_page_count * PMM_PAGE_SIZE) / (1024u * 1024u));
}

uintptr_t pmm_alloc_page(void) {
    for (u32 i = 0; i < PMM_BITMAP_DWORDS; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFFu) {
            u32 dword = pmm_bitmap[i];
            for (u32 bit = 0; bit < 32u; bit++) {
                if (!(dword & (1u << bit))) {
                    pmm_bitmap[i] |= (1u << bit);
                    if (pmm_free_page_count > 0)
                        pmm_free_page_count--;
                    pmm_used_page_count++;
                    return (uintptr_t)((i * 32u + bit) * PMM_PAGE_SIZE);
                }
            }
        }
    }
    KLOG_ERROR(KLOG_CAT_PMM, "Out of physical memory!");
    return 0;
}

void pmm_free_page(uintptr_t phys_addr) {
    if (!phys_addr || (phys_addr & (PMM_PAGE_SIZE - 1u)))
        return;

    u32 page_idx = (u32)(phys_addr / PMM_PAGE_SIZE);
    if (page_idx >= PMM_MAX_PAGES)
        return;

    u32 dword_idx = page_idx / 32u;
    u32 bit_idx = page_idx % 32u;

    if (pmm_bitmap[dword_idx] & (1u << bit_idx)) {
        pmm_bitmap[dword_idx] &= ~(1u << bit_idx);
        pmm_free_page_count++;
        if (pmm_used_page_count > 0)
            pmm_used_page_count--;
    }
}

uintptr_t pmm_alloc_pages(u32 count) {
    if (count == 0)
        return 0;
    if (count == 1)
        return pmm_alloc_page();

    u32 contiguous = 0;
    u32 start_page = 0;

    for (u32 i = 0; i < PMM_MAX_PAGES; i++) {
        if (!pmm_test_bit(i)) {
            if (contiguous == 0)
                start_page = i;
            contiguous++;
            if (contiguous == count) {
                for (u32 j = start_page; j < start_page + count; j++) {
                    pmm_set_bit(j);
                }
                if (pmm_free_page_count >= count)
                    pmm_free_page_count -= count;
                else
                    pmm_free_page_count = 0;
                pmm_used_page_count += count;
                return (uintptr_t)(start_page * PMM_PAGE_SIZE);
            }
        } else {
            contiguous = 0;
        }
    }

    KLOG_ERROR(KLOG_CAT_PMM, "Out of contiguous physical memory!");
    return 0;
}

void pmm_free_pages(uintptr_t phys_addr, u32 count) {
    if (!phys_addr || count == 0)
        return;
    for (u32 i = 0; i < count; i++) {
        pmm_free_page(phys_addr + i * PMM_PAGE_SIZE);
    }
}

u32 pmm_get_total_memory(void) {
    return pmm_highest_addr;
}

u32 pmm_get_usable_memory(void) {
    return pmm_usable_bytes;
}

u32 pmm_get_reserved_memory(void) {
    return pmm_reserved_bytes;
}

u32 pmm_get_free_memory(void) {
    return pmm_free_page_count * PMM_PAGE_SIZE;
}

u32 pmm_get_free_pages_count(void) {
    return pmm_free_page_count;
}

u32 pmm_get_used_pages_count(void) {
    return pmm_used_page_count;
}

u32 pmm_get_total_pages_count(void) {
    return pmm_total_pages;
}

u32 pmm_get_highest_ram_addr(void) {
    return pmm_highest_addr;
}

int pmm_self_test(void) {
    KLOG_INFO(KLOG_CAT_PMM, "Starting 128-page stress allocation test");
    u32 initial_free = pmm_free_page_count;

    #define TEST_PAGES_COUNT 128u
    uintptr_t pages[TEST_PAGES_COUNT];

    /* 1. Allocate 128 pages */
    for (u32 i = 0; i < TEST_PAGES_COUNT; i++) {
        pages[i] = pmm_alloc_page();
        if (!pages[i] || (pages[i] & (PMM_PAGE_SIZE - 1u))) {
            KLOG_ERROR(KLOG_CAT_PMM, "Self-test FAILED: alloc_page failed or misaligned");
            for (u32 j = 0; j < i; j++) pmm_free_page(pages[j]);
            return 0;
        }
    }

    /* 2. Ensure all allocated addresses are distinct (no duplicates) */
    for (u32 i = 0; i < TEST_PAGES_COUNT; i++) {
        for (u32 j = i + 1; j < TEST_PAGES_COUNT; j++) {
            if (pages[i] == pages[j]) {
                KLOG_ERROR(KLOG_CAT_PMM, "Self-test FAILED: duplicate page returned!");
                for (u32 k = 0; k < TEST_PAGES_COUNT; k++) pmm_free_page(pages[k]);
                return 0;
            }
        }
    }

    /* 3. Write distinct pattern to each page */
    for (u32 i = 0; i < TEST_PAGES_COUNT; i++) {
        volatile u32 *ptr = (volatile u32 *)pages[i];
        *ptr = (u32)(pages[i] ^ 0xAA55AA55u);
    }

    /* 4. Verify pattern on all pages */
    for (u32 i = 0; i < TEST_PAGES_COUNT; i++) {
        volatile u32 *ptr = (volatile u32 *)pages[i];
        if (*ptr != (u32)(pages[i] ^ 0xAA55AA55u)) {
            KLOG_ERROR(KLOG_CAT_PMM, "Self-test FAILED: memory pattern mismatch!");
            for (u32 k = 0; k < TEST_PAGES_COUNT; k++) pmm_free_page(pages[k]);
            return 0;
        }
    }

    /* 5. Free all 128 pages */
    for (u32 i = 0; i < TEST_PAGES_COUNT; i++) {
        pmm_free_page(pages[i]);
    }

    /* 6. Verify free page count returned to initial */
    if (pmm_free_page_count != initial_free) {
        KLOG_ERROR(KLOG_CAT_PMM, "Self-test FAILED: free page count did not restore!");
        return 0;
    }

    KLOG_INFO(KLOG_CAT_PMM, "Self-test PASSED (128 unique pages, patterns, and full cleanup verified)");
    return 1;
}
