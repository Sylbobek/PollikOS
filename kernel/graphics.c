#include "graphics.h"
#include "gui/app_host.h"
#include "font_data.h"

static u32 *draw_target;
static int draw_target_w = 1024, draw_target_h = 768, draw_target_stride = 1024;
static int screen_w = 1024, screen_h = 768;
static GraphicsClip draw_clip = {0, 0, 1024, 768};
/* All UI radii through the 29px dock, packed as sum(r*r), in BSS. */
#define CORNER_CACHE_MAX 29
static u8 corner_cache[8555];
static u16 corner_offsets[CORNER_CACHE_MAX + 1];
static int corner_cache_ready;
static int corner_coverage(int a, int b, int r) {
    if ((a + 1) * (a + 1) + (b + 1) * (b + 1) <= 4 * r * r) return 64;
    if ((a - 1) * (a - 1) + (b - 1) * (b - 1) >= 4 * r * r) return 0;
    int n = 0;
    for (int y = -3; y <= 3; y += 2)
        for (int x = -3; x <= 3; x += 2)
            if ((4 * a + x) * (4 * a + x) + (4 * b + y) * (4 * b + y) <= 64 * r * r) n += 4;
    return n;
}
void graphics_init(int w, int h) {
    screen_w = w; screen_h = h;
    if (corner_cache_ready) return;
    unsigned offset = 0;
    for (int r = 1; r <= CORNER_CACHE_MAX; r++) {
        corner_offsets[r] = (u16)offset;
        for (int j = 0; j < r; j++)
            for (int i = 0; i < r; i++)
                corner_cache[offset++] = (u8)corner_coverage(2*r-2*i-1, 2*r-2*j-1, r);
    }
    corner_cache_ready = 1;
}
int graphics_corner_coverage(int radius, int x, int y) {
    if (x < 0 || y < 0) return 0;
    if (radius <= 0 || x >= radius || y >= radius) return 64;
    if (!corner_cache_ready) graphics_init(screen_w, screen_h);
    if (radius <= CORNER_CACHE_MAX)
        return corner_cache[corner_offsets[radius] + y * radius + x];
    return corner_coverage(2*radius-2*x-1, 2*radius-2*y-1, radius);
}
GraphicsClip graphics_get_clip(void) { return draw_clip; }
void graphics_set_clip(GraphicsClip clip) {
    if (clip.x1 < 0) clip.x1 = 0;
    if (clip.y1 < 0) clip.y1 = 0;
    if (clip.x2 > draw_target_w) clip.x2 = draw_target_w;
    if (clip.y2 > draw_target_h) clip.y2 = draw_target_h;
    if (clip.x2 < clip.x1) clip.x2 = clip.x1;
    if (clip.y2 < clip.y1) clip.y2 = clip.y1;
    draw_clip = clip;
}
void set_draw_target(u32 *buf, int w, int h, int stride) {
    draw_target = buf;
    draw_target_w = w; draw_target_h = h; draw_target_stride = stride;
    draw_clip = (GraphicsClip){0, 0, w, h};
}
void app_host_blit(int x, int y, const u32 *src, int w, int h, int stride, u32 capacity) {
    if (!src || !draw_target || w <= 0 || h <= 0 || w > 2048 || h > 2048 ||
        stride < w || stride > 4096 || (u64)(u32)stride * h > capacity) return;
    int sx = 0, sy = 0;
    /* Bound origin before signed subtraction/addition. */
    if (x < -2048 || y < -2048 || x >= draw_clip.x2 || y >= draw_clip.y2) return;
    if (x < draw_clip.x1) { sx = draw_clip.x1 - x; x += sx; }
    if (y < draw_clip.y1) { sy = draw_clip.y1 - y; y += sy; }
    w -= sx; h -= sy;
    if (w > draw_clip.x2 - x) w = draw_clip.x2 - x;
    if (h > draw_clip.y2 - y) h = draw_clip.y2 - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++)
        memcpy(draw_target + (y+j)*draw_target_stride+x, src+(sy+j)*stride+sx, (u32)w*4);
}
void rect(int x, int y, int w, int h, u32 c) {
    if (x < draw_clip.x1) { w -= draw_clip.x1 - x; x = draw_clip.x1; }
    if (y < draw_clip.y1) { h -= draw_clip.y1 - y; y = draw_clip.y1; }
    if (x + w > draw_clip.x2) w = draw_clip.x2 - x;
    if (y + h > draw_clip.y2) h = draw_clip.y2 - y;
    if (w <= 0 || h <= 0) return;
    for (int j = y; j < y + h; j++) {
        u32 *row = &draw_target[j * draw_target_stride + x];
        u32 count = (u32)w;
        __asm__ volatile("cld; rep stosl" : "+D"(row), "+c"(count) : "a"(c) : "memory");
    }
}
static inline u32 blend_precomputed(u32 bg, int inv, int cr, int cg, int cb) {
    int r = (((bg >> 16) & 255) * inv + cr) >> 8;
    int g = (((bg >> 8) & 255) * inv + cg) >> 8;
    int b = ((bg & 255) * inv + cb) >> 8;
    return (u32)(r << 16 | g << 8 | b);
}
void rounded(int x, int y, int w, int h, int r, u32 c, int opacity) {
    if (w <= 0 || h <= 0 || opacity <= 0) return;
    if (opacity >= 256) { roundrect(x, y, w, h, r, c); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int inv = 256 - opacity;
    int cr = ((c >> 16) & 255) * opacity;
    int cg = ((c >> 8) & 255) * opacity;
    int cb = (c & 255) * opacity;
    if (r <= 0) {
        int first = y < draw_clip.y1 ? draw_clip.y1 - y : 0;
        int last = y + h > draw_clip.y2 ? draw_clip.y2 - y : h;
        for (int j = first; j < last; j++) {
            int yy = y + j;
            int x1 = x < draw_clip.x1 ? draw_clip.x1 : x;
            int x2 = x + w > draw_clip.x2 ? draw_clip.x2 : x + w;
            for (int i = x1; i < x2; i++) {
                u32 *p = &draw_target[yy * draw_target_stride + i];
                *p = blend_precomputed(*p, inv, cr, cg, cb);
            }
        }
        return;
    }
    int first = y < draw_clip.y1 ? draw_clip.y1 - y : 0;
    int last = y + h > draw_clip.y2 ? draw_clip.y2 - y : h;
    for (int j = first; j < last; j++) {
        int yy = y + j;
        int is_top = j < r, is_bot = j >= h - r;
        int cy = is_top ? j : h - 1 - j;
        if (!is_top && !is_bot) {
            int x1 = x < draw_clip.x1 ? draw_clip.x1 : x;
            int x2 = x + w > draw_clip.x2 ? draw_clip.x2 : x + w;
            for (int i = x1; i < x2; i++) {
                u32 *p = &draw_target[yy * draw_target_stride + i];
                *p = blend_precomputed(*p, inv, cr, cg, cb);
            }
            continue;
        }
        for (int i = 0; i < r; i++) {
            int xx = x + i;
            if (xx < draw_clip.x1 || xx >= draw_clip.x2) continue;
            int coverage = graphics_corner_coverage(r, i, cy);
            if (coverage) {
                u32 *p = &draw_target[yy * draw_target_stride + xx];
                *p = blend(*p, c, opacity * coverage / 64);
            }
        }
        int mid_start = x + r, mid_end = x + w - r;
        if (mid_start < draw_clip.x1) mid_start = draw_clip.x1;
        if (mid_end > draw_clip.x2) mid_end = draw_clip.x2;
        for (int i = mid_start; i < mid_end; i++) {
            u32 *p = &draw_target[yy * draw_target_stride + i];
            *p = blend_precomputed(*p, inv, cr, cg, cb);
        }
        for (int i = w - r; i < w; i++) {
            int xx = x + i;
            if (xx < draw_clip.x1 || xx >= draw_clip.x2) continue;
            int coverage = graphics_corner_coverage(r, w - 1 - i, cy);
            if (coverage) {
                u32 *p = &draw_target[yy * draw_target_stride + xx];
                *p = blend(*p, c, opacity * coverage / 64);
            }
        }
    }
}
void roundrect_slice(int x, int y, int w, int h, int r, u32 c, int j_start, int j_end) {
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 0) {
        if (j_start < 0) j_start = 0;
        if (j_end > h) j_end = h;
        if (j_end > j_start) rect(x, y + j_start, w, j_end - j_start, c);
        return;
    }
    if (j_start < 0) j_start = 0;
    if (j_end > h) j_end = h;
    if (j_start < draw_clip.y1 - y) j_start = draw_clip.y1 - y;
    if (j_end > draw_clip.y2 - y) j_end = draw_clip.y2 - y;
    for (int j = j_start; j < j_end; j++) {
        int yy = y + j;
        int is_top = j < r, is_bot = j >= h - r;
        if (!is_top && !is_bot) {
            int x1 = x < draw_clip.x1 ? draw_clip.x1 : x;
            int x2 = x + w > draw_clip.x2 ? draw_clip.x2 : x + w;
            if (x2 > x1) {
                u32 *row = &draw_target[yy * draw_target_stride + x1];
                u32 count = (u32)(x2 - x1);
                __asm__ volatile("cld; rep stosl" : "+D"(row), "+c"(count) : "a"(c) : "memory");
            }
            continue;
        }
        for (int i = 0; i < r; i++) {
            int xx = x + i;
            if (xx < draw_clip.x1 || xx >= draw_clip.x2) continue;
            int cj = is_top ? j : h - 1 - j;
            int coverage = graphics_corner_coverage(r, i, cj);
            if (coverage) {
                u32 *p = &draw_target[yy * draw_target_stride + xx];
                *p = coverage == 64 ? c : blend(*p, c, coverage * 4);
            }
        }
        int mid_start = x + r, mid_end = x + w - r;
        if (mid_start < draw_clip.x1) mid_start = draw_clip.x1;
        if (mid_end > draw_clip.x2) mid_end = draw_clip.x2;
        if (mid_end > mid_start) {
            u32 *row = &draw_target[yy * draw_target_stride + mid_start];
            u32 count = (u32)(mid_end - mid_start);
            __asm__ volatile("cld; rep stosl" : "+D"(row), "+c"(count) : "a"(c) : "memory");
        }
        for (int i = w - r; i < w; i++) {
            int xx = x + i;
            if (xx < draw_clip.x1 || xx >= draw_clip.x2) continue;
            int ci = w - 1 - i, cj = is_top ? j : h - 1 - j;
            int coverage = graphics_corner_coverage(r, ci, cj);
            if (coverage) {
                u32 *p = &draw_target[yy * draw_target_stride + xx];
                *p = coverage == 64 ? c : blend(*p, c, coverage * 4);
            }
        }
    }
}
void roundrect(int x, int y, int w, int h, int r, u32 c) { roundrect_slice(x, y, w, h, r, c, 0, h); }
void roundrect_stroke(int x, int y, int w, int h, int r, int t, u32 stroke, u32 fill) {
    if (w <= 0 || h <= 0) return;
    if (t < 1) t = 1;
    if (r < t) r = t;
    roundrect(x, y, w, h, r, stroke);
    if (w - 2 * t > 0 && h - 2 * t > 0) roundrect(x + t, y + t, w - 2 * t, h - 2 * t, r - t, fill);
}
/* Coverage of a rounded rect for one interior pixel; 64 fully inside, 0 outside. */
static int rrect_coverage(int w, int h, int r, int i, int j) {
    if (i < 0 || j < 0 || i >= w || j >= h) return 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 0) return 64;
    int cx = i < r ? i : (i >= w - r ? w - 1 - i : r);
    int cy = j < r ? j : (j >= h - r ? h - 1 - j : r);
    return graphics_corner_coverage(r, cx, cy);
}
void roundrect_border(int x, int y, int w, int h, int r, int t, u32 c) {
    if (w <= 0 || h <= 0 || t <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int iw = w - 2 * t, ih = h - 2 * t;
    if (iw < 0) iw = 0;
    if (ih < 0) ih = 0;
    int ir = r - t; if (ir < 0) ir = 0;
    /* Only the t-pixel perimeter interacts with the outline; skip interiors. */
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < draw_clip.y1 || yy >= draw_clip.y2) continue;
        int edge_row = j < t || j >= h - t;
        u32 *row = &draw_target[yy * draw_target_stride];
        for (int i = 0; i < w; i++) {
            if (!edge_row && i >= t && i < w - t) { i = w - t - 1; continue; }
            int xx = x + i;
            if (xx < draw_clip.x1 || xx >= draw_clip.x2) continue;
            int coverage = rrect_coverage(w, h, r, i, j) - rrect_coverage(iw, ih, ir, i - t, j - t);
            if (coverage <= 0) continue;
            u32 *p = &row[xx];
            if (coverage >= 64) *p = c;
            else *p = blend(*p, c, coverage * 4);
        }
    }
}
int text_width(const char *s, int scale) {
    int result = 0;
    if (scale < 1) scale = 1;
    if (scale > 5) scale = 5;
    while (*s) {
        u8 c = *s++;
        if (c >= 32 && c < 127) result += font_glyphs[scale - 1][c - 32].advance;
    }
    return result;
}
static const u16 font_scale_table[16] = {0,17,34,51,68,85,102,119,136,153,170,187,204,221,238,256};
static void letter(int x, int y, u8 c, u32 color, int scale) {
    if (c < 32 || c >= 127) return;
    const FontGlyph *g = &font_glyphs[scale - 1][c - 32];
    for (int j = 0; j < g->height; j++) {
        int yy = y + j;
        if (yy < draw_clip.y1 || yy >= draw_clip.y2) continue;
        for (int i = 0; i < g->width; i++) {
            int xx = x + i;
            if (xx < draw_clip.x1 || xx >= draw_clip.x2) continue;
            int index = j * g->width + i;
            u8 packed = font_coverage[g->offset + index / 2];
            int coverage = (index & 1) ? packed & 15 : packed >> 4;
            if (coverage == 15) draw_target[yy * draw_target_stride + xx] = color;
            else if (coverage) {
                u32 *p = &draw_target[yy * draw_target_stride + xx];
                *p = blend(*p, color, font_scale_table[coverage]);
            }
        }
    }
}
void text(int x, int y, const char *s, u32 color, int scale) {
    int origin = x;
    if (scale < 1) scale = 1;
    if (scale > 5) scale = 5;
    while (*s) {
        u8 c = *s++;
        if (c == '\n') { y += font_glyphs[scale - 1][0].height + 3; x = origin; continue; }
        if (c >= 32 && c < 127) { letter(x, y, c, color, scale); x += font_glyphs[scale - 1][c - 32].advance; }
    }
}
void centered(int x, int y, int w, const char *s, u32 c, int scale) { text(x + (w - text_width(s, scale)) / 2, y, s, c, scale); }
void app_draw_centered(int x, int y, int w, const char *s, u32 c, int scale) { centered(x,y,w,s,c,scale); }
void app_draw_mono(int x, int y, const char *s, u32 color) {
    while (*s) {
        u8 c = *s++;
        if (c >= 32 && c < 127) letter(x + (12 - font_glyphs[0][c - 32].advance) / 2, y, c, color, 1);
        x += 12;
    }
}
void graphics_blit_rgba(int dx, int dy, int dw, int dh, const u8 *rgba, int sw, int sh) {
    if (!rgba || !draw_target || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    for (int y = 0; y < dh; y++) {
        int yy = dy + y;
        if (yy < draw_clip.y1 || yy >= draw_clip.y2) continue;
        int sy = (int)((long)y * sh / dh);
        u32 *row = &draw_target[yy * draw_target_stride];
        for (int x = 0; x < dw; x++) {
            int xx = dx + x;
            if (xx < draw_clip.x1 || xx >= draw_clip.x2) continue;
            int sx = (int)((long)x * sw / dw);
            const u8 *p = rgba + 4 * ((u32)sy * sw + sx);
            int a = p[3];
            if (!a) continue;
            u32 c = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
            row[xx] = (a >= 255) ? c : blend(row[xx], c, a);
        }
    }
}
void sys_text_to_buffer(u32 *buffer,int width,int height,int stride,int x,int y,const char *s,u32 color,int scale) {
    if (!buffer || !s) return;
    u32 *ot = draw_target;
    int ow = draw_target_w, oh = draw_target_h, os = draw_target_stride;
    GraphicsClip oc = draw_clip;
    draw_target = buffer; draw_target_w = width; draw_target_h = height; draw_target_stride = stride;
    draw_clip = (GraphicsClip){0, 0, width, height};
    if (scale < 1) scale = 1; else if (scale > 5) scale = 5;
    while (*s) {
        if (*s == '\n') { y += font_glyphs[scale - 1][0].height + 3; s++; continue; }
        u8 ch = (u8)*s++;
        if (ch >= 32 && ch < 127) {
            letter(x, y, ch, color, scale);
            x += font_glyphs[scale - 1][ch - 32].advance;
        }
    }
    draw_target = ot; draw_target_w = ow; draw_target_h = oh; draw_target_stride = os; draw_clip = oc;
}
void app_draw_letter(int x,int y,u8 c,u32 color,int scale) { letter(x,y,c,color,scale); }void ui_bridge_rect(int x,int y,int w,int h,u32 c) { rect(x,y,w,h,c); }
void ui_bridge_roundrect(int x,int y,int w,int h,int r,u32 c) { roundrect(x,y,w,h,r,c); }
void ui_bridge_roundrect_border(int x,int y,int w,int h,int r,int t,u32 c) { roundrect_border(x,y,w,h,r,t,c); }
void ui_bridge_roundrect_stroke(int x,int y,int w,int h,int r,int t,u32 s,u32 f) { roundrect_stroke(x,y,w,h,r,t,s,f); }
void ui_bridge_blit_rgba(int x,int y,int w,int h,const u8 *rgba,int sw,int sh) { graphics_blit_rgba(x,y,w,h,rgba,sw,sh); }
void ui_bridge_rounded(int x,int y,int w,int h,int r,u32 c,int op) { rounded(x,y,w,h,r,c,op); }
void ui_bridge_text(int x,int y,const char *s,u32 c,int scale) { text(x,y,s,c,scale); }
int ui_bridge_text_width(const char *s,int scale) { return text_width(s,scale); }
u32 ui_bridge_blend(u32 a,u32 b,int t) { return blend(a,b,t); }
int ui_bridge_screen_width(void) { return screen_w; }
int ui_bridge_screen_height(void) { return screen_h; }
int sys_text_width(const char *s,int scale) { return text_width(s,scale); }
int sys_get_glyph_advance(u8 c,int scale) {
    if (scale < 1 || scale > 5 || c < 32 || c >= 127) return 0;
    return font_glyphs[scale-1][c-32].advance;
}
void sys_draw_rect_clipped(int x,int y,int w,int h,u32 c,int x1,int y1,int x2,int y2) {
    int r=x+w,b=y+h; if(x<x1)x=x1; if(y<y1)y=y1; if(r>x2)r=x2; if(b>y2)b=y2;
    if(r>x && b>y) rect(x,y,r-x,b-y,c);
}
void sys_draw_rounded_clipped(int x,int y,int w,int h,int r,u32 c,int x1,int y1,int x2,int y2) {
    if(x>=x1 && y>=y1 && x+w<=x2 && y+h<=y2) roundrect(x,y,w,h,r,c);
    else sys_draw_rect_clipped(x,y,w,h,c,x1,y1,x2,y2);
}
void sys_draw_window_bottom(int x,int y,int w,int h,int r,u32 c,int start_y) { roundrect_slice(x,y,w,h,r,c,start_y-y,h); }void sys_draw_roundrect_border_clipped(int x,int y,int w,int h,int r,int t,u32 c,int x1,int y1,int x2,int y2) {
    GraphicsClip saved = draw_clip;
    GraphicsClip cl = {x1, y1, x2, y2};
    if (cl.x1 < saved.x1) cl.x1 = saved.x1;
    if (cl.y1 < saved.y1) cl.y1 = saved.y1;
    if (cl.x2 > saved.x2) cl.x2 = saved.x2;
    if (cl.y2 > saved.y2) cl.y2 = saved.y2;
    draw_clip = cl;
    roundrect_border(x, y, w, h, r, t, c);
    draw_clip = saved;
}
void sys_draw_roundrect_stroke_clipped(int x,int y,int w,int h,int r,int t,u32 stroke,u32 fill,int x1,int y1,int x2,int y2) {
    GraphicsClip saved = draw_clip;
    GraphicsClip cl = {x1, y1, x2, y2};
    if (cl.x1 < saved.x1) cl.x1 = saved.x1;
    if (cl.y1 < saved.y1) cl.y1 = saved.y1;
    if (cl.x2 > saved.x2) cl.x2 = saved.x2;
    if (cl.y2 > saved.y2) cl.y2 = saved.y2;
    draw_clip = cl;
    roundrect_stroke(x, y, w, h, r, t, stroke, fill);
    draw_clip = saved;
}
void sys_draw_rgba_clipped(int x,int y,int w,int h,const u8 *rgba,int sw,int sh,int x1,int y1,int x2,int y2) {
    GraphicsClip saved = draw_clip;
    GraphicsClip cl = {x1, y1, x2, y2};
    if (cl.x1 < saved.x1) cl.x1 = saved.x1;
    if (cl.y1 < saved.y1) cl.y1 = saved.y1;
    if (cl.x2 > saved.x2) cl.x2 = saved.x2;
    if (cl.y2 > saved.y2) cl.y2 = saved.y2;
    draw_clip = cl;
    graphics_blit_rgba(x, y, w, h, rgba, sw, sh);
    draw_clip = saved;
}
/* Opaque 0x00RRGGBB pixel buffer (PollikGL canvas) scaled nearest into target. */
void sys_draw_canvas_clipped(int x,int y,int w,int h,const u32 *src,int sw,int sh,int x1,int y1,int x2,int y2) {
    GraphicsClip saved = draw_clip;
    GraphicsClip cl = {x1, y1, x2, y2};
    if (cl.x1 < saved.x1) cl.x1 = saved.x1;
    if (cl.y1 < saved.y1) cl.y1 = saved.y1;
    if (cl.x2 > saved.x2) cl.x2 = saved.x2;
    if (cl.y2 > saved.y2) cl.y2 = saved.y2;
    draw_clip = cl;
    if (src && w > 0 && h > 0 && sw > 0 && sh > 0) {
        for (int j = 0; j < h; j++) {
            int yy = y + j;
            if (yy < draw_clip.y1 || yy >= draw_clip.y2) continue;
            int sy = (int)((long)j * sh / h);
            u32 *row = &draw_target[yy * draw_target_stride];
            for (int i = 0; i < w; i++) {
                int xx = x + i;
                if (xx < draw_clip.x1 || xx >= draw_clip.x2) continue;
                int sx = (int)((long)i * sw / w);
                row[xx] = src[sy * sw + sx];
            }
        }
    }
    draw_clip = saved;
}
void sys_draw_letter_clipped(int x,int y,u8 c,u32 color,int scale,int x1,int y1,int x2,int y2) {
    if(c<32 || c>=127 || scale<1 || scale>5)return;
    const FontGlyph *g=&font_glyphs[scale-1][c-32];
    for(int j=0;j<g->height;j++)for(int i=0;i<g->width;i++) {
        int xx=x+i,yy=y+j;
        if(xx<x1||yy<y1||xx>=x2||yy>=y2||xx<draw_clip.x1||yy<draw_clip.y1||xx>=draw_clip.x2||yy>=draw_clip.y2)continue;
        int n=j*g->width+i; u8 p=font_coverage[g->offset+n/2]; int a=(n&1)?p&15:p>>4;
        if(a == 15) draw_target[yy*draw_target_stride+xx]=color;
        else if(a) draw_target[yy*draw_target_stride+xx]=blend(draw_target[yy*draw_target_stride+xx],color,font_scale_table[a]);
    }
}
void sprite(int x,int y,int w,int h,const u8 *indices,const u8 *alpha,const u32 *palette,int sw,int sh) {
    if (w == sw && h == sh) {
        for (int j = 0; j < h; j++) {
            int yy = y + j;
            if (yy < draw_clip.y1 || yy >= draw_clip.y2) continue;
            for (int i = 0; i < w; i++) {
                int xx = x + i;
                if (xx < draw_clip.x1 || xx >= draw_clip.x2) continue;
                int index = j * sw + i;
                u32 color = palette[indices[index]];
                int a = ((index & 1) ? alpha[index / 2] & 15 : alpha[index / 2] >> 4) * 17;
                if (a) {
                    u32 *p = &draw_target[yy * draw_target_stride + xx];
                    *p = blend(*p, color & 0xffffff, a == 255 ? 256 : a);
                }
            }
        }
        return;
    }
    if (w <= 0 || h <= 0 || w > 80) return;
    int x0_lut[80], x1_lut[80], fx_lut[80];
    int sx_step = ((sw - 1) << 8) / (w > 1 ? w - 1 : 1);
    for (int i = 0; i < w; i++) {
        int sx = i * sx_step, x0 = sx >> 8;
        x0_lut[i] = x0; x1_lut[i] = x0 + 1 < sw ? x0 + 1 : x0; fx_lut[i] = sx & 255;
    }
    int sy_step = ((sh - 1) << 8) / (h > 1 ? h - 1 : 1);
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < draw_clip.y1 || yy >= draw_clip.y2) continue;
        int sy = j * sy_step, y0 = sy >> 8;
        int y1 = y0 + 1 < sh ? y0 + 1 : y0, fy = sy & 255;
        int row0 = y0 * sw, row1 = y1 * sw;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < draw_clip.x1 || xx >= draw_clip.x2) continue;
            int x0 = x0_lut[i], x1 = x1_lut[i], fx = fx_lut[i];
            int idx00 = row0 + x0, idx10 = row0 + x1, idx01 = row1 + x0, idx11 = row1 + x1;
            int a00 = ((idx00 & 1) ? alpha[idx00 / 2] & 15 : alpha[idx00 / 2] >> 4) * 17;
            int a10 = ((idx10 & 1) ? alpha[idx10 / 2] & 15 : alpha[idx10 / 2] >> 4) * 17;
            int a01 = ((idx01 & 1) ? alpha[idx01 / 2] & 15 : alpha[idx01 / 2] >> 4) * 17;
            int a11 = ((idx11 & 1) ? alpha[idx11 / 2] & 15 : alpha[idx11 / 2] >> 4) * 17;
            if (!(a00 | a10 | a01 | a11)) continue;
            int a_top = a00 + (((a10 - a00) * fx) >> 8), a_bot = a01 + (((a11 - a01) * fx) >> 8);
            int a = a_top + (((a_bot - a_top) * fy) >> 8);
            if (a > 0) {
                u32 c00 = palette[indices[idx00]] & 0xffffff, c10 = palette[indices[idx10]] & 0xffffff;
                u32 c01 = palette[indices[idx01]] & 0xffffff, c11 = palette[indices[idx11]] & 0xffffff;
                if (!a00) c00 = c10;
                if (!a10) c10 = c00;
                if (!a01) c01 = c11;
                if (!a11) c11 = c01;
                u32 color = blend(blend(c00,c10,fx),blend(c01,c11,fx),fy);
                u32 *p = &draw_target[yy * draw_target_stride + xx];
                *p = blend(*p, color, a >= 255 ? 256 : a);
            }
        }
    }
}
