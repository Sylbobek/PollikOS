#ifndef POLLIK_AUDIO_H
#define POLLIK_AUDIO_H

#include "system.h"

typedef enum {
    SOUND_STARTUP = 1,
    SOUND_CLICK,
    SOUND_ALERT,
    SOUND_TRASH
} SoundEffect;

int audio_init(void);
int audio_is_available(void);
void audio_play_tone(u32 freq_hz, u32 duration_ms);
void audio_play_sound(SoundEffect s);
void audio_set_volume(u8 vol_0_to_100);
u8 audio_get_volume(void);
int audio_play_wav(const u8 *data, u32 len);
int audio_play_wav_file(const char *path);

#endif