#ifndef POLLIK_AUDIO_PLATFORM64_H
#define POLLIK_AUDIO_PLATFORM64_H
#include "fs_platform.h"
static inline void outl(u16 p,u32 v){hal_port_write32(p,v);}
static inline u32 inl(u16 p){return hal_port_read32(p);}
typedef struct {u8 bus,dev,fn;u16 vendor_id,device_id;u8 class_code,subclass,prog_if;} PciDevice;
#define MAX_PCI_DEVICES 32
int pci_scan_bus(PciDevice *out,int capacity);
u32 pci_config_read32(u8 bus,u8 dev,u8 fn,u8 reg);
void pci_config_write32(u8 bus,u8 dev,u8 fn,u8 reg,u32 value);
void *audio_dma_alloc(uintptr_t *physical);
void audio_dma_free(void *mapped,uintptr_t physical);
void *audio_stream_dma_alloc(uintptr_t *physical);
void audio_stream_dma_free(void *mapped,uintptr_t physical);
int sound_is_muted(void);
void speaker_beep(u32 hz,u32 ms);
void audio64_poll(void);
void memory_log(const char *message);
#define KLOG_CAT_BOOT 0
#define KLOG_INFO(cat,msg) do{(void)(cat);memory_log("[AUDIO64] " msg "\n");}while(0)
#define KLOG_WARN KLOG_INFO
#define KLOG_ERROR KLOG_INFO
#endif
