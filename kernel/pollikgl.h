#ifndef POLLIK_POLLIKGL_H
#define POLLIK_POLLIKGL_H
#include "system.h"
/* PollikGL: a small, own software graphics API (the CPU-only analogue of a
 * DirectX/Vulkan-style draw layer). Immediate-mode 2D/3D-ish primitives over a
 * caller-owned pixel buffer, with a scissor clip. No GPU is involved. */
typedef struct {
    u32 *pixels;
    int width, height, stride;
    int clip_x0, clip_y0, clip_x1, clip_y1;
} Pgl;

void pgl_begin(Pgl *g, u32 *pixels, int width, int height, int stride);
void pgl_clip(Pgl *g, int x0, int y0, int x1, int y1);
void pgl_clear(Pgl *g, u32 color);
void pgl_fill_rect(Pgl *g, int x, int y, int w, int h, u32 color);
void pgl_stroke_rect(Pgl *g, int x, int y, int w, int h, u32 color);
void pgl_line(Pgl *g, int x0, int y0, int x1, int y1, u32 color);
void pgl_fill_triangle(Pgl *g, int x0, int y0, int x1, int y1, int x2, int y2, u32 color);
void pgl_fill_circle(Pgl *g, int cx, int cy, int r, u32 color);
void pgl_stroke_circle(Pgl *g, int cx, int cy, int r, u32 color);
/* RGBA8 source scaled to w*h at (x,y), nearest neighbour, honouring clip. */
void pgl_blit(Pgl *g, int x, int y, int w, int h, const u8 *rgba, int sw, int sh);

#endif
