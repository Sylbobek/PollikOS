#include "../kernel/soft3d.h"
#include "gfx_corner_stub.h"

extern int printf(const char *, ...);
extern int atoi(const char *);
#ifdef _WIN32
typedef struct { long long QuadPart; } PerfCounter;
__declspec(dllimport) int __stdcall QueryPerformanceCounter(PerfCounter *);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(PerfCounter *);
#else
struct timespec { long tv_sec; long tv_nsec; };
extern int clock_gettime(int, struct timespec *);
#define CLOCK_MONOTONIC 1
#endif

#define REPEATS 3u
#define ROUNDS 8u
#define MAX_PIXELS (1920u * 1080u)

int framebuffer_width(void) { return 1920; }
int framebuffer_height(void) { return 1080; }
int framebuffer_bpp(void) { return 32; }

static u32 pixels[MAX_PIXELS];
static u32 triangle_image[MAX_PIXELS];
static u32 texture_pixels[128u * 128u];

static double monotonic_ms(void) {
#ifdef _WIN32
    PerfCounter counter, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return 1000.0 * (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return 1000.0 * (double)ts.tv_sec + (double)ts.tv_nsec / 1000000.0;
#endif
}

static unsigned long long checksum(const u32 *buffer, unsigned count) {
    unsigned long long h = 1469598103934665603ull;
    for (unsigned i = 0; i < count; ++i) {
        h ^= buffer[i];
        h *= 1099511628211ull;
    }
    return h;
}

static int run(int width, int height) {
    unsigned count = (unsigned)width * (unsigned)height;
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x)
        texture_pixels[y * 128 + x] = ((u32)(x * 2) << 16) | ((u32)(y * 2) << 8) | (u32)((x * 3 + y * 5) & 255);
    GfxTarget target = {pixels, 0, width, height, width, count};
    Triangle tri = {{{{-0.95f,-0.85f,0.0f,1.0f},{1.0f,0.1f,0.1f},{0.0f,0.0f}},
                     {{0.95f,-0.85f,0.0f,1.0f},{0.1f,1.0f,0.1f},{1.0f,0.0f}},
                     {{0.0f,1.9f,0.0f,2.0f},{0.1f,0.1f,1.0f},{0.5f,1.0f}}}};
    unsigned tri_writes = 0;
    double tri_ms[REPEATS];
    for (unsigned repeat = 0; repeat < REPEATS; ++repeat) {
        double t0 = monotonic_ms();
        for (unsigned i = 0; i < ROUNDS; ++i)
            tri_writes = soft3d_triangle(&target, &tri, 1, 2, 0, height);
        tri_ms[repeat] = monotonic_ms() - t0;
    }
    for (unsigned i = 0; i < count; ++i) triangle_image[i] = pixels[i];
    unsigned long long tri_sum = checksum(triangle_image, count);

    GfxTexture texture = {texture_pixels, 128, 128};
    unsigned tex_writes = 0;
    double tex_ms[REPEATS];
    for (unsigned repeat = 0; repeat < REPEATS; ++repeat) {
        double t0 = monotonic_ms();
        for (unsigned i = 0; i < ROUNDS; ++i)
            tex_writes = soft3d_triangle_textured(&target, &tri, 1, 0, height,
                                                   &texture, 1, 0);
        tex_ms[repeat] = monotonic_ms() - t0;
    }
    unsigned long long tex_sum = checksum(pixels, count);
    printf("SOFT3D res=%dx%d rounds=%u tri_writes=%u tri_checksum=%016llx tri_ms=%.3f,%.3f,%.3f tex_writes=%u tex_checksum=%016llx tex_ms=%.3f,%.3f,%.3f\n",
           width, height, ROUNDS, tri_writes, tri_sum, tri_ms[0], tri_ms[1], tri_ms[2],
           tex_writes, tex_sum, tex_ms[0], tex_ms[1], tex_ms[2]);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    int width = atoi(argv[1]), height = atoi(argv[2]);
    if (width < 1 || width > 1920 || height < 1 || height > 1080) return 2;
    return run(width, height);
}
