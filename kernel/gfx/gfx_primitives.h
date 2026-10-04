#ifndef POLLIK_GFX_PRIMITIVES_H
#define POLLIK_GFX_PRIMITIVES_H

typedef unsigned int gfx_u32;
typedef __SIZE_TYPE__ gfx_size_t;

/* Pixels are 0xAARRGGBB. Source RGB must already be premultiplied by alpha;
 * destination is opaque 0x00RRGGBB and remains opaque. */
void gfx_fill_span(gfx_u32 *dst, gfx_size_t pixels, gfx_u32 color);
/* Row blit requires non-overlapping source and destination ranges. */
void gfx_blit_row(gfx_u32 *dst, const gfx_u32 *src, gfx_size_t pixels);
void gfx_blend_premul_span(gfx_u32 *dst, const gfx_u32 *src, gfx_size_t pixels);

#endif
