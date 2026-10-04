#ifndef POLLIK_VMM_H
#define POLLIK_VMM_H

#include "system.h"

#define PAGE_SIZE 4096u

/* Page Table & Directory Entry Flags
 * NOTE: In standard x86 32-bit paging (without PAE/NX), the CPU enforces:
 * - Bit 0 (P): Present (1) / Not Present (0)
 * - Bit 1 (R/W): Read/Write (1) / Read-Only (0) (Enforced in Ring 0 via CR0.WP)
 * - Bit 2 (U/S): User (1, Ring 3 accessible) / Supervisor (0, Ring 0-2 only)
 * There is no separate NX (No-Execute) bit without PAE. All readable pages are executable.
 */
#define PAGE_PRESENT   0x001u
#define PAGE_RW        0x002u
#define PAGE_USER      0x004u
#define PAGE_WRITETHRU 0x008u
#define PAGE_NOCACHE   0x010u
#define PAGE_ACCESSED  0x020u
#define PAGE_DIRTY     0x040u

typedef u32 page_directory_t;
typedef u32 page_table_t;

void vmm_init(void);
page_directory_t *vmm_create_address_space(void);
void vmm_destroy_address_space(page_directory_t *pd);
void vmm_switch_address_space(page_directory_t *pd);
page_directory_t *vmm_get_kernel_directory(void);

int map_page(page_directory_t *pd, uintptr_t virt_addr, uintptr_t phys_addr, u32 flags);
int unmap_page(page_directory_t *pd, uintptr_t virt_addr);
int get_mapping(page_directory_t *pd, uintptr_t virt_addr, uintptr_t *out_phys_addr);
int get_mapping_flags(page_directory_t *pd, uintptr_t virt_addr, uintptr_t *out_phys, u32 *out_flags);

uintptr_t allocate_page(page_directory_t *pd, uintptr_t virt_addr, u32 flags);
void free_page(page_directory_t *pd, uintptr_t virt_addr);

/* Guard pages for stack overflow detection */
void vmm_set_guard_page(page_directory_t *pd, uintptr_t virt_addr);
int vmm_is_guard_page(uintptr_t virt_addr);

/* 32-bit virtual address layout.
 *
 * The kernel identity-maps physical RAM below KERNEL_DIRECT_MAP_TOP and the
 * framebuffer MMIO; those page directory entries are shared (supervisor-only)
 * by every process directory. User space occupies a disjoint range, so a user
 * mapping can never land in a shared kernel page table. RAM above
 * KERNEL_DIRECT_MAP_TOP is not managed by this 32-bit kernel (no highmem).
 *
 *   0x00000000 - 0x3FFFFFFF  kernel direct map (RAM), legacy workers, heap
 *   0x40000000 - 0xBFFFFFFF  user space (ELF image, heap, stack)
 *   0xC0000000 - 0xFFFFFFFF  kernel-only (VBE LFB MMIO, self-test window)
 */
#define KERNEL_DIRECT_MAP_TOP 0x40000000u /* 1 GiB */
#define USER_SPACE_START  KERNEL_DIRECT_MAP_TOP
#define USER_SPACE_END    0xC0000000u /* 3 GiB */
#define USER_STACK_TOP    0xC0000000u
#define USER_STACK_SIZE   (16u * 1024u)
#define USER_STACK_BOTTOM (USER_STACK_TOP - USER_STACK_SIZE)
#define USER_GUARD_PAGE   (USER_STACK_BOTTOM - PAGE_SIZE)

/* Central Userspace Pointer Validation & Safe Copying API */
int user_range_valid(page_directory_t *pd, const void *user_ptr, u32 size, int write);
int copy_from_user(void *kernel_dest, const void *user_src, u32 size);
int copy_to_user(void *user_dest, const void *kernel_src, u32 size);
int copy_string_from_user(char *kernel_dest, const char *user_src, u32 max_len);

void vmm_page_fault_handler(void *frame_ptr);
int vmm_self_test(void);
int vmm_isolation_self_test(void);

#endif
