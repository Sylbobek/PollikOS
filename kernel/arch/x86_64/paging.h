#ifndef POLLIK_X64_PAGING_H
#define POLLIK_X64_PAGING_H
#include "memory.h"
#include "../../hal.h"
/* Architectural bits stay private to the page-table implementation. */
#define PTE_PRESENT UINT64_C(1)
#define PTE_WRITE UINT64_C(2)
#define PTE_USER UINT64_C(4)
#define PTE_PWT UINT64_C(8)
#define PTE_PCD UINT64_C(16)
#define PTE_HUGE UINT64_C(128)
#define PTE_OWNED UINT64_C(512)
#define PTE_GUARD UINT64_C(1024)
#define PTE_NX (UINT64_C(1) << 63)
#define PTE_ADDRESS UINT64_C(0x000ffffffffff000)
#define WINDOW_PT UINT64_C(0x1fe000)
#define WINDOW_DATA UINT64_C(0x1ff000)
static inline void invalidate(virt_addr_t address) {
    hal_invalidate_page((const void *)address);
}
static inline void memory_context_check(void) {
    memory_require(!hal_interrupts_enabled(), "memory APIs require serialized BSP/IF=0");
}
#endif
