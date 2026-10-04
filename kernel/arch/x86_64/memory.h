#ifndef POLLIK_X64_MEMORY_H
#define POLLIK_X64_MEMORY_H
#include <stdint.h>
#include <stddef.h>

typedef uint64_t phys_addr_t;
typedef uintptr_t virt_addr_t;
typedef uint64_t pfn_t;
typedef uint64_t page_count_t;
_Static_assert(sizeof(virt_addr_t) == 8 && sizeof(size_t) == 8, "x86_64 ABI");
#define MM_PAGE_SIZE UINT64_C(4096)
#define MM_DMA32_END UINT64_C(0x100000000)
#define MM_USER_START UINT64_C(0x0000008000000000)
#define MM_USER_END UINT64_C(0x0000800000000000)
#define MM_KERNEL_START UINT64_C(0xffffff8000000000)

typedef struct __attribute__((packed)) {
    phys_addr_t base;
    uint64_t length;
    uint32_t type, attributes;
} MemoryMapEntry;
typedef struct { phys_addr_t base, end; } PhysicalRange;
typedef enum { PMM_NORMAL, PMM_DMA32 } PmmZone;
typedef struct {
    page_count_t managed, free, metadata, above4g;
} PmmStats;

/* BSP-only, IF=0. The physical aperture is not reentrant/NMI-safe. Its pointer
 * expires on the next physical_view call. NMI handlers must not use memory APIs.
 * No physical address is converted directly to a C pointer. */
void physical_window_init(void);
volatile uint64_t *physical_view(phys_addr_t frame);
void physical_zero(phys_addr_t frame);
phys_addr_t physical_limit(void);
void memory_panic(const char *message) __attribute__((noreturn));
void memory_require(int condition, const char *message);
void memory_log(const char *text);
void memory_hex(uint64_t number);

/* Reserved/metadata pages cannot be freed. Zero means allocation failure.
 * NORMAL prefers high RAM, then falls back; DMA32 is an explicit constraint. */
int pmm64_init(const MemoryMapEntry *map, size_t count,
               const PhysicalRange *reserved, size_t reserved_count);
phys_addr_t pmm64_alloc(PmmZone zone);
/* Physically consecutive frames for DMA descriptor lists. count==0 or an
 * invalid zone fails; a run never crosses a metadata chunk, so requests larger
 * than the chunk size fail. Freeing validates the whole range first, so a
 * partially-owned argument frees nothing. */
phys_addr_t pmm64_alloc_contiguous(page_count_t count, PmmZone zone);
int pmm64_free_contiguous(phys_addr_t frame, page_count_t count);
int pmm64_free(phys_addr_t frame);
int pmm64_is_allocated(phys_addr_t frame);
PmmStats pmm64_stats(void);
/* Internal VMM ownership transfer: owned frames cannot be pmm64_free'd. */
int pmm64_claim(phys_addr_t frame);
int pmm64_free_owned(phys_addr_t frame);
#ifdef SELFTEST
void pmm64_fail_after(int64_t successful_allocations); /* -1 disables injection */
#endif

typedef struct { phys_addr_t root; } AddressSpace;
typedef enum { VM_OK, VM_INVALID, VM_EXISTS, VM_MISSING, VM_NOMEM, VM_BUSY } VmResult;
enum { VM_WRITE = 1, VM_USER = 2, VM_EXEC = 4, VM_DEVICE = 8 };
typedef struct { phys_addr_t physical; unsigned flags; int owned, guard; } Mapping;
typedef struct { virt_addr_t base, top; size_t pages, guards; } GuardedStack;
int vmm64_init(void);
AddressSpace *vmm64_kernel(void);
VmResult vmm64_create(AddressSpace *space);
VmResult vmm64_switch(const AddressSpace *space);
VmResult vmm64_destroy(AddressSpace *space); /* refuses active/kernel roots */
/* Borrowed mappings never acquire/free the frame. USER requires caller-owned
 * allocated RAM; kernel VM_DEVICE mappings allow external MMIO, always NX/UC. */
VmResult vmm64_map_borrowed(AddressSpace *space, virt_addr_t address,
                            phys_addr_t frame, unsigned flags);
/* Allocates a zeroed frame and transfers sole ownership to the address space. */
VmResult vmm64_alloc_page(AddressSpace *space, virt_addr_t address, unsigned flags);
VmResult vmm64_lookup(const AddressSpace *space, virt_addr_t address, Mapping *out);
/* release=1 frees only owned frames. release=0 returns a caller-owned frame
 * only for borrowed mappings; owned mappings must be released, not detached. */
VmResult vmm64_unmap(AddressSpace *space, virt_addr_t address, int release,
                     phys_addr_t *borrowed_frame);
VmResult vmm64_stack_create(AddressSpace *space, virt_addr_t base, size_t pages,
                            size_t guards, int user, GuardedStack *stack);
VmResult vmm64_stack_destroy(AddressSpace *space, GuardedStack *stack);
#ifdef SELFTEST
void memory_selftest(void);
void memory_fault_test(void (*probe)(void *), uintptr_t address,
                       uintptr_t rip, uint64_t error, const char *name);
#endif
#endif
