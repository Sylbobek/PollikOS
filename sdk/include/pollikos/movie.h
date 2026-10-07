#ifndef POLLIKOS_MOVIE_H
#define POLLIKOS_MOVIE_H
#include <stddef.h>
#include <stdint.h>
typedef struct pollikos_movie pollikos_movie;
/* Unfragmented MP4, one H264 Baseline/no-B video and optional AAC-LC mono/
 * stereo. Entire encoded input <=16 MiB; dimensions <=640x480. Software
 * decoders run in userspace. Returned video buffers are borrowed until next. */
pollikos_movie *pollikos_movie_open(const void *data,size_t bytes);
pollikos_movie *pollikos_movie_open_file(const char *path);
void pollikos_movie_close(pollikos_movie *movie);
unsigned pollikos_movie_width(const pollikos_movie *movie);
unsigned pollikos_movie_height(const pollikos_movie *movie);
unsigned pollikos_movie_audio_rate(const pollikos_movie *movie);
unsigned pollikos_movie_video_count(const pollikos_movie *movie);
int pollikos_movie_error(const pollikos_movie *movie);
int pollikos_movie_next_video(pollikos_movie *movie,uint64_t *milliseconds);
const uint32_t *pollikos_movie_rgb(const pollikos_movie *movie);
const unsigned char *pollikos_movie_yuv(const pollikos_movie *movie);
size_t pollikos_movie_read_audio(pollikos_movie *movie,int16_t *stereo,size_t frames);
#endif
