#ifndef POLLIK_GFX_PRIMITIVES_H
#define POLLIK_GFX_PRIMITIVES_H

typedef unsigned int gfx_u32;
typedef __SIZE_TYPE__ gfx_size_t;

/* Pixels are numeric 0xAARRGGBB in memory; the i386 framebuffer writes the
 * low B,G,R bytes to a VBE 32-bpp LFB whose unused high byte is X. Source RGB
 * must already be premultiplied by alpha; destination is opaque 0x00RRGGBB.
 * For each channel C, source-over is out_C = min(255, src_C +
 * floor((dst_C * (255 - src_A) + 127) / 255)); +127 rounds to nearest with
 * ties upward. The result's unused high byte is zero. */
void gfx_fill_span(gfx_u32 *dst, gfx_size_t pixels, gfx_u32 color);
/* Row blit requires non-overlapping source and destination ranges. */
void gfx_blit_row(gfx_u32 *dst, const gfx_u32 *src, gfx_size_t pixels);
void gfx_blend_premul_span(gfx_u32 *dst, const gfx_u32 *src, gfx_size_t pixels);

#endif
