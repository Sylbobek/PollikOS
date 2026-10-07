#include <stdio.h>
#include <string.h>
#define POLLIK_X64 1
#include "../kernel/audio.c"
static int allocated,freed,calls,fail_second=1,unknown,stuck,writes;
static unsigned char dma[2][4096];
void *audio_dma_alloc(uintptr_t *phys){int n=calls++;if(fail_second&&n==1)return 0;allocated++;*phys=0x200000+n*4096;return dma[n%2];}
void audio_dma_free(void *v,uintptr_t p){(void)p;if(v)freed++;}
int pci_scan_bus(PciDevice *out,int n){(void)n;*out=(PciDevice){0,2,0,unknown?0x1234:0x8086,unknown?0x5678:0x2415,4,1,0};return 1;}
u32 pci_config_read32(u8 b,u8 d,u8 f,u8 r){(void)b;(void)d;(void)f;return r==16?0x1001:r==20?0x2001:0;}
void pci_config_write32(u8 b,u8 d,u8 f,u8 r,u32 v){(void)b;(void)d;(void)f;(void)r;(void)v;}
void memory_log(const char *s){(void)s;}
int sound_is_muted(void){return 0;}void speaker_beep(u32 f,u32 d){(void)f;(void)d;}
void hal_port_write8(unsigned short p,unsigned char v){(void)p;(void)v;writes++;}
void hal_port_write16(unsigned short p,unsigned short v){(void)p;(void)v;writes++;}
void hal_port_write32(unsigned short p,unsigned int v){(void)p;(void)v;writes++;}
unsigned char hal_port_read8(unsigned short p){(void)p;return stuck?2:0;}
unsigned short hal_port_read16(unsigned short p){(void)p;return 2;}
unsigned int hal_port_read32(unsigned short p){(void)p;return 0;}
int main(void){
 if(audio_init()||allocated!=freed){printf("FAIL DMA rollback allocated=%d freed=%d\n",allocated,freed);return 1;}
 fail_second=0;calls=0;unknown=1;
 if(audio_init()){puts("FAIL unsupported audio class accepted as AC97");return 2;}
 unknown=0;calls=0;if(!audio_init())return 3;
 /* Must not divide by zero for a one-sample tone period. */
 audio_play_tone(880,20);
 if(s_bdl[0].samples!=1920){printf("FAIL native stereo sample count=%u expected=1920\n",s_bdl[0].samples);return 4;}
 for(unsigned n=0;n<960;n++)if(s_pcm_buf[n*2]!=s_pcm_buf[n*2+1])return 5;
 audio_play_tone(48000,1);stuck=1;audio_play_tone(440,1);
 puts("PASS AC97: DMA rollback exact, unsupported PCI rejected, high-frequency tone, bounded stuck reset");return 0;
}
