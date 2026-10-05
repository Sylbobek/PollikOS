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

static gfx_u32 load_le_pixel(const gfx_u8 *p) {
    return (gfx_u32)p[0] | ((gfx_u32)p[1] << 8) |
           ((gfx_u32)p[2] << 16) | ((gfx_u32)p[3] << 24);
}

static void store_le_pixel(gfx_u8 *p, gfx_u32 value) {
    p[0] = (gfx_u8)value;
    p[1] = (gfx_u8)(value >> 8);
    p[2] = (gfx_u8)(value >> 16);
    p[3] = (gfx_u8)(value >> 24);
}

static gfx_u32 blend_pixel_active(gfx_u32 dst, gfx_u32 src) {
#if defined(GFX_REFERENCE)
    return blend_premul_pixel_reference(dst, src);
#else
    return blend_premul_pixel(dst, src);
#endif
}

void gfx_fill_rect(gfx_u8 *dst, gfx_size_t dst_pitch_bytes,
                   gfx_size_t x, gfx_size_t y, gfx_size_t width,
                   gfx_size_t height, gfx_u32 color) {
    while (height--) {
        gfx_u8 *row = dst + y * dst_pitch_bytes + x * 4u;
        if (((gfx_size_t)row & 3u) == 0) {
#if !defined(GFX_REFERENCE) && (defined(__i386__) || defined(__x86_64__))
            gfx_u32 *words = (gfx_u32 *)row;
            gfx_size_t count = width;
            __asm__ volatile("cld; rep stosl"
                             : "+D"(words), "+c"(count)
                             : "a"(color)
                             : "cc", "memory");
#else
            gfx_u32 *words = (gfx_u32 *)row;
            for (gfx_size_t i = 0; i < width; ++i) words[i] = color;
#endif
        } else {
            for (gfx_size_t i = 0; i < width; ++i)
                store_le_pixel(row + i * 4u, color);
        }
        ++y;
    }
}

void gfx_blit_rect(gfx_u8 *dst, gfx_size_t dst_pitch_bytes,
                   gfx_size_t dst_x, gfx_size_t dst_y,
                   const gfx_u8 *src, gfx_size_t src_pitch_bytes,
                   gfx_size_t src_x, gfx_size_t src_y,
                   gfx_size_t width, gfx_size_t height) {
    gfx_size_t dst_offset = dst_y * dst_pitch_bytes + dst_x * 4u;
    gfx_size_t src_offset = src_y * src_pitch_bytes + src_x * 4u;
    int reverse_rows = 0;

    if (!width || !height) return;
    if (dst_pitch_bytes == src_pitch_bytes &&
        (gfx_size_t)(dst + dst_offset) > (gfx_size_t)(src + src_offset))
        reverse_rows = 1;

    for (gfx_size_t n = 0; n < height; ++n) {
        gfx_size_t y = reverse_rows ? height - 1u - n : n;
        gfx_u8 *d = dst + dst_offset + y * dst_pitch_bytes;
        const gfx_u8 *s = src + src_offset + y * src_pitch_bytes;
        gfx_size_t bytes = width * 4u;
        gfx_size_t da = (gfx_size_t)d, sa = (gfx_size_t)s;
        int disjoint = da <= sa ? sa - da >= bytes : da - sa >= bytes;
        if (((da | sa) & 3u) == 0 && disjoint) {
            gfx_blit_row((gfx_u32 *)d, (const gfx_u32 *)s, width);
        } else if (da > sa) {
            while (bytes) { --bytes; d[bytes] = s[bytes]; }
        } else {
            for (gfx_size_t i = 0; i < bytes; ++i) d[i] = s[i];
        }
    }
}

void gfx_fill_rect_alpha(gfx_u8 *dst, gfx_size_t dst_pitch_bytes,
                         gfx_size_t x, gfx_size_t y, gfx_size_t width,
                         gfx_size_t height, gfx_u32 rgb, gfx_u32 alpha) {
    gfx_u32 a = alpha > 255u ? 255u : alpha;
    gfx_u32 src = (a << 24) |
        (((((rgb >> 16) & 255u) * a + 127u) / 255u) << 16) |
        (((((rgb >> 8) & 255u) * a + 127u) / 255u) << 8) |
        (((rgb & 255u) * a + 127u) / 255u);
    while (height--) {
        gfx_u8 *row = dst + y * dst_pitch_bytes + x * 4u;
        for (gfx_size_t i = 0; i < width; ++i) {
            gfx_u8 *p = row + i * 4u;
            store_le_pixel(p, blend_pixel_active(load_le_pixel(p), src));
        }
        ++y;
    }
}

void gfx_rounded_rect_mask(gfx_u8 *mask, gfx_size_t mask_pitch_bytes,
                           gfx_size_t width, gfx_size_t height,
                           gfx_size_t radius) {
    if (radius > width / 2u) radius = width / 2u;
    if (radius > height / 2u) radius = height / 2u;
    for (gfx_size_t y = 0; y < height; ++y) {
        gfx_u8 *row = mask + y * mask_pitch_bytes;
        for (gfx_size_t x = 0; x < width; ++x) row[x] = 255u;
    }
    if (!radius) return;

    gfx_size_t radius16 = radius * 16u;
    gfx_size_t radius_sq = radius16 * radius16;

    for (gfx_size_t y = 0; y < radius; ++y)
        for (gfx_size_t x = 0; x < radius; ++x) {
            gfx_u32 covered = 0;
            for (gfx_u32 sy = 0; sy < 8u; ++sy) {
                gfx_size_t qy = y * 16u + sy * 2u + 1u;
                gfx_size_t dy = radius16 - qy;
                for (gfx_u32 sx = 0; sx < 8u; ++sx) {
                    gfx_size_t qx = x * 16u + sx * 2u + 1u;
                    gfx_size_t dx = radius16 - qx;
                    if (dx * dx + dy * dy <= radius_sq) ++covered;
                }
            }
            gfx_u8 coverage = (gfx_u8)((covered * 255u + 32u) / 64u);
            mask[y * mask_pitch_bytes + x] = coverage;
            mask[y * mask_pitch_bytes + width - 1u - x] = coverage;
            mask[(height - 1u - y) * mask_pitch_bytes + x] = coverage;
            mask[(height - 1u - y) * mask_pitch_bytes + width - 1u - x] = coverage;
        }
}

void gfx_fill_rect_masked(gfx_u8 *dst, gfx_size_t dst_pitch_bytes,
                          gfx_size_t x, gfx_size_t y, gfx_size_t width,
                          gfx_size_t height, gfx_u32 rgb, gfx_u32 alpha,
                          const gfx_u8 *mask, gfx_size_t mask_pitch_bytes) {
    gfx_u32 base_alpha = alpha > 255u ? 255u : alpha;
    for (gfx_size_t ry = 0; ry < height; ++ry) {
        gfx_u8 *drow = dst + (y + ry) * dst_pitch_bytes + x * 4u;
        const gfx_u8 *mrow = mask + ry * mask_pitch_bytes;
        for (gfx_size_t rx = 0; rx < width; ++rx) {
            gfx_u32 a = (base_alpha * mrow[rx] + 127u) / 255u;
            gfx_u32 src = (a << 24) |
                (((((rgb >> 16) & 255u) * a + 127u) / 255u) << 16) |
                (((((rgb >> 8) & 255u) * a + 127u) / 255u) << 8) |
                (((rgb & 255u) * a + 127u) / 255u);
            gfx_u8 *p = drow + rx * 4u;
            store_le_pixel(p, blend_pixel_active(load_le_pixel(p), src));
        }
    }
}

void gfx_blend_premul_rect(gfx_u8 *dst, gfx_size_t dst_pitch_bytes,
                           gfx_size_t dst_x, gfx_size_t dst_y,
                           const gfx_u8 *src, gfx_size_t src_pitch_bytes,
                           gfx_size_t src_x, gfx_size_t src_y,
                           gfx_size_t width, gfx_size_t height) {
    for (gfx_size_t y = 0; y < height; ++y) {
        gfx_u8 *d = dst + (dst_y + y) * dst_pitch_bytes + dst_x * 4u;
        const gfx_u8 *s = src + (src_y + y) * src_pitch_bytes + src_x * 4u;
        for (gfx_size_t x = 0; x < width; ++x) {
            gfx_u8 *dp = d + x * 4u;
            const gfx_u8 *sp = s + x * 4u;
            store_le_pixel(dp, blend_pixel_active(load_le_pixel(dp), load_le_pixel(sp)));
        }
    }
}
