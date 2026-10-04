#include "fpu.h"
#include "../../hal.h"

/* FXSAVE64 preserves x87, MMX and the x86-64 baseline SSE register file.
 * AVX is deliberately not enabled, so no upper YMM state can be lost. */
static _Alignas(16) uint8_t initial_state[FPU64_STATE_SIZE];
static int initialized;

int fpu64_init(void) {
    uint32_t eax = 1, ebx, ecx = 0, edx;
    hal_cpuid(eax, ecx, &eax, &ebx, &ecx, &edx);
    (void)ebx;
    const uint32_t required = (1u << 0) | (1u << 24) | (1u << 25) | (1u << 26);
    if ((edx & required) != required) return 0; /* FPU, FXSR, SSE and SSE2 */

    uint64_t cr0, cr4;
    cr0 = hal_read_cr0();
    cr4 = hal_read_cr4();
    cr4 |= (1ull << 9) | (1ull << 10); /* OSFXSR and OSXMMEXCPT */
    hal_write_cr4(cr4);
    cr0 &= ~((1ull << 2) | (1ull << 3)); /* clear EM and TS */
    cr0 |= (1ull << 1) | (1ull << 5);    /* MP and native x87 exceptions */
    hal_write_cr0(cr0);

    const uint32_t mxcsr = 0x1f80; /* masked exceptions, round-to-nearest */
    __asm__ volatile("fninit; ldmxcsr %0" : : "m"(mxcsr) : "memory");
    fpu64_state_save(initial_state);
    initialized = 1;
    return 1;
}

int fpu64_state_init(void *state) {
    if (!initialized || !state || ((uintptr_t)state & 15)) return 0;
    uint8_t *destination = state;
    for (unsigned i = 0; i < FPU64_STATE_SIZE; ++i) destination[i] = initial_state[i];
    return 1;
}

void fpu64_state_save(void *state) {
    __asm__ volatile("fxsave64 (%0)" : : "r"(state) : "memory");
}

void fpu64_state_restore(const void *state) {
    __asm__ volatile("fxrstor64 (%0)" : : "r"(state) : "memory");
}
