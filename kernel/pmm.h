#ifndef POLLIK_PMM_H
#define POLLIK_PMM_H

#include "system.h"

#define PMM_PAGE_SIZE 4096u
#define PMM_MAX_PAGES 1048576u /* 4 GiB max addressable in 32-bit */
#define PMM_BITMAP_DWORDS (PMM_MAX_PAGES / 32u)

typedef struct __attribute__((packed)) {
    u64 base;
    u64 length;
    u32 type;     /* 1 = Usable RAM, 2 = Reserved, 3 = ACPI Reclaim, 4 = ACPI NVS, 5 = Bad */
    u32 acpi_ext; /* Bit 0: 1 = entry is valid (ACPI 3.0) */
} E820Entry;

void pmm_init(void);
uintptr_t pmm_alloc_page(void);
void pmm_free_page(uintptr_t phys_addr);
uintptr_t pmm_alloc_pages(u32 count);
void pmm_free_pages(uintptr_t phys_addr, u32 count);
void pmm_reserve_range(uintptr_t start, uintptr_t end);
void pmm_free_range(uintptr_t start, uintptr_t end);

u32 pmm_get_total_memory(void);
u32 pmm_get_usable_memory(void);
u32 pmm_get_reserved_memory(void);
u32 pmm_get_free_memory(void);
u32 pmm_get_free_pages_count(void);
u32 pmm_get_used_pages_count(void);
u32 pmm_get_total_pages_count(void);
u32 pmm_get_highest_ram_addr(void);
int pmm_self_test(void);

#endif
