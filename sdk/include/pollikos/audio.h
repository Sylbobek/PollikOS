#ifndef POLLIKOS_AUDIO_H
#define POLLIKOS_AUDIO_H
#include <stdint.h>
#include <pollikos/syscall.h>
/* One owner, asynchronous DMA; fixed S16LE stereo 48000 Hz. A submit returns
 * its frame count or EAGAIN while the bounded hardware queue is full. */
static inline int64_t pollikos_audio_begin(void){return __pollikos_syscall2(USER_AUDIO_STREAM,USER_AUDIO_BEGIN,0);}
static inline int64_t pollikos_audio_submit(const int16_t *stereo,unsigned frames){return __pollikos_syscall3(USER_AUDIO_STREAM,USER_AUDIO_SUBMIT,(uint64_t)(uintptr_t)stereo,frames);}
static inline int64_t pollikos_audio_pending(void){return __pollikos_syscall1(USER_AUDIO_STREAM,USER_AUDIO_PENDING);}
static inline int64_t pollikos_audio_stop(void){return __pollikos_syscall1(USER_AUDIO_STREAM,USER_AUDIO_STOP);}
static inline int64_t pollikos_audio_pause(int paused){return __pollikos_syscall2(USER_AUDIO_STREAM,USER_AUDIO_PAUSE,(uint64_t)paused);}
#endif
