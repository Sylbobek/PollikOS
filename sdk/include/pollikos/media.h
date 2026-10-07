#ifndef POLLIKOS_MEDIA_H
#define POLLIKOS_MEDIA_H
#include <stddef.h>
#include <stdint.h>
typedef struct pollikos_audio_decoder pollikos_audio_decoder;
/* Decode in userspace. Memory input is borrowed until close; open_file owns
 * its encoded buffer. Output is signed 16-bit stereo PCM at 48000 Hz.
 * Supported: RIFF/WAVE PCM 8/16/24/32 and IEEE-float32, mono/stereo; MP3.
 * MP3 encoder padding is retained; gapless metadata is not interpreted. */
pollikos_audio_decoder *pollikos_media_open(const void *encoded,size_t bytes);
pollikos_audio_decoder *pollikos_media_open_file(const char *path);
size_t pollikos_media_read(pollikos_audio_decoder *decoder,int16_t *stereo,size_t frames);
void pollikos_media_close(pollikos_audio_decoder *decoder);
unsigned pollikos_media_source_rate(const pollikos_audio_decoder *decoder);
const char *pollikos_media_format(const pollikos_audio_decoder *decoder);
#endif
