#ifndef POLLIK_GFX_PRIMITIVES_H
#define POLLIK_GFX_PRIMITIVES_H

typedef unsigned int gfx_u32;
typedef unsigned char gfx_u8;
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

/* Rectangle coordinates and dimensions are in pixels; pitches are bytes.
 * The pixel byte order is little-endian 0xAARRGGBB, and base pointers may be
 * unaligned. Callers provide valid storage for every addressed row. */
void gfx_fill_rect(gfx_u8 *dst, gfx_size_t dst_pitch_bytes,
                   gfx_size_t x, gfx_size_t y, gfx_size_t width,
                   gfx_size_t height, gfx_u32 color);
/* Overlap is supported when source and destination use the same pitch, as
 * with a move within one surface. Non-overlapping buffers may use any pitches. */
void gfx_blit_rect(gfx_u8 *dst, gfx_size_t dst_pitch_bytes,
                   gfx_size_t dst_x, gfx_size_t dst_y,
                   const gfx_u8 *src, gfx_size_t src_pitch_bytes,
                   gfx_size_t src_x, gfx_size_t src_y,
                   gfx_size_t width, gfx_size_t height);
/* rgb is straight 0x00RRGGBB and alpha is constant for the whole rectangle. */
void gfx_fill_rect_alpha(gfx_u8 *dst, gfx_size_t dst_pitch_bytes,
                         gfx_size_t x, gfx_size_t y, gfx_size_t width,
                         gfx_size_t height, gfx_u32 rgb, gfx_u32 alpha);
/* Generates 8x8 supersampled coverage (0..255); radius is clamped to fit. */
void gfx_rounded_rect_mask(gfx_u8 *mask, gfx_size_t mask_pitch_bytes,
                           gfx_size_t width, gfx_size_t height,
                           gfx_size_t radius);
/* rgb is straight 0x00RRGGBB; alpha and mask coverage are each 0..255. */
void gfx_fill_rect_masked(gfx_u8 *dst, gfx_size_t dst_pitch_bytes,
                          gfx_size_t x, gfx_size_t y, gfx_size_t width,
                          gfx_size_t height, gfx_u32 rgb, gfx_u32 alpha,
                          const gfx_u8 *mask, gfx_size_t mask_pitch_bytes);
/* Source pixels are premultiplied; source and destination must not overlap. */
void gfx_blend_premul_rect(gfx_u8 *dst, gfx_size_t dst_pitch_bytes,
                           gfx_size_t dst_x, gfx_size_t dst_y,
                           const gfx_u8 *src, gfx_size_t src_pitch_bytes,
                           gfx_size_t src_x, gfx_size_t src_y,
                           gfx_size_t width, gfx_size_t height);

#endif
