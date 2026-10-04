/* Sparse, dynamically backed version of PollikOS's bitmap frame allocator.
 * Two bits/frame distinguish free, allocated and VMM-owned from unavailable.
 * Metadata scales with usable RAM, not the largest E820 hole/address. */
#include "memory.h"
#include "paging.h"
#define MAX_INTERVALS 160
#define CHUNK_FRAMES 15872u
typedef struct {
    phys_addr_t next;
    pfn_t base;
    page_count_t count, free;
    uint64_t states[496];
} BitmapPage;
_Static_assert(sizeof(BitmapPage) <= MM_PAGE_SIZE, "PMM metadata page");
typedef struct { pfn_t first, end; int usable; } Interval;
static Interval intervals[MAX_INTERVALS];
static pfn_t boundaries[MAX_INTERVALS*2];
static PhysicalRange available[MAX_INTERVALS*2];
static size_t available_count;
static phys_addr_t head;
static PmmStats stats;
#ifdef SELFTEST
static int64_t allocation_budget = -1;
void pmm64_fail_after(int64_t count) { allocation_budget = count; }
#endif

static unsigned state(const volatile BitmapPage *page, page_count_t i) {
    return (unsigned)((page->states[i/32] >> ((i%32)*2)) & 3);
}
static void set_state(volatile BitmapPage *page, page_count_t i, unsigned value) {
    unsigned shift = (unsigned)(i%32)*2;
    page->states[i/32] = (page->states[i/32] & ~(UINT64_C(3) << shift)) | ((uint64_t)value << shift);
}
static int transition(phys_addr_t frame, unsigned from, unsigned to) {
    if (!frame || (frame & (MM_PAGE_SIZE-1))) return 0;
    pfn_t pfn = frame / MM_PAGE_SIZE;
    for (phys_addr_t cursor = head; cursor;) {
        volatile BitmapPage *page = (volatile BitmapPage *)physical_view(cursor);
        if (pfn >= page->base && pfn-page->base < page->count) {
            page_count_t index = pfn-page->base;
            if (state(page, index) != from) return 0;
            set_state(page, index, to);
            if (from == 1) { --page->free; --stats.free; }
            if (to == 1) { ++page->free; ++stats.free; }
            return 1;
        }
        cursor = page->next;
    }
    return 0;
}
static phys_addr_t early_frame(void) {
    /* Consume metadata from the top down; mark it unavailable after bitmaps exist. */
    for (size_t i = available_count; i; --i) {
        if (available[i-1].end > available[i-1].base) {
            available[i-1].end -= MM_PAGE_SIZE;
            return available[i-1].end;
        }
    }
    return 0;
}
int pmm64_init(const MemoryMapEntry *map, size_t count,
               const PhysicalRange *reserved, size_t reserved_count) {
    if (head || !map || !count || count > 64 || reserved_count > 16 ||
        (reserved_count && !reserved)) return 0;
    size_t n = 0, nb = 0;
    phys_addr_t limit = physical_limit();
    for (size_t i = 0; i < count + reserved_count; ++i) {
        phys_addr_t base, end;
        int usable = 0;
        if (i < count) {
            if (!(map[i].attributes & 1) || !map[i].length) continue;
            base = map[i].base;
            if (map[i].length > UINT64_MAX-base) return 0;
            end = base + map[i].length;
            usable = map[i].type == 1;
        } else {
            base = reserved[i-count].base;
            end = reserved[i-count].end;
            if (end < base) return 0;
        }
        if (base >= limit || base == end) continue;
        if (end > limit) end = limit;
        pfn_t first = usable ? (base+4095)/4096 : base/4096;
        pfn_t last = usable ? end/4096 : (end+4095)/4096;
        if (first >= last) continue;
        intervals[n++] = (Interval){first, last, usable};
        boundaries[nb++] = first;
        boundaries[nb++] = last;
    }
    /* Sort endpoints; non-usable entries win, irrespective of firmware ordering. */
    for (size_t i = 1; i < nb; ++i) {
        pfn_t value = boundaries[i];
        size_t j = i;
        while (j && boundaries[j-1] > value) { boundaries[j] = boundaries[j-1]; --j; }
        boundaries[j] = value;
    }
    available_count = 0;
    for (size_t i = 1; i < nb; ++i) {
        pfn_t first = boundaries[i-1], end = boundaries[i];
        if (first == end) continue;
        int usable = 0, blocked = first == 0;
        for (size_t j = 0; j < n; ++j) {
            if (intervals[j].first <= first && intervals[j].end >= end) {
                if (intervals[j].usable) usable = 1;
                else blocked = 1;
            }
        }
        if (!usable || blocked) continue;
        if (available_count && available[available_count-1].end == first*4096)
            available[available_count-1].end = end*4096;
        else available[available_count++] = (PhysicalRange){first*4096, end*4096};
    }
    /* Keep immutable ranges while early_frame consumes the allocation cursors. */
    PhysicalRange ranges[MAX_INTERVALS*2];
    page_count_t required = 0, total = 0;
    for (size_t i = 0; i < available_count; ++i) {
        ranges[i] = available[i];
        page_count_t pages = (ranges[i].end-ranges[i].base)/4096;
        required += (pages + CHUNK_FRAMES-1)/CHUNK_FRAMES;
        total += pages;
    }
    if (!total || required >= total) return 0;
    stats = (PmmStats){total, total, required, 0};
    phys_addr_t tail = 0;
    for (size_t i = 0; i < available_count; ++i) {
        pfn_t end = ranges[i].end/4096;
        for (pfn_t first = ranges[i].base/4096; first < end;) {
            phys_addr_t frame = early_frame();
            if (!frame) memory_panic("PMM metadata sizing inconsistency");
            physical_zero(frame);
            page_count_t pages = end-first;
            if (pages > CHUNK_FRAMES) pages = CHUNK_FRAMES;
            volatile BitmapPage *page = (volatile BitmapPage *)physical_view(frame);
            page->base = first; page->count = pages; page->free = pages;
            for (page_count_t k = 0; k < pages; ++k) set_state(page, k, 1);
            if (tail) ((volatile BitmapPage *)physical_view(tail))->next = frame;
            else head = frame;
            tail = frame;
            pfn_t high_start = first > MM_DMA32_END/4096 ? first : MM_DMA32_END/4096;
            if (first+pages > high_start) stats.above4g += first+pages-high_start;
            first += pages;
        }
    }
    for (phys_addr_t cursor = head; cursor;) {
        phys_addr_t next = ((volatile BitmapPage *)physical_view(cursor))->next;
        memory_require(transition(cursor, 1, 0), "reserve PMM metadata");
        cursor = next;
    }
    return 1;
}
static phys_addr_t allocate_between(pfn_t minimum, pfn_t maximum) {
    for (phys_addr_t cursor = head; cursor;) {
        volatile BitmapPage *page = (volatile BitmapPage *)physical_view(cursor);
        if (page->free && page->base < maximum && page->base+page->count > minimum) {
            page_count_t start = minimum > page->base ? minimum-page->base : 0;
            page_count_t end = page->count;
            if (maximum-page->base < end) end = maximum-page->base;
            for (page_count_t i = start; i < end; ++i) {
                if (state(page, i) == 1) {
                    set_state(page, i, 2);
                    --page->free; --stats.free;
                    return (page->base+i)*4096;
                }
            }
        }
        cursor = page->next;
    }
    return 0;
}
/* First run of `count` consecutive free frames inside [minimum, maximum).
 * A run never crosses a metadata chunk boundary: physical continuity is only
 * guaranteed within one BitmapPage, so requests larger than CHUNK_FRAMES fail
 * rather than silently returning a non-contiguous set. */
static phys_addr_t contiguous_between(pfn_t minimum, pfn_t maximum, page_count_t count) {
    if (!count || minimum >= maximum) return 0;
    for (phys_addr_t cursor = head; cursor;) {
        volatile BitmapPage *page = (volatile BitmapPage *)physical_view(cursor);
        page_count_t start = minimum > page->base ? minimum - page->base : 0;
        page_count_t end = page->count;
        if (maximum > page->base) {
            page_count_t cap = maximum - page->base;
            if (cap < end) end = cap;
        }
        if (start < end) {
            page_count_t run = 0;
            for (page_count_t i = start; i < end; ++i) {
                if (state(page, i) == 1) {
                    if (++run == count) {
                        page_count_t first = i + 1 - count;
                        for (page_count_t k = 0; k < count; ++k)
                            set_state(page, first + k, 2);
                        page->free -= count;
                        stats.free -= count;
                        return (page->base + first) * 4096;
                    }
                } else run = 0;
            }
        }
        cursor = page->next;
    }
    return 0;
}
phys_addr_t pmm64_alloc(PmmZone zone) {
    memory_context_check();
    if (zone != PMM_NORMAL && zone != PMM_DMA32) return 0;
#ifdef SELFTEST
    if (!allocation_budget) return 0;
#endif
    phys_addr_t frame = 0;
    if (zone == PMM_NORMAL) frame = allocate_between(MM_DMA32_END/4096, physical_limit()/4096);
    if (!frame) frame = allocate_between(1, MM_DMA32_END/4096);
#ifdef SELFTEST
    if (frame && allocation_budget > 0) --allocation_budget;
#endif
    return frame;
}
phys_addr_t pmm64_alloc_contiguous(page_count_t count, PmmZone zone) {
    memory_context_check();
    if ((zone != PMM_NORMAL && zone != PMM_DMA32) || !count) return 0;
#ifdef SELFTEST
    if (allocation_budget >= 0 && allocation_budget < (int64_t)count) return 0;
#endif
    phys_addr_t frame = 0;
    if (zone == PMM_NORMAL) {
        frame = contiguous_between(MM_DMA32_END/4096, physical_limit()/4096, count);
        if (!frame) frame = contiguous_between(1, MM_DMA32_END/4096, count);
    } else {
        frame = contiguous_between(1, MM_DMA32_END/4096, count);
    }
#ifdef SELFTEST
    if (frame && allocation_budget > 0) allocation_budget -= (int64_t)count;
#endif
    return frame;
}
int pmm64_free_contiguous(phys_addr_t frame, page_count_t count) {
    if (!frame || (frame & (MM_PAGE_SIZE-1)) || !count) return 0;
    /* Validate the whole range before touching state: a partially-owned
     * argument frees nothing instead of leaving a half-released run. */
    for (page_count_t i = 0; i < count; ++i)
        if (!pmm64_is_allocated(frame + i*MM_PAGE_SIZE)) return 0;
    for (page_count_t i = 0; i < count; ++i)
        memory_require(pmm64_free(frame + i*MM_PAGE_SIZE), "contiguous free");
    return 1;
}
int pmm64_free(phys_addr_t frame) { return transition(frame, 2, 1); }
int pmm64_is_allocated(phys_addr_t frame) { return transition(frame, 2, 2); }
int pmm64_claim(phys_addr_t frame) { return transition(frame, 2, 3); }
int pmm64_free_owned(phys_addr_t frame) { return transition(frame, 3, 1); }
PmmStats pmm64_stats(void) { return stats; }
