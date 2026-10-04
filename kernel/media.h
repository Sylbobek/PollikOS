#ifndef POLLIK_MEDIA_H
#define POLLIK_MEDIA_H
#include "system.h"

/* Decode a still image (PNG/JPEG/BMP/GIF/TGA/PSD/PNM) to RGBA8.
 * Returns an owned w*h*4 buffer, or 0. Free with media_free(). */
u8 *media_decode(const u8 *data, u32 len, int *w, int *h);
void media_free(void *p);

/* Animated GIF playback (own LZW decoder; stb only gives the first frame). */
typedef struct MediaGif MediaGif;
MediaGif *media_gif_open(const u8 *data, u32 len);
void media_gif_close(MediaGif *g);
int media_gif_width(MediaGif *g);
int media_gif_height(MediaGif *g);
int media_gif_animating(MediaGif *g);
/* Composited RGBA canvas (width*height*4). When animate is non-zero it advances
 * once the current frame delay has elapsed. Call media_gif_rewind to restart. */
const u8 *media_gif_canvas(MediaGif *g, int animate);
void media_gif_rewind(MediaGif *g);
/* Non-zero once the current frame's delay elapsed (poll for animation). */
int media_gif_due(MediaGif *g);

/* Simple MJPEG-style clip ("Pollik Video" .pkv): header + JPEG frames.
 * A real, if minimal, video player: each frame is independently decodable. */
typedef struct MediaClip MediaClip;
MediaClip *media_clip_open(const u8 *data, u32 len);
void media_clip_close(MediaClip *c);
int media_clip_width(MediaClip *c);
int media_clip_height(MediaClip *c);
int media_clip_animating(MediaClip *c);
const u8 *media_clip_canvas(MediaClip *c, int animate);
int media_clip_due(MediaClip *c);
void media_clip_rewind(MediaClip *c);

#endif
