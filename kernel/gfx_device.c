#include "gfx_device.h"
#include "graphics.h"
extern int framebuffer_width(void), framebuffer_height(void), framebuffer_bpp(void);
void gfx_device_info(GfxInfo *out) {
    if (out) *out = (GfxInfo){framebuffer_width(), framebuffer_height(), framebuffer_bpp(), 31};
}
u32 gfx_ratio(u64 n, u32 d) {
    if (!d) return 0;
    u64 q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) { r -= d; q |= 1ull << i; }
    }
    return q > 0xffffffffu ? 0xffffffffu : (u32)q;
}
int gfx_target_valid(const GfxTarget *t) {
    return t && t->color && t->width > 0 && t->height > 0 &&
        t->width <= 2048 && t->height <= 2048 && t->stride >= t->width &&
        t->stride <= 4096 && (u64)(u32)t->stride * t->height <= t->capacity &&
        t->capacity <= 0x3fffffffu;
}
u32 gfx_clear(GfxTarget *t, u32 offset, u32 count, u32 color) {
    if (!gfx_target_valid(t)) return 0;
    u32 size = t->stride * t->height;
    if (offset >= size) return 0;
    if (count > size - offset) count = size - offset;
    for (u32 i = offset; i < offset + count; i++) {
        t->color[i] = color;
        if (t->depth) t->depth[i] = 1.0f;
    }
    return count;
}
u32 gfx_rect(GfxTarget *t, int x, int y, int w, int h, u32 color) {
    if (!gfx_target_valid(t) || w <= 0 || h <= 0) return 0;
    long long right = (long long)x + w, bottom = (long long)y + h;
    if (right > t->width) right = t->width;
    if (bottom > t->height) bottom = t->height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    u32 n = 0;
    for (int j = y; j < bottom; j++) for (int i = x; i < right; i++) {
        t->color[j*t->stride+i] = color; n++;
    }
    return n;
}
u32 gfx_shape(GfxTarget *t, int x, int y, int w, int h, u32 color, u32 kind) {
    if(!gfx_target_valid(t) || w<1 || h<1 || w>64 || h>64 || kind>4 ||
       x < -64 || y < -64 || x>=t->width || y>=t->height)return 0;
    if(kind==0)return gfx_rect(t,x,y,w,h,color);
    /* Bit zero is the left column, one blank column between glyphs. */
    static const u16 pm[7]={0x44f,0x6d1,0x551,0x44f,0x441,0x441,0x441};
    if(kind==3) {w=11;h=7;}
    int radius=w<h?w/2:h/2;if(radius>3)radius=3;
    u32 n=0;
    for(int j=0;j<h;j++)for(int i=0;i<w;i++) {
        int px=x+i,py=y+j;
        if(px<0 || py<0 || px>=t->width || py>=t->height)continue;
        int a=64;
        if(kind==1) {
            int cx=i<w/2?i:w-1-i,cy=j<h/2?j:h-1-j;
            a=graphics_corner_coverage(radius,cx,cy);
        } else if(kind==2) {
            /* Supercover of the diagonal: bounded, connected at any slope. */
            int d=i*(h-1)-j*(w-1);if(d<0)d=-d;
            if(2*d>w+h-2)continue;
        } else if(kind==3) {if(!(pm[j]&(1u<<i)))continue;}
        else a=32;
        if(!a)continue;
        u32 *p=&t->color[py*t->stride+px],b=64-a;
        *p=((((color&0xff00ff)*a+(*p&0xff00ff)*b)>>6)&0xff00ff) |
            ((((color&0xff00)*a+(*p&0xff00)*b)>>6)&0xff00);
        n++;
    }
    return n;
}
