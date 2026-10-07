#include <stdio.h>
#include <stdlib.h>
#define POLLIK_X64 1
#include "../kernel/arch/x86_64/audio_platform.c"
u32 ticks;
static int live,mappings,allocation_fail,mapping_fail,next;
PmmZone requested;
phys_addr_t pmm64_alloc(PmmZone z){requested=z;if(allocation_fail)return 0;live++;return 0x100000+(next++)*4096;}
int pmm64_free(phys_addr_t p){(void)p;live--;return 1;}
AddressSpace *vmm64_kernel(void){return 0;}
static phys_addr_t slots[2];
VmResult vmm64_map_borrowed(AddressSpace *s,virt_addr_t v,phys_addr_t p,unsigned f){
 (void)s;if(mapping_fail)return VM_NOMEM;if(f!=VM_WRITE||p>=MM_DMA32_END)abort();slots[(v-AUDIO_BASE)/4096]=p;mappings++;return VM_OK;
}
VmResult vmm64_unmap(AddressSpace *s,virt_addr_t v,int release,phys_addr_t *p){(void)s;if(release)abort();*p=slots[(v-AUDIO_BASE)/4096];mappings--;return VM_OK;}
void memory_require(int c,const char *text){if(!c){puts(text);abort();}}
u8 audio_get_volume(void){return 85;}
void hal_port_write8(unsigned short p,unsigned char v){(void)p;(void)v;}
void hal_port_write32(unsigned short p,unsigned int v){(void)p;(void)v;}
unsigned char hal_port_read8(unsigned short p){(void)p;return 0;}
unsigned int hal_port_read32(unsigned short p){(void)p;return 65535;}
int main(void){
 uintptr_t physical=0;allocation_fail=1;
 if(audio_dma_alloc(&physical)||live||used)return 1;
 allocation_fail=0;mapping_fail=1;
 if(audio_dma_alloc(&physical)||live||used||mappings)return 2;
 mapping_fail=0;uintptr_t a,b;void *one=audio_dma_alloc(&a),*two=audio_dma_alloc(&b);
 if(!one||!two||live!=2||mappings!=2||used!=3||requested!=PMM_DMA32||audio_dma_alloc(&physical))return 3;
 audio_dma_free(one,a);audio_dma_free(two,b);
 if(live||mappings||used)return 4;
 puts("PASS x64 DMA32: PMM failure, borrowed-map rollback, two-slot limit, free frames/mappings exactly baseline");return 0;
}
