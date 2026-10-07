#include "audio_stream.h"
#include "audio_platform.h"
#include "../../audio.h"
#include "user_abi.h"
enum { DEPTH=16,DESCRIPTORS=32 };
typedef struct __attribute__((packed)){uint32_t physical;uint16_t samples,flags;} Descriptor;
static Descriptor *bdl;
static int16_t *pcm[DEPTH];
static uintptr_t bdl_physical,pcm_physical[DEPTH];
static uint64_t owner,produced,consumed;
static uint16_t frame_counts[DESCRIPTORS];
static unsigned queued_frames,port,started,failed,base_slot,paused;
static page_count_t dma_baseline;
int audio64_stream_active(void){return owner!=0;}
static int release(void){
 int ok=!port||audio_quiesce_dma(); /* DMA is stopped before unmapping. */
 for(unsigned i=0;i<DEPTH;i++){audio_stream_dma_free(pcm[i],pcm_physical[i]);pcm[i]=0;pcm_physical[i]=0;}
 audio_stream_dma_free(bdl,bdl_physical);bdl=0;bdl_physical=0;
 if(dma_baseline){
  memory_log("[AUDIO64] stream PMM before=");memory_hex(dma_baseline);
  memory_log(" after=");memory_hex(pmm64_stats().free);memory_log("\n");dma_baseline=0;
 }
 owner=produced=consumed=0;queued_frames=port=started=failed=base_slot=paused=0;
 return ok;
}
void audio64_stream_cleanup(uint64_t pid){if(owner==pid)release();}
void audio64_stream_poll(void){
 if(!owner||!started||produced==consumed||failed||paused)return;
 uint16_t status=inw((u16)(port+0x16));
 if(status&0x10){failed=1;outb((u16)(port+0x1b),0);return;}
 unsigned current=inb((u16)(port+0x14))&31;
 /* CIV advances on completed descriptors; while halted at LVI, CIV remains
  * on the final completed descriptor. Account that last buffer separately. */
 while(consumed<produced&&(unsigned)(consumed&31)!=current){
  queued_frames-=frame_counts[consumed&31];consumed++;
 }
 if((status&3)==3&&consumed<produced&&consumed+1==produced){
  queued_frames-=frame_counts[consumed&31];consumed++;started=0;
 }
 outw((u16)(port+0x16),0x1c);
}
static int64_t begin(uint64_t pid,uint64_t flags){
 if(flags)return -USER_EINVAL;if(owner)return -USER_EAGAIN;
 if(!audio_is_available()||audio_output()!=1)return -USER_ENOTSUP;
 dma_baseline=pmm64_stats().free;
 bdl=audio_stream_dma_alloc(&bdl_physical);
 for(unsigned i=0;bdl&&i<DEPTH;i++){
  pcm[i]=audio_stream_dma_alloc(&pcm_physical[i]);if(!pcm[i]){release();return -USER_ENOMEM;}
 }
 if(!bdl){release();return -USER_ENOMEM;}
 if(!audio_reset_dma()){release();return -USER_EIO;}
 port=audio_bus_master_port();memset(bdl,0,4096);
 for(unsigned i=0;i<DESCRIPTORS;i++)bdl[i].physical=(uint32_t)pcm_physical[i%DEPTH];
 outl((u16)(port+0x10),(uint32_t)bdl_physical);outw((u16)(port+0x16),0x1c);
 owner=pid;return 0;
}
static int pause_stream(unsigned value){
 if(value==paused)return 1;
 if(value){
  outb((u16)(port+0x1b),0);paused=1;
  unsigned current=inb((u16)(port+0x14))&31;
  while(consumed<produced&&(unsigned)(consumed&31)!=current){queued_frames-=frame_counts[consumed&31];consumed++;}
  if(consumed<produced){
   unsigned words=inw((u16)(port+0x18));unsigned index=(unsigned)(consumed&31);
   if(words>bdl[index].samples||words&1){failed=1;return 0;}
   queued_frames-=frame_counts[index]-words/2;
   bdl[index].physical+=(bdl[index].samples-words)*2;bdl[index].samples=(uint16_t)words;
   frame_counts[index]=(uint16_t)(words/2);
   if(!words)consumed++;
  }
  return 1;
 }
 /* Restart from the exact unplayed portion. QEMU's CR write fetches PIV,
  * so simply writing Run again would skip the paused current descriptor. */
 Descriptor saved[DEPTH];uint16_t counts[DEPTH];unsigned pending=(unsigned)(produced-consumed);
 for(unsigned i=0;i<pending;i++){saved[i]=bdl[(consumed+i)&31];counts[i]=frame_counts[(consumed+i)&31];}
 base_slot=(base_slot+(unsigned)(consumed%DEPTH))%DEPTH;
 if(!audio_reset_dma()){failed=1;return 0;}
 for(unsigned i=0;i<DESCRIPTORS;i++)bdl[i].physical=(uint32_t)pcm_physical[(base_slot+i)%DEPTH];
 for(unsigned i=0;i<pending;i++){bdl[i]=saved[i];frame_counts[i]=counts[i];}
 produced=pending;consumed=0;paused=0;started=pending!=0;
 outl((u16)(port+0x10),(uint32_t)bdl_physical);outw((u16)(port+0x16),0x1c);
 if(pending){outb((u16)(port+0x15),(uint8_t)(pending-1));outb((u16)(port+0x1b),1);}
 return 1;
}
int64_t audio64_stream_control(Process64 *process,uint64_t op,uint64_t pointer,uint64_t frames){
 if(op==USER_AUDIO_BEGIN)return begin(process->pid,pointer);
 if(op!=USER_AUDIO_SUBMIT&&op!=USER_AUDIO_PENDING&&op!=USER_AUDIO_STOP&&op!=USER_AUDIO_PAUSE)return -USER_EINVAL;
 if(owner!=process->pid)return -USER_EBADF;
 if(op==USER_AUDIO_STOP)return release()?0:-USER_EIO;
 audio64_stream_poll();if(failed)return -USER_EIO;
 if(op==USER_AUDIO_PAUSE){
  if(pointer>1)return -USER_EINVAL;return pause_stream((unsigned)pointer)?0:-USER_EIO;
 }
 if(op==USER_AUDIO_PENDING)return queued_frames;
 if(!frames||frames>USER_AUDIO_MAX_FRAMES)return -USER_EINVAL;
 if(produced-consumed==DEPTH)return -USER_EAGAIN;
 unsigned index=(unsigned)(produced&31),slot=(base_slot+(unsigned)(produced%DEPTH))%DEPTH;
 if(copy_from_user64(&process->space,pcm[slot],pointer,(size_t)frames*4)!=USER_COPY_OK)return -USER_EFAULT;
 bdl[index].physical=(uint32_t)pcm_physical[slot];
 bdl[index].samples=(uint16_t)(frames*2);bdl[index].flags=0;
 frame_counts[index]=(uint16_t)frames;queued_frames+=(unsigned)frames;produced++;
 __asm__ volatile("" ::: "memory");
 /* A drained DMA channel must be reset: after LVI completion, CIV points at
  * the completed entry, not at the next producer index. Reset only with no
  * live buffer, never while a descriptor is being played. */
 if(!started&&produced-consumed==1){
  if(!audio_reset_dma()){failed=1;return -USER_EIO;}
  /* Rebase the first buffer to entry zero after an underrun. */
  if(index){bdl[0]=bdl[index];frame_counts[0]=frame_counts[index];}
  produced=1;consumed=0;
  base_slot=slot;
  /* Rotate the repeated physical-page ring to match this first buffer. */
  for(unsigned i=1;i<DESCRIPTORS;i++)bdl[i].physical=(uint32_t)pcm_physical[(slot+i)%DEPTH];
  outl((u16)(port+0x10),(uint32_t)bdl_physical);outw((u16)(port+0x16),0x1c);index=0;
 }
 outb((u16)(port+0x15),(uint8_t)index);
 if(!started&&!paused)outb((u16)(port+0x1b),1);
 started=1;
 return (int64_t)frames;
}
