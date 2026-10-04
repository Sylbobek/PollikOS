#ifndef POLLIK_GRAPHICS_H
#define POLLIK_GRAPHICS_H
#include "system.h"
/* Software rasterizer. The caller owns buffers and selects a valid target;
 * no WM, input, desktop state or application knowledge belongs here. */
typedef struct { int x1, y1, x2, y2; } GraphicsClip;
void graphics_init(int screen_width, int screen_height);
/* 0..64 coverage, x/y measured inward from either exterior corner.
 * Radii 1..29 are LUT-only after initialization (including shadows/dock). */
int graphics_corner_coverage(int radius, int x, int y);
/* Selecting a target resets its scissor to the complete target. Save/restore
 * the caller's clip explicitly around temporary client-cache targets. */
void set_draw_target(u32 *buffer, int width, int height, int stride);
GraphicsClip graphics_get_clip(void);
void graphics_set_clip(GraphicsClip clip);
void rect(int x, int y, int w, int h, u32 color);
void rounded(int x, int y, int w, int h, int r, u32 color, int opacity);
void roundrect_slice(int x, int y, int w, int h, int r, u32 color, int first, int last);
void roundrect(int x, int y, int w, int h, int r, u32 color);
/* Antialiased outline of thickness t that follows the rounded corners, unlike
 * disjoint straight rect() stripes that leave the arcs bare. */
void roundrect_border(int x, int y, int w, int h, int r, int t, u32 color);
/* Full, even rounded stroke of thickness t over a solid fill: paints the ring
 * colour then the interior, so corner pixels never fade out. */
void roundrect_stroke(int x, int y, int w, int h, int r, int t, u32 stroke, u32 fill);
void text(int x, int y, const char *s, u32 color, int scale);
int text_width(const char *s, int scale);
void centered(int x, int y, int w, const char *s, u32 color, int scale);
void sprite(int x, int y, int w, int h, const u8 *indices, const u8 *alpha,
            const u32 *palette, int sw, int sh);
/* Nearest-neighbour scale of an RGBA8 image into the target, honouring clip. */
void graphics_blit_rgba(int dx, int dy, int dw, int dh, const u8 *rgba, int sw, int sh);
static inline u32 blend(u32 a, u32 b, int t) {
    if (t <= 0) return a;
    if (t >= 256) return b;
    int inv = 256 - t;
    int r = (((a >> 16) & 255) * inv + ((b >> 16) & 255) * t) >> 8;
    int g = (((a >> 8) & 255) * inv + ((b >> 8) & 255) * t) >> 8;
    int z = ((a & 255) * inv + (b & 255) * t) >> 8;
    return (u32)(r << 16 | g << 8 | z);
}
#endif
