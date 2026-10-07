#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define POLLIK_X64 1
#include "../kernel/arch/x86_64/audio_stream.c"
static unsigned char pages[17][4096];static int live,alloc_count,fail_at=-1,reset_ok=1,copy_ok=1;
static unsigned civ,status=1,run_writes,picb;static unsigned char lvi;
PmmStats pmm64_stats(void){return (PmmStats){.free=1000-(unsigned)live};}
void memory_log(const char *s){(void)s;}void memory_hex(uint64_t n){(void)n;}
void *audio_stream_dma_alloc(uintptr_t *physical){if(alloc_count==fail_at)return 0;int n=alloc_count++;live++;*physical=0x100000+n*4096;return pages[n];}
void audio_stream_dma_free(void *p,uintptr_t f){(void)f;if(p)live--;}
int audio_is_available(void){return 1;}int audio_output(void){return 1;}
unsigned audio_bus_master_port(void){return 0x2000;}int audio_reset_dma(void){civ=0;status=1;return reset_ok;}
int audio_quiesce_dma(void){status=1;return 1;}
UserCopyResult copy_from_user64(const AddressSpace *s,void *d,virt_addr_t v,size_t n){(void)s;if(!copy_ok)return USER_COPY_FAULT;memcpy(d,(void *)v,n);return USER_COPY_OK;}
void hal_port_write8(unsigned short p,unsigned char v){if(p==0x2015)lvi=v;if(p==0x201b&&v==1){status=0;run_writes++;}}
void hal_port_write16(unsigned short p,unsigned short v){(void)p;(void)v;}
void hal_port_write32(unsigned short p,unsigned int v){(void)p;(void)v;}
unsigned char hal_port_read8(unsigned short p){return p==0x2014?(unsigned char)civ:0;}
unsigned short hal_port_read16(unsigned short p){return p==0x2016?(unsigned short)status:p==0x2018?(unsigned short)picb:0;}
unsigned int hal_port_read32(unsigned short p){(void)p;return 0;}
#define CHECK(c) do{if(!(c)){printf("FAIL stream line=%d %s live=%d produced=%llu consumed=%llu\n",__LINE__,#c,live,(unsigned long long)produced,(unsigned long long)consumed);return 1;}}while(0)
int main(void){
 Process64 p={.pid=17},other={.pid=18};int16_t sample[2048];
 for(fail_at=0;fail_at<17;fail_at++){
  alloc_count=0;CHECK(audio64_stream_control(&p,USER_AUDIO_BEGIN,0,0)==-USER_ENOMEM);CHECK(live==0&&!owner);
 }
 fail_at=-1;alloc_count=0;reset_ok=0;CHECK(audio64_stream_control(&p,USER_AUDIO_BEGIN,0,0)==-USER_EIO);CHECK(!live&&!owner);
 reset_ok=1;alloc_count=0;CHECK(!audio64_stream_control(&p,USER_AUDIO_BEGIN,0,0));CHECK(live==17);
 CHECK(audio64_stream_control(&other,USER_AUDIO_STOP,0,0)==-USER_EBADF);
 CHECK(audio64_stream_control(&p,USER_AUDIO_SUBMIT,(uintptr_t)sample,1025)==-USER_EINVAL);
 copy_ok=0;CHECK(audio64_stream_control(&p,USER_AUDIO_SUBMIT,(uintptr_t)sample,32)==-USER_EFAULT);CHECK(!produced);copy_ok=1;
 /* Fill, backpressure, 96 completions across three BDL wraps. */
 for(unsigned n=0;n<104;n++){
  if(n>=16){civ=(unsigned)((consumed+1)&31);audio64_stream_poll();}
  for(unsigned i=0;i<2048;i++)sample[i]=(int16_t)n;
  CHECK(audio64_stream_control(&p,USER_AUDIO_SUBMIT,(uintptr_t)sample,1024)==1024);
  CHECK(bdl[(produced-1)&31].samples==2048);
  CHECK(run_writes==1); /* Enqueue must not restart a running channel. */
  CHECK(pcm[(base_slot+(unsigned)((produced-1)%16))%16][0]==(int16_t)n);
  if(n==15)CHECK(audio64_stream_control(&p,USER_AUDIO_SUBMIT,(uintptr_t)sample,1)==-USER_EAGAIN);
 }
 civ=lvi;status=3;audio64_stream_poll();CHECK(consumed==produced&&!queued_frames);
 /* Restart from an arbitrary drained physical slot, then append safely. */
 sample[0]=777;CHECK(audio64_stream_control(&p,USER_AUDIO_SUBMIT,(uintptr_t)sample,10)==10);
 sample[0]=778;CHECK(audio64_stream_control(&p,USER_AUDIO_SUBMIT,(uintptr_t)sample,10)==10);
 CHECK(((int16_t *)pages[((bdl[1].physical-0x100000)/4096)])[0]==778);
 uintptr_t original=bdl[0].physical;picb=10;civ=0;
 CHECK(!audio64_stream_control(&p,USER_AUDIO_PAUSE,1,0));CHECK(paused&&queued_frames==15);
 CHECK(bdl[0].physical==original+20&&bdl[0].samples==10);
 civ=7;audio64_stream_poll();CHECK(queued_frames==15);civ=0;
 CHECK(!audio64_stream_control(&p,USER_AUDIO_PAUSE,0,0));CHECK(!paused&&bdl[0].physical==original+20&&bdl[0].samples==10);
 audio64_stream_cleanup(other.pid);CHECK(live==17);
 audio64_stream_cleanup(p.pid);CHECK(!live&&!owner);
 puts("PASS stream: 17 allocation failures, reset failure, ownership/pointers, depth 16, 3 wraps, no duplicate Run, exact partial pause/resume, underrun restart, owner cleanup baseline");return 0;
}
