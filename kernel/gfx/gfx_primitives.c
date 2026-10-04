#include "gfx_primitives.h"

void gfx_fill_span(gfx_u32 *dst, gfx_size_t pixels, gfx_u32 color) {
#if !defined(GFX_REFERENCE) && (defined(__i386__) || defined(__x86_64__))
    __asm__ volatile("cld; rep stosl"
                     : "+D"(dst), "+c"(pixels)
                     : "a"(color)
                     : "cc", "memory");
#else
    while (pixels--) *dst++ = color;
#endif
}

void gfx_blit_row(gfx_u32 *dst, const gfx_u32 *src, gfx_size_t pixels) {
#if !defined(GFX_REFERENCE) && defined(__x86_64__)
    gfx_size_t qwords = pixels >> 1;
    __asm__ volatile("cld; rep movsq"
                     : "+D"(dst), "+S"(src), "+c"(qwords)
                     :
                     : "cc", "memory");
    if (pixels & 1u) *dst = *src;
#elif !defined(GFX_REFERENCE) && defined(__i386__)
    __asm__ volatile("cld; rep movsl"
                     : "+D"(dst), "+S"(src), "+c"(pixels)
                     :
                     : "cc", "memory");
#else
    while (pixels--) *dst++ = *src++;
#endif
}

#if !defined(GFX_REFERENCE)
static inline gfx_u32 div255_round(gfx_u32 value) {
    value += 127u;
    return (value + 1u + (value >> 8)) >> 8;
}

static inline gfx_u32 blend_premul_pixel(gfx_u32 dst, gfx_u32 src) {
    gfx_u32 alpha = src >> 24;
    gfx_u32 inverse = 255u - alpha;
    gfx_u32 rb = (dst & 0x00ff00ffu) * inverse + 0x007f007fu;
    rb += 0x00010001u + ((rb >> 8) & 0x00ff00ffu);
    rb = (rb >> 8) & 0x00ff00ffu;
    gfx_u32 g = div255_round(((dst >> 8) & 255u) * inverse);
    g = ((src >> 8) & 255u) + g;
    rb += src & 0x00ff00ffu;
    return (rb & 0x00ff00ffu) | ((g & 255u) << 8);
}
#else
static inline gfx_u32 blend_premul_pixel_reference(gfx_u32 dst, gfx_u32 src) {
    gfx_u32 alpha = src >> 24;
    gfx_u32 inverse = 255u - alpha;
    gfx_u32 r = (src >> 16) & 255u;
    gfx_u32 g = (src >> 8) & 255u;
    gfx_u32 b = src & 255u;
    r += (((dst >> 16) & 255u) * inverse + 127u) / 255u;
    g += (((dst >> 8) & 255u) * inverse + 127u) / 255u;
    b += ((dst & 255u) * inverse + 127u) / 255u;
    return (r << 16) | (g << 8) | b;
}
#endif

void gfx_blend_premul_span(gfx_u32 *dst, const gfx_u32 *src, gfx_size_t pixels) {
#if defined(GFX_REFERENCE)
    while (pixels--) {
        *dst = blend_premul_pixel_reference(*dst, *src);
        ++dst;
        ++src;
    }
#else
    while (pixels--) {
        *dst = blend_premul_pixel(*dst, *src);
        ++dst;
        ++src;
    }
#endif
}
