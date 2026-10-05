#include "../kernel/gfx/gfx_primitives.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

#define CHECK(x, name) do { if (!(x)) { \
    fprintf(stderr, "FAIL: %s at line %d\n", name, __LINE__); return 1; \
} } while (0)

#define PIXELS (1920u * 1080u)
#define BENCH_ROUNDS 128u
#define BENCH_REPEATS 3u

static uint32_t reference_blend(uint32_t dst, uint32_t src) {
    uint32_t a = src >> 24, inv = 255u - a;
    uint32_t r = ((src >> 16) & 255u) + ((((dst >> 16) & 255u) * inv + 127u) / 255u);
    uint32_t g = ((src >> 8) & 255u) + ((((dst >> 8) & 255u) * inv + 127u) / 255u);
    uint32_t b = (src & 255u) + (((dst & 255u) * inv + 127u) / 255u);
    return ((r & 255u) << 16) | ((g & 255u) << 8) | (b & 255u);
}

static uint32_t load_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void store_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t random_u32(void);
static uint32_t random_state = 0x6d2b79f5u;

static void reference_rounded_mask(uint8_t *mask, size_t pitch, size_t width,
                                   size_t height, size_t radius) {
    if (radius > width / 2u) radius = width / 2u;
    if (radius > height / 2u) radius = height / 2u;
    for (size_t y = 0; y < height; ++y)
        for (size_t x = 0; x < width; ++x) {
            unsigned covered = 0;
            for (unsigned sy = 0; sy < 8; ++sy)
                for (unsigned sx = 0; sx < 8; ++sx) {
                    double px = (double)x + ((double)sx + 0.5) / 8.0;
                    double py = (double)y + ((double)sy + 0.5) / 8.0;
                    double cx = px < radius ? radius : (px > width - radius ? width - radius : px);
                    double cy = py < radius ? radius : (py > height - radius ? height - radius : py);
                    double dx = px - cx, dy = py - cy;
                    if (dx * dx + dy * dy <= (double)radius * radius) ++covered;
                }
            mask[y * pitch + x] = (uint8_t)((covered * 255u + 32u) / 64u);
        }
}

static void reference_blit_rect(uint8_t *dst, size_t pitch, size_t dx, size_t dy,
                                const uint8_t *src, size_t sx, size_t sy,
                                size_t width, size_t height) {
    uint8_t snapshot[37 * 29 * 4];
    for (size_t y = 0; y < height; ++y)
        for (size_t x = 0; x < width; ++x)
            store_le32(snapshot + (y * width + x) * 4,
                       load_le32(src + (sy + y) * pitch + (sx + x) * 4));
    for (size_t y = 0; y < height; ++y)
        for (size_t x = 0; x < width; ++x)
            store_le32(dst + (dy + y) * pitch + (dx + x) * 4,
                       load_le32(snapshot + (y * width + x) * 4));
}

static int rectangle_fuzz(void) {
    enum { W = 37, H = 29, MIN_PITCH = W * 4, MAX_PITCH = MIN_PITCH + 11,
           GUARD = 17, BUFFER_SIZE = H * MAX_PITCH + GUARD * 2 + 4, CASES = 100000 };
    uint8_t *actual = (uint8_t *)malloc(BUFFER_SIZE);
    uint8_t *expected = (uint8_t *)malloc(BUFFER_SIZE);
    uint8_t source[BUFFER_SIZE];
    uint8_t coverage[H * MAX_PITCH];
    CHECK(actual && expected, "rectangle fuzz allocation");
    random_state = 0x6d2b79f5u;

    for (size_t i = 0; i < sizeof(source); ++i) source[i] = (uint8_t)random_u32();

    for (unsigned test = 0; test < CASES; ++test) {
        size_t pitch = MIN_PITCH + random_u32() % (MAX_PITCH - MIN_PITCH + 1u);
        size_t align = random_u32() & 3u;
        size_t x = random_u32() % W, y = random_u32() % H;
        size_t width = random_u32() % (W - x + 1), height = random_u32() % (H - y + 1);
        size_t src_x = random_u32() % (W - width + 1);
        size_t src_y = random_u32() % (H - height + 1);
        uint32_t color = random_u32();
        uint32_t alpha = random_u32() & 255u;
        uint32_t rgb = random_u32() & 0x00ffffffu;

        memset(actual, 0xa5, BUFFER_SIZE);
        memset(expected, 0xa5, BUFFER_SIZE);
        uint8_t *a = actual + GUARD + align;
        uint8_t *e = expected + GUARD + align;
        gfx_fill_rect(a, pitch, x, y, width, height, color);
        for (size_t ry = 0; ry < height; ++ry)
            for (size_t rx = 0; rx < width; ++rx)
                store_le32(e + (y + ry) * pitch + (x + rx) * 4, color);
        CHECK(memcmp(actual, expected, BUFFER_SIZE) == 0,
              "seeded rectangle fill/alignment/guards");

        memcpy(actual, source, BUFFER_SIZE);
        memcpy(expected, source, BUFFER_SIZE);
        a = actual + GUARD + align;
        e = expected + GUARD + align;
        reference_blit_rect(e, pitch, x, y, e, src_x, src_y, width, height);
        gfx_blit_rect(a, pitch, x, y, a, pitch, src_x, src_y, width, height);
        CHECK(memcmp(actual, expected, BUFFER_SIZE) == 0,
              "seeded overlapping rectangle blit/alignment/guards");

        for (size_t ry = 0; ry < height; ++ry)
            for (size_t rx = 0; rx < width; ++rx) {
                uint8_t *p = e + (y + ry) * pitch + (x + rx) * 4;
                store_le32(p, reference_blend(load_le32(p),
                          ((alpha << 24) | ((((rgb >> 16) & 255u) * alpha + 127u) / 255u << 16) |
                           ((((rgb >> 8) & 255u) * alpha + 127u) / 255u << 8) |
                           (((rgb & 255u) * alpha + 127u) / 255u))));
            }
        gfx_fill_rect_alpha(a, pitch, x, y, width, height, rgb, alpha);
        CHECK(memcmp(actual, expected, BUFFER_SIZE) == 0,
              "seeded constant-alpha rectangle/alignment/guards");

        for (size_t ry = 0; ry < height; ++ry)
            for (size_t rx = 0; rx < width; ++rx) {
                uint8_t cov = (uint8_t)random_u32();
                coverage[ry * pitch + rx] = cov;
                uint32_t effective_alpha = (alpha * cov + 127u) / 255u;
                uint32_t src_pixel = (effective_alpha << 24) |
                    (((((rgb >> 16) & 255u) * effective_alpha + 127u) / 255u) << 16) |
                    (((((rgb >> 8) & 255u) * effective_alpha + 127u) / 255u) << 8) |
                    (((rgb & 255u) * effective_alpha + 127u) / 255u);
                uint8_t *p = e + (y + ry) * pitch + (x + rx) * 4;
                store_le32(p, reference_blend(load_le32(p), src_pixel));
            }
        gfx_fill_rect_masked(a, pitch, x, y, width, height, rgb, alpha, coverage, pitch);
        CHECK(memcmp(actual, expected, BUFFER_SIZE) == 0,
              "seeded masked rectangle/alignment/guards");

        /* Source-over rectangle uses a disjoint source surface. */
        memcpy(actual, source, BUFFER_SIZE);
        memcpy(expected, source, BUFFER_SIZE);
        for (size_t ry = 0; ry < height; ++ry)
            for (size_t rx = 0; rx < width; ++rx) {
                uint32_t sa = random_u32() & 255u;
                uint32_t sr = random_u32() % (sa + 1u);
                uint32_t sg = random_u32() % (sa + 1u);
                uint32_t sb = random_u32() % (sa + 1u);
                uint8_t *sp = source + GUARD + ((src_y + ry) * pitch) + (src_x + rx) * 4;
                store_le32(sp, (sa << 24) | (sr << 16) | (sg << 8) | sb);
                uint8_t *dp = e + (y + ry) * pitch + (x + rx) * 4;
                store_le32(dp, reference_blend(load_le32(dp), load_le32(sp)));
            }
        gfx_blend_premul_rect(a, pitch, x, y,
                              source + GUARD, pitch, src_x, src_y, width, height);
        CHECK(memcmp(actual, expected, BUFFER_SIZE) == 0,
              "seeded premultiplied rectangle blend/alignment/guards");
    }
    free(actual);
    free(expected);
    printf("PASS gfx rectangle fuzz: %u cases seed=0x6d2b79f5 pitch=%u..%u align=0..3 overlap=all guards=checked\n",
           CASES, MIN_PITCH, MAX_PITCH);
    return 0;
}

static int rounded_mask_fuzz(void) {
    enum { W = 37, H = 29, PITCH = 42, GUARD = 17, CASES = 10000 };
    uint8_t actual[H * PITCH + GUARD * 2];
    uint8_t expected[H * PITCH + GUARD * 2];
    random_state = 0xc001d00du;
    for (unsigned test = 0; test < CASES; ++test) {
        size_t width = random_u32() % (W + 1u);
        size_t height = random_u32() % (H + 1u);
        size_t max_radius = width / 2u < height / 2u ? width / 2u : height / 2u;
        size_t radius = max_radius ? random_u32() % (max_radius + 1u) : 0;
        memset(actual, 0xa5, sizeof(actual));
        memset(expected, 0xa5, sizeof(expected));
        reference_rounded_mask(expected + GUARD, PITCH, width, height, radius);
        gfx_rounded_rect_mask(actual + GUARD, PITCH, width, height, radius);
        CHECK(memcmp(actual, expected, sizeof(actual)) == 0,
              "seeded rounded mask coverage/padded-stride/guards");
    }
    printf("PASS gfx rounded-mask fuzz: %u cases seed=0xc001d00d coverage=8x8 guards=checked\n", CASES);
    return 0;
}

static uint32_t random_u32(void) {
    uint32_t x = random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    random_state = x;
    return x;
}

static volatile uint32_t bench_sink;
static double monotonic_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return 1000.0 * (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return 1000.0 * (double)ts.tv_sec + (double)ts.tv_nsec / 1000000.0;
#endif
}

static void row_fill_legacy(uint32_t *dst, size_t stride, size_t width,
                            size_t height, uint32_t color) {
    for (size_t y = 0; y < height; ++y)
        gfx_fill_span(dst + y * stride, width, color);
}

static void row_blit_legacy(uint32_t *dst, size_t dst_stride,
                            const uint32_t *src, size_t src_stride,
                            size_t width, size_t height) {
    for (size_t y = 0; y < height; ++y)
        gfx_blit_row(dst + y * dst_stride, src + y * src_stride, width);
}

static void row_blend_legacy(uint32_t *dst, size_t dst_stride,
                             const uint32_t *src, size_t src_stride,
                             size_t width, size_t height) {
    for (size_t y = 0; y < height; ++y)
        gfx_blend_premul_span(dst + y * dst_stride, src + y * src_stride, width);
}

static int correctness(void) {
    uint32_t src_buf[264], dst_buf[264], expected[264];
    for (size_t n = 0; n <= 257; ++n) {
        for (size_t i = 0; i < 264; ++i) dst_buf[i] = expected[i] = 0x91abcdefu;
        gfx_fill_span(dst_buf + 3, n, 0x12345678u);
        for (size_t i = 3; i < 3 + n; ++i) expected[i] = 0x12345678u;
        for (size_t i = 0; i < 264; ++i) CHECK(dst_buf[i] == expected[i], "fill span bounds/value");

        for (size_t i = 0; i < 264; ++i) src_buf[i] = (uint32_t)(i * 0x10203u + n);
        for (size_t i = 0; i < 264; ++i) dst_buf[i] = expected[i] = 0x55aa55aau;
        gfx_blit_row(dst_buf + 2, src_buf + 4, n);
        for (size_t i = 0; i < n; ++i) expected[i + 2] = src_buf[i + 4];
        for (size_t i = 0; i < 264; ++i) CHECK(dst_buf[i] == expected[i], "row blit bounds/value");
    }

    /* Exhaust every destination channel and inverse-alpha multiplier. */
    for (uint32_t alpha = 0; alpha < 256; ++alpha) {
        for (uint32_t channel = 0; channel < 256; ++channel) {
            uint32_t d = channel * 0x010101u;
            uint32_t s = alpha << 24;
            uint32_t got = d;
            gfx_blend_premul_span(&got, &s, 1);
            CHECK(got == reference_blend(d, s), "exhaustive blend scale pair");
        }
    }

    uint32_t d, s, got;
    for (unsigned i = 0; i < 100000; ++i) {
        uint32_t a = random_u32() & 255u;
        uint32_t r = random_u32() % (a + 1u);
        uint32_t g = random_u32() % (a + 1u);
        uint32_t b = random_u32() % (a + 1u);
        s = (a << 24) | (r << 16) | (g << 8) | b;
        d = random_u32() & 0x00ffffffu;
        got = d;
        gfx_blend_premul_span(&got, &s, 1);
        CHECK(got == reference_blend(d, s), "seeded premultiplied blend fuzz");
    }
    printf("PASS gfx correctness: fill/blit lengths 0..257; blend multipliers 65536; seeded pixels 100000 seed=0x6d2b79f5\n");
    return 0;
}

static int benchmark(void) {
    uint32_t *a = (uint32_t *)malloc((size_t)PIXELS * sizeof(uint32_t));
    uint32_t *b = (uint32_t *)malloc((size_t)PIXELS * sizeof(uint32_t));
    uint32_t *c = (uint32_t *)malloc((size_t)PIXELS * sizeof(uint32_t));
    if (!a || !b || !c) { free(a); free(b); free(c); return 1; }
    for (size_t i = 0; i < PIXELS; ++i) {
        a[i] = 0x004080c0u;
        b[i] = 0x80504020u;
        c[i] = (uint32_t)i;
    }
    for (unsigned repeat = 0; repeat < BENCH_REPEATS; ++repeat) {
        double t0 = monotonic_ms();
        for (unsigned i = 0; i < BENCH_ROUNDS; ++i) gfx_fill_span(a, PIXELS, 0x00123456u + i);
        double fill_ms = monotonic_ms() - t0;
        t0 = monotonic_ms();
        for (unsigned i = 0; i < BENCH_ROUNDS; ++i) gfx_blit_row(a, c, PIXELS);
        double copy_ms = monotonic_ms() - t0;
        t0 = monotonic_ms();
        for (unsigned i = 0; i < BENCH_ROUNDS; ++i) gfx_blend_premul_span(a, b, PIXELS);
        double blend_ms = monotonic_ms() - t0;
        bench_sink = a[PIXELS / 2];
    double operations_mpx = (double)PIXELS * BENCH_ROUNDS / 1000.0;
    double fill_mb = operations_mpx * 4.0;
    double copy_mb = operations_mpx * 8.0;
    double blend_mb = operations_mpx * 12.0;
    printf("BENCH gfx mode=%s pixels=%u rounds=%u fill_ms=%.3f fill_Mpx_s=%.1f fill_MB_s=%.1f copy_ms=%.3f copy_Mpx_s=%.1f copy_MB_s=%.1f blend_ms=%.3f blend_Mpx_s=%.1f blend_MB_s=%.1f sink=%08x\n",
#if defined(GFX_REFERENCE)
           "reference",
#else
           "fast",
#endif
           PIXELS, BENCH_ROUNDS,
           fill_ms, operations_mpx / fill_ms, fill_mb / fill_ms,
           copy_ms, operations_mpx / copy_ms, copy_mb / copy_ms,
           blend_ms, operations_mpx / blend_ms, blend_mb / blend_ms,
           (unsigned)bench_sink);
    }

    enum { RW = 1920, RH = 1080, FILL_ROUNDS = 32, COPY_ROUNDS = 16,
           BLEND_ROUNDS = 4, SMALL_W = 320, SMALL_H = 200, SMALL_ROUNDS = 8,
           MASK_ROUNDS = 4 };
    const size_t pitch = RW * sizeof(uint32_t);
    uint8_t *mask = (uint8_t *)malloc(SMALL_W * SMALL_H);
    if (!mask) { free(a); free(b); free(c); return 1; }
    for (size_t i = 0; i < SMALL_W * SMALL_H; ++i) mask[i] = (uint8_t)(i * 37u + 11u);

    for (unsigned repeat = 0; repeat < BENCH_REPEATS; ++repeat) {
        double t0, rect_fill_ms, row_fill_ms, rect_copy_ms, row_copy_ms;
        double rect_blend_ms, row_blend_ms;
        if (repeat & 1u) {
            t0 = monotonic_ms();
            for (unsigned i = 0; i < FILL_ROUNDS; ++i)
                row_fill_legacy(a, RW, RW, RH, 0x00123456u + i);
            row_fill_ms = monotonic_ms() - t0;
            t0 = monotonic_ms();
            for (unsigned i = 0; i < FILL_ROUNDS; ++i)
                gfx_fill_rect((gfx_u8 *)a, pitch, 0, 0, RW, RH, 0x00123456u + i);
            rect_fill_ms = monotonic_ms() - t0;
        } else {
            t0 = monotonic_ms();
            for (unsigned i = 0; i < FILL_ROUNDS; ++i)
                gfx_fill_rect((gfx_u8 *)a, pitch, 0, 0, RW, RH, 0x00123456u + i);
            rect_fill_ms = monotonic_ms() - t0;
            t0 = monotonic_ms();
            for (unsigned i = 0; i < FILL_ROUNDS; ++i)
                row_fill_legacy(a, RW, RW, RH, 0x00123456u + i);
            row_fill_ms = monotonic_ms() - t0;
        }

        if (repeat & 1u) {
            t0 = monotonic_ms();
            for (unsigned i = 0; i < COPY_ROUNDS; ++i)
                row_blit_legacy(a, RW, c, RW, RW, RH);
            row_copy_ms = monotonic_ms() - t0;
            t0 = monotonic_ms();
            for (unsigned i = 0; i < COPY_ROUNDS; ++i)
                gfx_blit_rect((gfx_u8 *)a, pitch, 0, 0, (const gfx_u8 *)c, pitch,
                              0, 0, RW, RH);
            rect_copy_ms = monotonic_ms() - t0;
        } else {
            t0 = monotonic_ms();
            for (unsigned i = 0; i < COPY_ROUNDS; ++i)
                gfx_blit_rect((gfx_u8 *)a, pitch, 0, 0, (const gfx_u8 *)c, pitch,
                              0, 0, RW, RH);
            rect_copy_ms = monotonic_ms() - t0;
            t0 = monotonic_ms();
            for (unsigned i = 0; i < COPY_ROUNDS; ++i)
                row_blit_legacy(a, RW, c, RW, RW, RH);
            row_copy_ms = monotonic_ms() - t0;
        }

        if (repeat & 1u) {
            t0 = monotonic_ms();
            for (unsigned i = 0; i < BLEND_ROUNDS; ++i)
                row_blend_legacy(a, RW, b, RW, RW, RH);
            row_blend_ms = monotonic_ms() - t0;
            t0 = monotonic_ms();
            for (unsigned i = 0; i < BLEND_ROUNDS; ++i)
                gfx_blend_premul_rect((gfx_u8 *)a, pitch, 0, 0, (const gfx_u8 *)b,
                                      pitch, 0, 0, RW, RH);
            rect_blend_ms = monotonic_ms() - t0;
        } else {
            t0 = monotonic_ms();
            for (unsigned i = 0; i < BLEND_ROUNDS; ++i)
                gfx_blend_premul_rect((gfx_u8 *)a, pitch, 0, 0, (const gfx_u8 *)b,
                                      pitch, 0, 0, RW, RH);
            rect_blend_ms = monotonic_ms() - t0;
            t0 = monotonic_ms();
            for (unsigned i = 0; i < BLEND_ROUNDS; ++i)
                row_blend_legacy(a, RW, b, RW, RW, RH);
            row_blend_ms = monotonic_ms() - t0;
        }

        t0 = monotonic_ms();
        for (unsigned i = 0; i < SMALL_ROUNDS; ++i)
            gfx_fill_rect_alpha((gfx_u8 *)a, pitch, 100, 100, SMALL_W, SMALL_H,
                                0x00a060d0u, 151u);
        double alpha_fill_ms = monotonic_ms() - t0;

        t0 = monotonic_ms();
        for (unsigned i = 0; i < MASK_ROUNDS; ++i)
            gfx_rounded_rect_mask(mask, SMALL_W, SMALL_W, SMALL_H, 28u);
        double mask_ms = monotonic_ms() - t0;
        t0 = monotonic_ms();
        for (unsigned i = 0; i < SMALL_ROUNDS; ++i)
            gfx_fill_rect_masked((gfx_u8 *)a, pitch, 100, 100, SMALL_W, SMALL_H,
                                 0x00a060d0u, 151u, mask, SMALL_W);
        double masked_fill_ms = monotonic_ms() - t0;
        bench_sink = a[(RH / 2u) * RW + RW / 2u] ^ mask[SMALL_W * SMALL_H / 2u];

        double fill_mpx = (double)RW * RH * FILL_ROUNDS / 1000000.0;
        double copy_mpx = (double)RW * RH * COPY_ROUNDS / 1000000.0;
        double blend_mpx = (double)RW * RH * BLEND_ROUNDS / 1000000.0;
        double small_mpx = (double)SMALL_W * SMALL_H * SMALL_ROUNDS / 1000000.0;
        double mask_mpx = (double)SMALL_W * SMALL_H * MASK_ROUNDS / 1000000.0;
        printf("BENCH gfx_rect mode=%s fill_Mpx_s=%.1f row_fill_Mpx_s=%.1f copy_Mpx_s=%.1f row_copy_Mpx_s=%.1f blend_Mpx_s=%.1f row_blend_Mpx_s=%.1f alpha_fill_Mpx_s=%.2f mask_Mpx_s=%.3f mask_ms=%.3f masked_fill_Mpx_s=%.2f sink=%08x\n",
#if defined(GFX_REFERENCE)
               "reference",
#else
               "fast",
#endif
               1000.0 * fill_mpx / rect_fill_ms, 1000.0 * fill_mpx / row_fill_ms,
               1000.0 * copy_mpx / rect_copy_ms, 1000.0 * copy_mpx / row_copy_ms,
               1000.0 * blend_mpx / rect_blend_ms, 1000.0 * blend_mpx / row_blend_ms,
               1000.0 * small_mpx / alpha_fill_ms, 1000.0 * mask_mpx / mask_ms, mask_ms,
               1000.0 * small_mpx / masked_fill_ms, (unsigned)bench_sink);
    }
    free(mask);
    free(a); free(b); free(c);
    return 0;
}

int main(void) {
    if (correctness()) return 1;
    if (rectangle_fuzz()) return 1;
    if (rounded_mask_fuzz()) return 1;
    return benchmark();
}
