#include "audio_platform.h"
#include "memory.h"
#include "scheduler.h"
#include "../../audio.h"
/* Two dedicated kernel virtual slots; physical addresses stay DMA32 even on
 * guests with RAM above 4 GiB. Borrowed mappings retain caller ownership. */
#define AUDIO_BASE (MM_KERNEL_START+UINT64_C(0x06000000))
static unsigned used;
static unsigned stream_used;
static u32 speaker_until;
u32 pci_config_read32(u8 bus,u8 dev,u8 fn,u8 reg){
    outl(0xcf8,0x80000000u|((u32)bus<<16)|((u32)dev<<11)|((u32)fn<<8)|(reg&0xfc));return inl(0xcfc);
}
void pci_config_write32(u8 bus,u8 dev,u8 fn,u8 reg,u32 value){
    outl(0xcf8,0x80000000u|((u32)bus<<16)|((u32)dev<<11)|((u32)fn<<8)|(reg&0xfc));outl(0xcfc,value);
}
int pci_scan_bus(PciDevice *out,int capacity){
    int count=0;
    /* Scan all PCI buses, including endpoints behind bridges. */
    for(unsigned b=0;b<256;b++)for(unsigned d=0;d<32;d++) {
        u32 first=pci_config_read32(b,d,0,0);if((first&65535)==65535)continue;
        unsigned functions=(pci_config_read32(b,d,0,12)&0x800000)?8:1;
        for(unsigned f=0;f<functions;f++){
            u32 id=pci_config_read32(b,d,f,0);if((id&65535)==65535)continue;
            u32 cls=pci_config_read32(b,d,f,8);
            if(count<capacity)out[count++]=(PciDevice){b,d,f,id,id>>16,cls>>24,cls>>16,cls>>8};
        }
    }
    return count;
}
void *audio_dma_alloc(uintptr_t *physical){
    unsigned slot=0;while(slot<2&&(used&(1u<<slot)))slot++;if(slot==2)return 0;
    phys_addr_t frame=pmm64_alloc(PMM_DMA32);if(!frame)return 0;
    virt_addr_t va=AUDIO_BASE+slot*MM_PAGE_SIZE;
    if(vmm64_map_borrowed(vmm64_kernel(),va,frame,VM_WRITE)!=VM_OK){pmm64_free(frame);return 0;}
    used|=1u<<slot;*physical=frame;return (void *)va;
}
void audio_dma_free(void *mapped,uintptr_t physical){
    if(!mapped)return;unsigned slot=((uintptr_t)mapped-AUDIO_BASE)/MM_PAGE_SIZE;
    memory_require(slot<2&&(used&(1u<<slot)),"audio DMA ownership");
    phys_addr_t found=0;memory_require(vmm64_unmap(vmm64_kernel(),(uintptr_t)mapped,0,&found)==VM_OK&&found==physical,"audio DMA unmap");
    pmm64_free(found);used&=~(1u<<slot);
}
int sound_is_muted(void){return audio_get_volume()==0;}
void *audio_stream_dma_alloc(uintptr_t *physical){
    unsigned slot=0;while(slot<17&&(stream_used&(1u<<slot)))slot++;if(slot==17)return 0;
    phys_addr_t frame=pmm64_alloc(PMM_DMA32);if(!frame)return 0;
    virt_addr_t va=AUDIO_BASE+(slot+2)*MM_PAGE_SIZE;
    if(vmm64_map_borrowed(vmm64_kernel(),va,frame,VM_WRITE)!=VM_OK){pmm64_free(frame);return 0;}
    stream_used|=1u<<slot;*physical=frame;return (void *)va;
}
void audio_stream_dma_free(void *mapped,uintptr_t physical){
    if(!mapped)return;unsigned slot=((uintptr_t)mapped-AUDIO_BASE)/MM_PAGE_SIZE-2;
    memory_require(slot<17&&(stream_used&(1u<<slot)),"stream DMA ownership");
    phys_addr_t found=0;memory_require(vmm64_unmap(vmm64_kernel(),(uintptr_t)mapped,0,&found)==VM_OK&&found==physical,"stream DMA unmap");
    pmm64_free(found);stream_used&=~(1u<<slot);
}
void speaker_beep(u32 hz,u32 ms){
    if(hz<19||hz>20000||!ms)return;
    unsigned divisor=1193182/hz;outb(0x43,0xb6);
    outb(0x42,divisor);outb(0x42,divisor>>8);outb(0x61,inb(0x61)|3);
    speaker_until=ticks+(ms*TIMER64_HZ+999)/1000;
}
void audio64_poll(void){if(speaker_until&&(int32_t)(ticks-speaker_until)>=0){outb(0x61,inb(0x61)&~3);speaker_until=0;}}
