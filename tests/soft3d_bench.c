#include "../kernel/soft3d.h"
#include "gfx_corner_stub.h"

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

int framebuffer_width(void) { return 1920; }
int framebuffer_height(void) { return 1080; }
int framebuffer_bpp(void) { return 32; }

static u32 texture_pixels[128u * 128u];
static int texture_ready;

static void init_texture(void) {
    if (texture_ready) return;
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x)
        texture_pixels[y * 128 + x] = ((u32)(x * 2) << 16) | ((u32)(y * 2) << 8) | (u32)((x * 3 + y * 5) & 255);
    texture_ready = 1;
}

EXPORT unsigned int soft3d_bench_render(int width, int height, int textured, u32 *pixels) {
    if (!pixels || width < 1 || width > 1920 || height < 1 || height > 1080 || textured < 0 || textured > 1)
        return 0;
    init_texture();
    GfxTarget target = {pixels, 0, width, height, width, (u32)width * (u32)height};
    Triangle tri = {{{{-0.95f,-0.85f,0.0f,1.0f},{1.0f,0.1f,0.1f},{0.0f,0.0f}},
                     {{0.95f,-0.85f,0.0f,1.0f},{0.1f,1.0f,0.1f},{1.0f,0.0f}},
                     {{0.0f,1.9f,0.0f,2.0f},{0.1f,0.1f,1.0f},{0.5f,1.0f}}}};
    if (!textured) return soft3d_triangle(&target, &tri, 1, 2, 0, height);
    GfxTexture texture = {texture_pixels, 128, 128};
    return soft3d_triangle_textured(&target, &tri, 1, 0, height, &texture, 1, 0);
}
