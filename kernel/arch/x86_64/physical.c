#include "paging.h"
extern uint64_t boot_pt[512];
static phys_addr_t limit;

void physical_window_init(void) {
    uint32_t a, b, c, d;
    hal_cpuid(0x80000000, 0, &a, &b, &c, &d);
    unsigned bits = 36; /* architectural PAE fallback if address-width leaf absent */
    if (a >= 0x80000008) {
        hal_cpuid(0x80000008, 0, &a, &b, &c, &d);
        bits = a & 255;
    }
    memory_require(bits >= 32 && bits <= 52, "physical address width");
    limit = UINT64_C(1) << bits;
    boot_pt[510] = (uintptr_t)boot_pt | PTE_PRESENT | PTE_WRITE | PTE_NX;
    boot_pt[511] = 0;
    invalidate(WINDOW_PT);
    invalidate(WINDOW_DATA);
}
phys_addr_t physical_limit(void) { return limit; }
volatile uint64_t *physical_view(phys_addr_t frame) {
    memory_context_check();
    memory_require(!(frame & (MM_PAGE_SIZE-1)) && frame < limit, "invalid physical frame");
    volatile uint64_t *pt = (volatile uint64_t *)(uintptr_t)WINDOW_PT;
    pt[511] = frame | PTE_PRESENT | PTE_WRITE | PTE_NX;
    invalidate(WINDOW_DATA);
    return (volatile uint64_t *)(uintptr_t)WINDOW_DATA;
}
void physical_zero(phys_addr_t frame) {
    volatile uint64_t *words = physical_view(frame);
    for (size_t i = 0; i < 512; ++i) words[i] = 0;
}
