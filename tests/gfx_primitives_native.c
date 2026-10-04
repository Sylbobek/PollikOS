#include "../kernel/gfx/gfx_primitives.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static uint32_t random_state = 0x6d2b79f5u;
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
    printf("BENCH gfx mode=%s pixels=%u rounds=%u fill_ms=%.3f copy_ms=%.3f blend_ms=%.3f sink=%08x\n",
#if defined(GFX_REFERENCE)
           "reference",
#else
           "fast",
#endif
           PIXELS, BENCH_ROUNDS, fill_ms, copy_ms, blend_ms, (unsigned)bench_sink);
    }
    free(a); free(b); free(c);
    return 0;
}

int main(void) {
    if (correctness()) return 1;
    return benchmark();
}
