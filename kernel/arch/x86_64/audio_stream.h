#ifndef POLLIK_AUDIO_STREAM64_H
#define POLLIK_AUDIO_STREAM64_H
#include "user.h"
int64_t audio64_stream_control(Process64 *process,uint64_t op,uint64_t pointer,uint64_t frames);
void audio64_stream_poll(void);
void audio64_stream_cleanup(uint64_t pid);
int audio64_stream_active(void);
#endif
