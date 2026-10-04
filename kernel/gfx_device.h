#ifndef POLLIK_GFX_DEVICE_H
#define POLLIK_GFX_DEVICE_H
#include "system.h"
/* Caller-owned RGB888-in-u32 memory, stride/capacity in pixels; no GPU handles.
 * Bounded targets keep raster integer arithmetic and address offsets safe. */
typedef struct { u32 *color; float *depth; int width, height, stride; u32 capacity; } GfxTarget;
typedef struct { int width, height, bpp; u32 capabilities; } GfxInfo;
enum { GFX_CLEAR = 1, GFX_TRIANGLE = 2, GFX_DEPTH = 4, GFX_COLOR = 8, GFX_TEXTURE = 16 };
int gfx_target_valid(const GfxTarget *t);
void gfx_device_info(GfxInfo *out);
u32 gfx_clear(GfxTarget *t, u32 offset, u32 count, u32 color);
u32 gfx_rect(GfxTarget *t, int x, int y, int w, int h, u32 color);
/* Bounded microbenchmark primitives (w,h <=64): rect, radius-3 roundrect,
 * diagonal line, fixed 5x7 "PM" text, half-alpha rect. Clips to this target
 * only; never changes the compositor target/clip. Returns pixel writes. */
u32 gfx_shape(GfxTarget *t, int x, int y, int w, int h, u32 color, u32 kind);
/* Saturating quotient, including n < d and zero divisor; no libgcc runtime. */
u32 gfx_ratio(u64 n, u32 d);
#endif
