#include <stddef.h>
#ifdef POLLIK_X64
#include "arch/x86_64/paging.h"
void *fs_journal_alloc(size_t bytes) {
    uintptr_t base=MM_KERNEL_START+UINT64_C(0x3d000000);
    size_t pages=(bytes+4095)/4096,mapped=0;
    while(mapped<pages && vmm64_alloc_page(vmm64_kernel(),base+mapped*4096,VM_WRITE)==VM_OK) ++mapped;
    if(mapped==pages) return (void *)base;
    while(mapped) { --mapped;memory_require(vmm64_unmap(vmm64_kernel(),base+mapped*4096,1,0)==VM_OK,"journal cache rollback"); }
    return 0;
}
#else
#include "mem.h"
void *fs_journal_alloc(size_t bytes) { return kmalloc((u32)bytes); }
#endif
