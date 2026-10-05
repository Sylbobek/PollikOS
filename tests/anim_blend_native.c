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

#define PIXELS (1920u * 1080u)
#define REPEATS 3u

static uint32_t rng_state = 0x5d3u;
static volatile uint32_t bench_sink;

static uint32_t next_u32(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static uint32_t blend_reference(uint32_t dst, uint32_t src, unsigned alpha) {
    if (alpha == 0) return dst;
    if (alpha >= 256) return src;
    unsigned inv = 256u - alpha;
    unsigned r = (((dst >> 16) & 255u) * inv + ((src >> 16) & 255u) * alpha) >> 8;
    unsigned g = (((dst >> 8) & 255u) * inv + ((src >> 8) & 255u) * alpha) >> 8;
    unsigned b = ((dst & 255u) * inv + (src & 255u) * alpha) >> 8;
    return (r << 16) | (g << 8) | b;
}

static uint32_t blend_packed(uint32_t dst, uint32_t src, unsigned alpha) {
    if (alpha == 0) return dst;
    if (alpha >= 256) return src;
    unsigned inv = 256u - alpha;
    uint32_t rb = (((dst & 0x00ff00ffu) * inv +
                    (src & 0x00ff00ffu) * alpha) >> 8) & 0x00ff00ffu;
    uint32_t g = (((dst & 0x0000ff00u) * inv +
                   (src & 0x0000ff00u) * alpha) >> 8) & 0x0000ff00u;
    return rb | g;
}

static double monotonic_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}

static int correctness(void) {
    for (unsigned alpha = 0; alpha <= 256; ++alpha) {
        for (unsigned i = 0; i < 2000; ++i) {
            uint32_t dst = next_u32() & 0x00ffffffu;
            uint32_t src = next_u32() & 0x00ffffffu;
            if (blend_reference(dst, src, alpha) != blend_packed(dst, src, alpha)) {
                fprintf(stderr, "FAIL alpha=%u dst=%08x src=%08x ref=%08x packed=%08x\n",
                        alpha, dst, src, blend_reference(dst, src, alpha),
                        blend_packed(dst, src, alpha));
                return 1;
            }
        }
    }
    puts("PASS animation blend exact parity: alpha=0..256 vectors=514000 seed=0x5d3");
    return 0;
}

static int benchmark(void) {
    size_t bytes = (size_t)PIXELS * sizeof(uint32_t);
    uint32_t *base = (uint32_t *)malloc(bytes);
    uint32_t *src = (uint32_t *)malloc(bytes);
    uint32_t *ref = (uint32_t *)malloc(bytes);
    uint32_t *fast = (uint32_t *)malloc(bytes);
    if (!base || !src || !ref || !fast) {
        free(base); free(src); free(ref); free(fast);
        return 1;
    }
    for (size_t i = 0; i < PIXELS; ++i) {
        base[i] = next_u32() & 0x00ffffffu;
        src[i] = next_u32() & 0x00ffffffu;
    }
    for (unsigned repeat = 0; repeat < REPEATS; ++repeat) {
        memcpy(ref, base, bytes);
        double t0 = monotonic_ms();
        for (size_t i = 0; i < PIXELS; ++i)
            ref[i] = blend_reference(ref[i], src[i], 137u);
        double reference_ms = monotonic_ms() - t0;

        memcpy(fast, base, bytes);
        t0 = monotonic_ms();
        for (size_t i = 0; i < PIXELS; ++i)
            fast[i] = blend_packed(fast[i], src[i], 137u);
        double packed_ms = monotonic_ms() - t0;
        if (memcmp(ref, fast, bytes) != 0) {
            fputs("FAIL benchmark output differs\n", stderr);
            free(base); free(src); free(ref); free(fast);
            return 1;
        }
        bench_sink ^= ref[PIXELS / 2u] ^ fast[PIXELS / 3u];
        printf("BENCH animation_blend pixels=%u repeat=%u reference_ms=%.3f packed_ms=%.3f "
               "exact=1 sink=%08x\n", PIXELS, repeat + 1u,
               reference_ms, packed_ms, (unsigned)bench_sink);
    }
    free(base); free(src); free(ref); free(fast);
    return 0;
}

int main(void) {
    return correctness() || benchmark();
}
