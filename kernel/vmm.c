#include "vmm.h"
#include "pmm.h"
#include "klog.h"
#include <stdint.h>

typedef struct {
    u32 edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    u32 gs, fs, es, ds, vector, error, eip, cs, eflags, useresp, ss;
} Frame;

static page_directory_t kernel_pdir[1024] __attribute__((aligned(4096)));
static page_table_t boot_pts[16][1024] __attribute__((aligned(4096)));
static page_directory_t *active_pdir = kernel_pdir;

#define MAX_GUARD_PAGES 16
static uintptr_t guard_pages[MAX_GUARD_PAGES];
static u32 guard_pages_count = 0;

static void hex_to_str(char *buf, u32 val) {
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        u8 nibble = (val >> (i * 4)) & 0xFu;
        buf[2 + (7 - i)] = nibble < 10 ? ('0' + nibble) : ('a' + (nibble - 10));
    }
    buf[10] = 0;
}

page_directory_t *vmm_get_kernel_directory(void) {
    return kernel_pdir;
}

void vmm_switch_address_space(page_directory_t *pd) {
    if (!pd)
        pd = kernel_pdir;
    if (active_pdir == pd)
        return; /* Skip unnecessary CR3 reload / TLB flush */
    active_pdir = pd;
    hal_write_cr3((unsigned long)(uintptr_t)pd);
}

#define USER_PDE_FIRST (USER_SPACE_START >> 22)
#define USER_PDE_END   (USER_SPACE_END >> 22)

/* A page directory entry of a process directory that points at the same page
 * table as the kernel directory is shared kernel memory. Writing user PTEs
 * into it would alter the kernel's own mappings and leak them into every
 * other process, so such tables are never modified through a process PD. */
static int pde_shared_with_kernel(page_directory_t *pd, u32 pde_idx) {
    if (pd == kernel_pdir)
        return 0;
    if (!(pd[pde_idx] & PAGE_PRESENT) || !(kernel_pdir[pde_idx] & PAGE_PRESENT))
        return 0;
    return (pd[pde_idx] & ~0xFFFu) == (kernel_pdir[pde_idx] & ~0xFFFu);
}

page_directory_t *vmm_create_address_space(void) {
    uintptr_t phys = pmm_alloc_page();
    if (!phys)
        return 0;

    page_directory_t *pd = (page_directory_t *)phys;
    memset(pd, 0, PAGE_SIZE);

    /* Share kernel mappings outside the user range (supervisor-only: PAGE_USER
     * is stripped from the PDE). User-range PDEs start empty so that every
     * process gets private page tables there. */
    for (u32 i = 0; i < 1024; i++) {
        if (i >= USER_PDE_FIRST && i < USER_PDE_END)
            continue;
        if (kernel_pdir[i] & PAGE_PRESENT) {
            pd[i] = kernel_pdir[i] & ~PAGE_USER;
        }
    }

    return pd;
}

void vmm_destroy_address_space(page_directory_t *pd) {
    if (!pd || pd == kernel_pdir)
        return;

    if (pd == active_pdir) {
        /* Never free the tables the CPU is currently walking. */
        vmm_switch_address_space(kernel_pdir);
    }

    /* Free page tables uniquely created for this process (skip kernel shared PDEs) */
    for (u32 i = 0; i < 1024; i++) {
        uintptr_t pt_phys = pd[i] & ~0xFFFu;
        if ((pd[i] & PAGE_PRESENT) && pt_phys && !pde_shared_with_kernel(pd, i)) {
            page_table_t *pt = (page_table_t *)pt_phys;
            for (u32 j = 0; j < 1024; j++) {
                if (pt[j] & PAGE_PRESENT) {
                    pmm_free_page(pt[j] & ~0xFFFu);
                }
            }
            pmm_free_page(pt_phys);
        }
    }

    pmm_free_page((uintptr_t)pd);
}

int map_page(page_directory_t *pd, uintptr_t virt_addr, uintptr_t phys_addr, u32 flags) {
    if (!pd)
        pd = active_pdir;
    u32 pde_idx = (virt_addr >> 22) & 0x3FFu;
    u32 pte_idx = (virt_addr >> 12) & 0x3FFu;

    if (pde_shared_with_kernel(pd, pde_idx)) {
        klog_hex(KLOG_CAT_VMM, "map_page refused: shared kernel page table for virt ", virt_addr);
        return 0;
    }
    if (pd != kernel_pdir && (flags & PAGE_USER) &&
        (virt_addr < USER_SPACE_START || virt_addr >= USER_SPACE_END)) {
        klog_hex(KLOG_CAT_VMM, "map_page refused: user page outside user range at ", virt_addr);
        return 0;
    }

    if (!(pd[pde_idx] & PAGE_PRESENT)) {
        uintptr_t pt_phys = pmm_alloc_page();
        if (!pt_phys)
            return 0;
        memset((void *)pt_phys, 0, PAGE_SIZE);
        u32 pde_flags = PAGE_PRESENT | PAGE_RW | (flags & PAGE_USER);
        pd[pde_idx] = pt_phys | pde_flags;
    } else if (flags & PAGE_USER) {
        pd[pde_idx] |= PAGE_USER;
    }

    page_table_t *pt = (page_table_t *)(pd[pde_idx] & ~0xFFFu);
    pt[pte_idx] = (phys_addr & ~0xFFFu) | (flags & 0xFFFu) | PAGE_PRESENT;
    hal_invalidate_page((const void *)virt_addr);
    return 1;
}

int unmap_page(page_directory_t *pd, uintptr_t virt_addr) {
    if (!pd)
        pd = active_pdir;
    u32 pde_idx = (virt_addr >> 22) & 0x3FFu;
    u32 pte_idx = (virt_addr >> 12) & 0x3FFu;

    if (!(pd[pde_idx] & PAGE_PRESENT))
        return 0;
    if (pde_shared_with_kernel(pd, pde_idx)) {
        klog_hex(KLOG_CAT_VMM, "unmap_page refused: shared kernel page table for virt ", virt_addr);
        return 0;
    }

    page_table_t *pt = (page_table_t *)(pd[pde_idx] & ~0xFFFu);
    pt[pte_idx] = 0;
    hal_invalidate_page((const void *)virt_addr);
    return 1;
}

int get_mapping(page_directory_t *pd, uintptr_t virt_addr, uintptr_t *out_phys_addr) {
    return get_mapping_flags(pd, virt_addr, out_phys_addr, 0);
}

int get_mapping_flags(page_directory_t *pd, uintptr_t virt_addr, uintptr_t *out_phys, u32 *out_flags) {
    if (!pd)
        pd = active_pdir;
    u32 pde_idx = (virt_addr >> 22) & 0x3FFu;
    u32 pte_idx = (virt_addr >> 12) & 0x3FFu;

    if (!(pd[pde_idx] & PAGE_PRESENT))
        return 0;

    page_table_t *pt = (page_table_t *)(pd[pde_idx] & ~0xFFFu);
    if (!(pt[pte_idx] & PAGE_PRESENT))
        return 0;

    if (out_phys)
        *out_phys = (pt[pte_idx] & ~0xFFFu) | (virt_addr & 0xFFFu);
    if (out_flags)
        *out_flags = pt[pte_idx] & 0xFFFu;
    return 1;
}

uintptr_t allocate_page(page_directory_t *pd, uintptr_t virt_addr, u32 flags) {
    uintptr_t phys = pmm_alloc_page();
    if (!phys)
        return 0;
    memset((void *)phys, 0, PAGE_SIZE);
    if (!map_page(pd, virt_addr, phys, flags)) {
        pmm_free_page(phys);
        return 0;
    }
    return phys;
}

void free_page(page_directory_t *pd, uintptr_t virt_addr) {
    uintptr_t phys = 0;
    if (get_mapping(pd, virt_addr, &phys)) {
        unmap_page(pd, virt_addr);
        pmm_free_page(phys);
    }
}

void vmm_set_guard_page(page_directory_t *pd, uintptr_t virt_addr) {
    unmap_page(pd, virt_addr);
    uintptr_t aligned = virt_addr & ~0xFFFu;
    /* The registry only classifies faults for diagnostics; every process uses
     * the same user guard address, so record each distinct address once. */
    for (u32 i = 0; i < guard_pages_count; i++) {
        if (guard_pages[i] == aligned)
            return;
    }
    if (guard_pages_count < MAX_GUARD_PAGES) {
        guard_pages[guard_pages_count++] = aligned;
    }
}

int vmm_is_guard_page(uintptr_t virt_addr) {
    uintptr_t aligned = virt_addr & ~0xFFFu;
    for (u32 i = 0; i < guard_pages_count; i++) {
        if (guard_pages[i] == aligned)
            return 1;
    }
    return 0;
}

int user_range_valid(page_directory_t *pd, const void *user_ptr, u32 size, int write) {
    if (!user_ptr || size == 0)
        return 0;

    uintptr_t start = (uintptr_t)user_ptr;
    uintptr_t end = start + size;

    /* Guard against integer overflow */
    if (end < start)
        return 0;

    /* Ensure entire range lies inside the user range. Kernel-range pages
     * carry no PAGE_USER PTE bit, except the legacy worker area whose PTEs
     * are user-accessible only through the kernel directory's PDE; the range
     * check keeps a flat process from reaching it via the kernel. */
    if (start < USER_SPACE_START || end > USER_SPACE_END)
        return 0;

    if (!pd)
        pd = active_pdir;

    uintptr_t start_page = start & ~0xFFFu;
    uintptr_t end_page = (end - 1u) & ~0xFFFu;

    for (uintptr_t page = start_page; page <= end_page; page += PAGE_SIZE) {
        uintptr_t phys = 0;
        u32 flags = 0;
        if (!get_mapping_flags(pd, page, &phys, &flags))
            return 0;
        if (!(flags & PAGE_PRESENT))
            return 0;
        if (!(flags & PAGE_USER))
            return 0;
        if (write && !(flags & PAGE_RW))
            return 0;
    }

    return 1;
}

int copy_from_user(void *kernel_dest, const void *user_src, u32 size) {
    if (!user_range_valid(active_pdir, user_src, size, 0))
        return 0;
    memcpy(kernel_dest, user_src, size);
    return 1;
}

int copy_to_user(void *user_dest, const void *kernel_src, u32 size) {
    if (!user_range_valid(active_pdir, user_dest, size, 1))
        return 0;
    memcpy(user_dest, kernel_src, size);
    return 1;
}

int copy_string_from_user(char *kernel_dest, const char *user_src, u32 max_len) {
    if (!kernel_dest || !user_src || max_len == 0)
        return 0;

    u32 i = 0;
    while (i < max_len - 1) {
        char c;
        if (!copy_from_user(&c, user_src + i, 1))
            return 0;
        kernel_dest[i] = c;
        if (c == '\0')
            return 1;
        i++;
    }
    kernel_dest[i] = '\0';
    return 1;
}

void vmm_init(void) {
    KLOG_INFO(KLOG_CAT_VMM, "Initializing 32-bit x86 paging and virtual memory");
    memset(kernel_pdir, 0, sizeof(kernel_pdir));
    guard_pages_count = 0;

    /* 1. Identity map lower 64 MiB (kernel code, data, BSS, heap, pixels, wallpaper) */
    for (u32 i = 0; i < 16; i++) {
        u32 pde_flags = PAGE_PRESENT | PAGE_RW;
        if (i == 2) {
            /* 8 MiB - 12 MiB contains legacy ring-3 workers (0x800000 - 0x820000) */
            pde_flags |= PAGE_USER;
        }
        kernel_pdir[i] = (uintptr_t)&boot_pts[i][0] | pde_flags;

        for (u32 j = 0; j < 1024; j++) {
            uintptr_t phys = (i * 1024u + j) * PAGE_SIZE;
            u32 pte_flags = PAGE_PRESENT | PAGE_RW;
            if (phys >= 0x800000 && phys < 0x820000) {
                pte_flags |= PAGE_USER;
            }
            boot_pts[i][j] = phys | pte_flags;
        }
    }

    /* Unmap NULL page (0x00000000) to ensure NULL pointer dereference causes Page Fault */
    boot_pts[0][0] = 0;

    /* Set unmapped guard page below worker memory (0x7FF000) */
    vmm_set_guard_page(kernel_pdir, 0x7FF000);

    /* 2. Map VBE Linear Framebuffer */
    const u8 *vbe_info = (const u8 *)0x7000;
    u32 lfb_phys = *(const u32 *)(vbe_info + 40);
    if (lfb_phys >= 0x10000000) {
        klog_hex(KLOG_CAT_VMM, "Mapping VBE LFB MMIO at ", lfb_phys);
        if (lfb_phys < USER_SPACE_END && lfb_phys + 32u * 1024u * 1024u > USER_SPACE_START) {
            /* Process directories do not share user-range PDEs, so the LFB is
             * reachable only while the kernel directory is active (PID 0). */
            KLOG_WARN(KLOG_CAT_VMM, "LFB lies inside the user virtual range; not visible from process address spaces");
        }
        for (u32 off = 0; off < 32u * 1024u * 1024u; off += PAGE_SIZE) {
            map_page(kernel_pdir, lfb_phys + off, lfb_phys + off, PAGE_PRESENT | PAGE_RW);
        }
    }

    /* 3. Identity map managed RAM above 64 MiB. The direct map stops at
     * KERNEL_DIRECT_MAP_TOP so it never overlaps the user range; the PMM does
     * not hand out frames above that limit either. */
    u32 ram_top = pmm_get_highest_ram_addr();
    if (ram_top > KERNEL_DIRECT_MAP_TOP)
        ram_top = KERNEL_DIRECT_MAP_TOP;
    if (ram_top > 64u * 1024u * 1024u) {
        klog_hex(KLOG_CAT_VMM, "Mapping RAM identity up to ", ram_top);
        for (uintptr_t addr = 64u * 1024u * 1024u; addr < ram_top; addr += PAGE_SIZE) {
            map_page(kernel_pdir, addr, addr, PAGE_PRESENT | PAGE_RW);
        }
    }

    /* 4. Load CR3 with physical address of Page Directory */
    hal_write_cr3((unsigned long)(uintptr_t)kernel_pdir);

    /* 5. Enable Paging (CR0.PG, bit 31) and Write Protect in Ring 0 (CR0.WP, bit 16) */
    u32 cr0;
    cr0 = (u32)hal_read_cr0();
    cr0 |= 0x80010000u; /* Bit 31: PG, Bit 16: WP */
    hal_write_cr0(cr0);

    active_pdir = kernel_pdir;
    klog_hex(KLOG_CAT_VMM, "Paging active (CR0.PG=1, CR0.WP=1, CR3=", (uintptr_t)kernel_pdir);
}

void vmm_page_fault_handler(void *frame_ptr) {
    Frame *f = (Frame *)frame_ptr;
    u32 cr2;
    cr2 = (u32)hal_read_cr2();

    char h_cr2[12], h_eip[12], h_err[12];
    hex_to_str(h_cr2, cr2);
    hex_to_str(h_eip, f->eip);
    hex_to_str(h_err, f->error);

    int is_user = (f->cs & 3) == 3 || (f->error & 4);
    int is_write = (f->error & 2) != 0;
    int is_present = (f->error & 1) != 0;
    int is_guard = vmm_is_guard_page(cr2);

    extern int process_get_current_pid(void);
    extern const char *process_get_current_name(void);
    int pid = process_get_current_pid();

    serial("\n[PF] pid=");
    char d[12];
    number(d, pid);
    serial(d);
    serial(" process=");
    serial(process_get_current_name());
    serial("\n");
    serial("[PF] pid="); serial(d); serial(" addr="); serial(h_cr2);
    serial(is_user ? " USER " : " SUPERVISOR ");
    serial(is_write ? "WRITE " : "READ ");
    serial(is_present ? "PROTECTION_VIOLATION" : "NOT_PRESENT");
    if (is_guard) serial(" [STACK_OVERFLOW_GUARD_PAGE]");
    serial("\n");
    serial("address="); serial(h_cr2); serial("\n");
    serial("eip="); serial(h_eip); serial("\n");
    serial("reason=");
    serial(is_user ? "USER " : "SUPERVISOR ");
    serial(is_write ? "WRITE " : "READ ");
    serial(is_present ? "PROTECTION_VIOLATION" : "NOT_PRESENT");
    if (f->error & 8) serial(" RESERVED_BIT");
    if (f->error & 16) serial(" INSTRUCTION_FETCH");
    if (is_guard) serial(" [STACK_OVERFLOW_GUARD_PAGE]");
    serial("\n");

    if (is_user) {
        serial("[PROC] PID ");
        serial(d);
        serial(" terminated because of Page Fault\n");
    } else {
        panic("Unhandled Page Fault in Kernel Mode", f);
    }
}

int vmm_self_test(void) {
    KLOG_INFO(KLOG_CAT_VMM, "Running VMM self-test");
    uintptr_t test_virt = 0xC0000000u;
    uintptr_t mapped_phys = 0;

    /* Verify unmapped initially */
    if (get_mapping(kernel_pdir, test_virt, &mapped_phys)) {
        KLOG_ERROR(KLOG_CAT_VMM, "Self-test FAILED: test_virt already mapped");
        return 0;
    }

    /* Allocate and map page */
    uintptr_t phys = allocate_page(kernel_pdir, test_virt, PAGE_PRESENT | PAGE_RW);
    if (!phys) {
        KLOG_ERROR(KLOG_CAT_VMM, "Self-test FAILED: allocate_page failed");
        return 0;
    }

    if (!get_mapping(kernel_pdir, test_virt, &mapped_phys) || mapped_phys != phys) {
        KLOG_ERROR(KLOG_CAT_VMM, "Self-test FAILED: get_mapping mismatch");
        free_page(kernel_pdir, test_virt);
        return 0;
    }

    /* Write 0x12345678 and verify readback */
    volatile u32 *ptr = (volatile u32 *)test_virt;
    *ptr = 0x12345678u;
    if (*ptr != 0x12345678u) {
        KLOG_ERROR(KLOG_CAT_VMM, "Self-test FAILED: pattern mismatch on write/read");
        free_page(kernel_pdir, test_virt);
        return 0;
    }

    /* Unmap and verify get_mapping returns 0 */
    free_page(kernel_pdir, test_virt);
    if (get_mapping(kernel_pdir, test_virt, &mapped_phys)) {
        KLOG_ERROR(KLOG_CAT_VMM, "Self-test FAILED: page still mapped after unmap");
        return 0;
    }

    /* Verify user_range_valid logic */
    if (user_range_valid(kernel_pdir, (const void *)0xC0000000u, 4096, 0)) {
        KLOG_ERROR(KLOG_CAT_VMM, "Self-test FAILED: user_range_valid accepted kernel address");
        return 0;
    }
    if (user_range_valid(kernel_pdir, (const void *)0xFFFFFFF0u, 32, 0)) {
        KLOG_ERROR(KLOG_CAT_VMM, "Self-test FAILED: user_range_valid accepted overflowing pointer");
        return 0;
    }

    KLOG_INFO(KLOG_CAT_VMM, "Self-test PASSED (map/write 0x12345678/read/unmap and range validation OK)");
    return vmm_isolation_self_test();
}

/* Address-space isolation invariants that the process model depends on:
 *  1. A user mapping in process A must not alter the kernel directory.
 *  2. Process B must not see process A's user mapping.
 *  3. A process directory must refuse user mappings inside the kernel range.
 *  4. Destroying both address spaces returns every frame to the PMM.
 * The test uses real page tables and USER_SPACE_START, so it fails whenever
 * a process directory shares a page table with the kernel for that range. */
int vmm_isolation_self_test(void) {
    KLOG_INFO(KLOG_CAT_VMM, "Running address-space isolation self-test");
    u32 free_before = pmm_get_free_pages_count();
    uintptr_t probe = USER_SPACE_START;
    uintptr_t kernel_phys_before = 0;
    int kernel_mapped_before = get_mapping(kernel_pdir, probe, &kernel_phys_before);

    page_directory_t *pd_a = vmm_create_address_space();
    page_directory_t *pd_b = vmm_create_address_space();
    if (!pd_a || !pd_b) {
        KLOG_ERROR(KLOG_CAT_VMM, "Isolation test FAILED: cannot create address spaces");
        vmm_destroy_address_space(pd_a);
        vmm_destroy_address_space(pd_b);
        return 0;
    }

    int ok = 1;
    uintptr_t phys_a = allocate_page(pd_a, probe, PAGE_PRESENT | PAGE_RW | PAGE_USER);
    if (!phys_a) {
        KLOG_ERROR(KLOG_CAT_VMM, "Isolation test FAILED: cannot map USER_SPACE_START in process A");
        ok = 0;
    }

    uintptr_t kernel_phys_after = 0;
    int kernel_mapped_after = get_mapping(kernel_pdir, probe, &kernel_phys_after);
    if (ok && (kernel_mapped_after != kernel_mapped_before || kernel_phys_after != kernel_phys_before)) {
        KLOG_ERROR(KLOG_CAT_VMM, "Isolation test FAILED: user mapping modified the kernel directory (shared page table)");
        klog_hex(KLOG_CAT_VMM, "  kernel translation of USER_SPACE_START now: ", kernel_phys_after);
        ok = 0;
    }

    uintptr_t phys_b = 0;
    if (ok && get_mapping(pd_b, probe, &phys_b)) {
        KLOG_ERROR(KLOG_CAT_VMM, "Isolation test FAILED: process B sees process A's page");
        ok = 0;
    }

    uintptr_t phys_check = 0;
    if (ok && (!get_mapping(pd_a, probe, &phys_check) || phys_check != phys_a)) {
        KLOG_ERROR(KLOG_CAT_VMM, "Isolation test FAILED: process A lost its own mapping");
        ok = 0;
    }

    /* A process directory must never accept a user page inside the kernel range. */
    uintptr_t kernel_probe = 0x00400000u;
    uintptr_t kprobe_phys_before = 0;
    int kprobe_before = get_mapping(kernel_pdir, kernel_probe, &kprobe_phys_before);
    uintptr_t spare = pmm_alloc_page();
    if (ok && spare) {
        int accepted = map_page(pd_a, kernel_probe, spare, PAGE_PRESENT | PAGE_RW | PAGE_USER);
        uintptr_t kprobe_phys_after = 0;
        int kprobe_after = get_mapping(kernel_pdir, kernel_probe, &kprobe_phys_after);
        if (accepted || kprobe_after != kprobe_before || kprobe_phys_after != kprobe_phys_before) {
            KLOG_ERROR(KLOG_CAT_VMM, "Isolation test FAILED: process directory accepted a user page in the kernel range");
            ok = 0;
        }
    }
    if (spare)
        pmm_free_page(spare);

    vmm_destroy_address_space(pd_a);
    vmm_destroy_address_space(pd_b);

    u32 free_after = pmm_get_free_pages_count();
    if (free_after != free_before) {
        KLOG_ERROR(KLOG_CAT_VMM, "Isolation test FAILED: PMM frame count changed after destroying address spaces");
        klog_dec(KLOG_CAT_VMM, "  free pages before: ", free_before);
        klog_dec(KLOG_CAT_VMM, "  free pages after:  ", free_after);
        ok = 0;
    }

    if (ok)
        KLOG_INFO(KLOG_CAT_VMM, "Isolation self-test PASSED (kernel directory intact, no cross-process visibility, no frame leak)");
    else
        KLOG_ERROR(KLOG_CAT_VMM, "Isolation self-test FAILED");
    return ok;
}
