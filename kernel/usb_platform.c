/* Kernel-owned DMA32 allocator and PCI/MMIO hooks for libpayload USB. */
#include <libpayload.h>
#include <pci/pci.h>
#include "hal.h"
#include "usb_input.h"
#ifdef POLLIK_X64
#include "arch/x86_64/paging.h"
#define USB_POOL_VA (MM_KERNEL_START+UINT64_C(0x40000000))
#define USB_MMIO_VA (MM_KERNEL_START+UINT64_C(0x41000000))
extern void serial(const char *text);
#else
#undef inb
#undef inw
#undef inl
#undef outb
#undef outw
#undef outl
#include "pmm.h"
#include "vmm.h"
#include "hw.h"
#define USB_POOL_VA 0
#define USB_MMIO_VA 0xe8000000u
#endif
#define USB_POOL_BYTES (8u*1024*1024)
typedef struct Block {size_t bytes;struct Block *next;int free;} Block;
static unsigned char *pool;
static uintptr_t pool_physical;
static Block *blocks;
static unsigned long long cycles_per_us;
static struct {uintptr_t physical,virtual_base;unsigned bytes;} mmio[16];
static unsigned mmio_count;
int usb_platform_start(void){
    if(pool)return 1;
    unsigned cpu_features=0;
    if(hal_cpu_has_cpuid())hal_cpuid(1,0,NULL,NULL,NULL,&cpu_features);
    if(!(cpu_features&16))return 0; /* This host stack requires a calibrated timer. */
#ifdef POLLIK_X64
    phys_addr_t physical=pmm64_alloc_contiguous(USB_POOL_BYTES/4096,PMM_DMA32);if(!physical)return 0;
    unsigned mapped=0;while(mapped<USB_POOL_BYTES/4096&&vmm64_map_borrowed(vmm64_kernel(),USB_POOL_VA+(uint64_t)mapped*4096,physical+(uint64_t)mapped*4096,VM_WRITE)==VM_OK)mapped++;
    if(mapped!=USB_POOL_BYTES/4096){while(mapped){mapped--;vmm64_unmap(vmm64_kernel(),USB_POOL_VA+(uint64_t)mapped*4096,0,0);}pmm64_free_contiguous(physical,USB_POOL_BYTES/4096);return 0;}
    pool=(unsigned char *)(uintptr_t)USB_POOL_VA;pool_physical=(uintptr_t)physical;
#else
    pool_physical=pmm_alloc_pages(USB_POOL_BYTES/4096);if(!pool_physical)return 0;pool=(void *)pool_physical;
#endif
    memset(pool,0,USB_POOL_BYTES);blocks=(Block *)pool;blocks->bytes=USB_POOL_BYTES-sizeof(Block);blocks->next=NULL;blocks->free=1;
    /* Calibrate TSC using PIT channel 2 once, preserving the speaker gate. */
    unsigned char gate=hal_port_read8(0x61);hal_port_write8(0x61,(gate&~2u)|1u);
    hal_port_write8(0x43,0xb0);hal_port_write8(0x42,0x9c);hal_port_write8(0x42,0x2e);
    unsigned long long start=hal_read_tsc();
    for(unsigned i=0;i<10000000&&!(hal_port_read8(0x61)&0x20);i++)hal_cpu_relax();
    cycles_per_us=(hal_read_tsc()-start)/10000u;if(!cycles_per_us)cycles_per_us=1;
    hal_port_write8(0x61,gate);return 1;
}
void usb_lp_delay(unsigned usec){unsigned long long start=hal_read_tsc(),duration=cycles_per_us*usec;while(hal_read_tsc()-start<duration)hal_cpu_relax();}
unsigned usb_platform_millis(void){return (unsigned)(hal_read_tsc()/(cycles_per_us*1000u));}
void *usb_lp_memalign(size_t alignment,size_t bytes){
    if(!pool||!bytes||bytes>USB_POOL_BYTES||!alignment||(alignment&(alignment-1)))return NULL;
    if(alignment<16)alignment=16;
    for(Block *b=blocks;b;b=b->next)if(b->free){
        uintptr_t data=(uintptr_t)(b+1),physical=pool_physical+data-(uintptr_t)pool;
        uintptr_t aligned_physical=(physical+sizeof(Block *)+alignment-1)&~(uintptr_t)(alignment-1);
        uintptr_t aligned=data+aligned_physical-physical; /* DMA alignment refers to the bus address. */
        size_t used=(size_t)(aligned-data)+bytes;used=(used+15)&~(size_t)15;
        if(used>b->bytes)continue;
        if(b->bytes-used>sizeof(Block)+32){Block *tail=(Block *)(data+used);tail->bytes=b->bytes-used-sizeof(Block);tail->next=b->next;tail->free=1;b->next=tail;b->bytes=used;}
        b->free=0;((Block **)aligned)[-1]=b;memset((void *)aligned,0,bytes);return (void *)aligned;
    }
    return NULL;
}
void *usb_lp_malloc(size_t bytes){return usb_lp_memalign(16,bytes);}
void *usb_lp_calloc(size_t count,size_t bytes){if(bytes&&count>USB_POOL_BYTES/bytes)return NULL;return usb_lp_malloc(count*bytes);}
void *usb_lp_xzalloc(size_t bytes){void *p=usb_lp_malloc(bytes);if(!p)usb_lp_fatal("USB allocation failed");return p;}
void usb_lp_free(void *p){
    if(!p)return;if((uintptr_t)p<(uintptr_t)pool+sizeof(Block *)||(uintptr_t)p>=(uintptr_t)pool+USB_POOL_BYTES)usb_lp_fatal("USB allocator ownership");
    Block *b=((Block **)p)[-1];if((uintptr_t)b<(uintptr_t)pool||(uintptr_t)b>=(uintptr_t)pool+USB_POOL_BYTES||b->free)usb_lp_fatal("USB allocator duplicate free");b->free=1;
    for(Block *it=blocks;it&&it->next;)if(it->free&&it->next->free){it->bytes+=sizeof(Block)+it->next->bytes;it->next=it->next->next;}else it=it->next;
}
int usb_lp_dma_coherent(const volatile void *p){uintptr_t a=(uintptr_t)p;return pool&&a>=(uintptr_t)pool&&a<(uintptr_t)pool+USB_POOL_BYTES;}
uintptr_t usb_lp_virt_to_phys(const volatile void *p){
    if(!p)return 0; /* Host descriptors use NULL as a terminated DMA link. */
    uintptr_t a=(uintptr_t)p;if(usb_lp_dma_coherent(p))return pool_physical+a-(uintptr_t)pool;
#ifdef POLLIK_X64
    Mapping map;if(vmm64_lookup(vmm64_kernel(),a,&map)==VM_OK)return (uintptr_t)map.physical+(a&4095);
#else
    uintptr_t physical;if(get_mapping(vmm_get_kernel_directory(),a,&physical))return physical;
#endif
    usb_lp_fatal("USB invalid DMA pointer %p",(const void *)p);
}
void *usb_lp_phys_to_virt(uintptr_t p){
    if(!p)return NULL;if(p>=pool_physical&&p<pool_physical+USB_POOL_BYTES)return pool+p-pool_physical;
    for(unsigned i=0;i<mmio_count;i++)if(p>=mmio[i].physical&&p<mmio[i].physical+mmio[i].bytes)return (void *)(mmio[i].virtual_base+p-mmio[i].physical);
    if(mmio_count==16)return NULL;
    uintptr_t base=p&~(uintptr_t)4095,va=USB_MMIO_VA+(uintptr_t)mmio_count*0x10000u;unsigned mapped=0;
    while(mapped<16){
#ifdef POLLIK_X64
        if(vmm64_map_borrowed(vmm64_kernel(),va+mapped*4096u,base+mapped*4096u,VM_WRITE|VM_DEVICE)!=VM_OK)break;
#else
        if(!map_page(vmm_get_kernel_directory(),va+mapped*4096u,base+mapped*4096u,PAGE_PRESENT|PAGE_RW|PAGE_NOCACHE))break;
#endif
        mapped++;
    }
    if(mapped!=16){while(mapped){mapped--;
#ifdef POLLIK_X64
        vmm64_unmap(vmm64_kernel(),va+mapped*4096u,0,0);
#else
        unmap_page(vmm_get_kernel_directory(),va+mapped*4096u);
#endif
    }return NULL;}
    mmio[mmio_count].physical=base;mmio[mmio_count].virtual_base=va;mmio[mmio_count].bytes=65536;mmio_count++;return (void *)(va+p-base);
}
uint32_t pci_read_config32(pcidev_t d,unsigned r){hal_port_write32(0xcf8,0x80000000u|d|(r&0xfc));return hal_port_read32(0xcfc);}
uint16_t pci_read_config16(pcidev_t d,unsigned r){return (uint16_t)(pci_read_config32(d,r)>>((r&2)*8));}
uint8_t pci_read_config8(pcidev_t d,unsigned r){return (uint8_t)(pci_read_config32(d,r)>>((r&3)*8));}
void pci_write_config32(pcidev_t d,unsigned r,uint32_t v){hal_port_write32(0xcf8,0x80000000u|d|(r&0xfc));hal_port_write32(0xcfc,v);}
void pci_write_config16(pcidev_t d,unsigned r,uint16_t v){hal_port_write32(0xcf8,0x80000000u|d|(r&0xfc));hal_port_write16((unsigned short)(0xcfc+(r&2)),v);}
void pci_write_config8(pcidev_t d,unsigned r,uint8_t v){hal_port_write32(0xcf8,0x80000000u|d|(r&0xfc));hal_port_write8((unsigned short)(0xcfc+(r&3)),v);}
int usb_lp_vprintf(const char *fmt,va_list ap){
    char output[256];unsigned at=0;
    while(*fmt&&at<sizeof(output)-1){
        if(*fmt!='%'){output[at++]=*fmt++;continue;}fmt++;
        unsigned width=0,longs=0;char pad=' ';
        if(*fmt=='0'){pad='0';fmt++;}while(*fmt>='0'&&*fmt<='9'){width=width*10+(unsigned)(*fmt++-'0');if(width>32)width=32;}
        while(*fmt=='l'){longs++;fmt++;}if(*fmt=='z'){longs=sizeof(size_t)==8?2:0;fmt++;}
        char spec=*fmt;if(!spec)break;fmt++;
        if(spec=='s'){const char *s=va_arg(ap,const char *);if(!s)s="(null)";while(*s&&at<sizeof(output)-1)output[at++]=*s++;continue;}
        if(spec=='c'){output[at++]=(char)va_arg(ap,int);continue;}
        if(spec=='%'){output[at++]='%';continue;}
        if(spec!='x'&&spec!='X'&&spec!='p'&&spec!='u'&&spec!='d'&&spec!='i'){output[at++]='?';continue;}
        unsigned long long v;int negative=0;
        if(spec=='p')v=(uintptr_t)va_arg(ap,void *);
        else if(spec=='d'||spec=='i'){long long n=longs>=2?va_arg(ap,long long):longs?va_arg(ap,long):va_arg(ap,int);negative=n<0;v=negative?0-(unsigned long long)n:(unsigned long long)n;}
        else v=longs>=2?va_arg(ap,unsigned long long):longs?va_arg(ap,unsigned long):va_arg(ap,unsigned);
        char digits[24];unsigned n=0,base=spec=='x'||spec=='X'||spec=='p'?16:10;
        do{unsigned digit=(unsigned)(v%base);digits[n++]="0123456789abcdef"[digit];v/=base;}while(v);
        if(negative&&at<sizeof(output)-1)output[at++]='-';
        while(width>n&&at<sizeof(output)-1){output[at++]=pad;width--;}
        while(n&&at<sizeof(output)-1)output[at++]=digits[--n];
    }output[at]=0;serial(output);return (int)at;
}
int usb_lp_printf(const char *fmt,...){va_list ap;va_start(ap,fmt);int n=usb_lp_vprintf(fmt,ap);va_end(ap);return n;}
void usb_lp_fatal(const char *fmt,...){serial("USB fatal: ");va_list ap;va_start(ap,fmt);usb_lp_vprintf(fmt,ap);va_end(ap);serial("\n");hal_cpu_halt_forever();}

/* Acquire firmware ownership before resetting a PCI host controller. */
int usb_platform_handoff(pcidev_t device,unsigned kind){
    if(kind==0){pci_write_config16(device,0xc0,0x2000);return 1;}
    if(kind!=0x10&&kind!=0x20&&kind!=0x30)return 1;
    uint64_t physical=pci_read_config32(device,0x10)&0xfffffff0u;
    if((pci_read_config32(device,0x10)&6)==4)physical|=(uint64_t)pci_read_config32(device,0x14)<<32;
    if(!physical||physical>UINTPTR_MAX)return 0;
    volatile u8 *bar=usb_lp_phys_to_virt((uintptr_t)physical);if(!bar)return 0;
    if(kind==0x10){
        if(readl(bar+4)&0x100){writel(8,bar+8);
            for(unsigned wait=0;wait<1000&&(readl(bar+4)&0x100);wait++)usb_lp_delay(1000);
            if(readl(bar+4)&0x100)return 0;
        }
        writel(0xffffffff,bar+0x14);return 1;
    }
    if(kind==0x20){
        unsigned offset=(readl(bar+8)>>8)&255;
        for(unsigned steps=0;offset&&steps<48;steps++){
            if(offset<0x40||offset>0xf8||(offset&3))return 0;
            u32 cap=pci_read_config32(device,offset);
            if((cap&255)==1){pci_write_config32(device,offset,cap|0x1000000);
                for(unsigned wait=0;wait<1000&&(pci_read_config32(device,offset)&0x10000);wait++)usb_lp_delay(1000);
                if(pci_read_config32(device,offset)&0x10000)return 0;
                pci_write_config32(device,offset+4,0xffff0000);return 1;}
            unsigned next=(cap>>8)&255;if(next==offset)return 0;offset=next;
        }return offset==0;
    }
    unsigned offset=(readl(bar+0x10)>>16)*4;
    for(unsigned steps=0;offset&&steps<256;steps++){
        if(offset>65528)return 0;volatile u32 *cap=(volatile u32 *)(bar+offset);u32 value=readl(cap);
        if((value&255)==1){writel(value|0x1000000,cap);
            for(unsigned wait=0;wait<1000&&(readl(cap)&0x10000);wait++)usb_lp_delay(1000);
            if(readl(cap)&0x10000)return 0;writel(0xe0000000,cap+1);return 1;}
        unsigned next=(value>>8)&255;if(!next)return 1;offset+=next*4;
    }return offset==0;
}
