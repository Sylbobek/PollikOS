#include "account.h"
#include "hal.h"
#ifdef POLLIK_X64
#include "arch/x86_64/paging.h"
#define ACCOUNT_WORK_BASE (MM_KERNEL_START+UINT64_C(0x38000000))
void *account_work_alloc(size_t bytes) {
    size_t pages=(bytes+4095)/4096, mapped=0;
    while(mapped<pages && vmm64_alloc_page(vmm64_kernel(),ACCOUNT_WORK_BASE+mapped*4096,VM_WRITE)==VM_OK) ++mapped;
    if(mapped==pages) return (void *)(uintptr_t)ACCOUNT_WORK_BASE;
    while(mapped) { --mapped; memory_require(vmm64_unmap(vmm64_kernel(),ACCOUNT_WORK_BASE+mapped*4096,1,0)==VM_OK,"account KDF rollback"); }
    return 0;
}
void account_work_free(void *memory,size_t bytes) {
    for(size_t i=0;i<(bytes+4095)/4096;i++) memory_require(vmm64_unmap(vmm64_kernel(),(uintptr_t)memory+i*4096,1,0)==VM_OK,"account KDF cleanup");
}
#else
#include "pmm.h"
void *account_work_alloc(size_t bytes) { return (void *)pmm_alloc_pages((u32)((bytes+4095)/4096)); }
void account_work_free(void *memory,size_t bytes) { pmm_free_pages((uintptr_t)memory,(u32)((bytes+4095)/4096)); }
#endif
uint64_t account_platform_time(void) { return hal_read_tsc(); }
int account_platform_random(uint32_t *value) { if(!hal_cpu_has_cpuid()) return 0;
    unsigned a,b,c,d; hal_cpuid(1,0,&a,&b,&c,&d); if(!(c&(1u<<30))) return 0;
    for(unsigned i=0;i<10;i++) if(hal_rdrand32(value)) return 1; return 0; }
