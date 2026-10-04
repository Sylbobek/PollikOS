#include "pollikgl.h"
/* PollikGL software backend. All primitives are immediate-mode and clipped to
 * the current scissor; colors are packed 0x00RRGGBB. */
void pgl_begin(Pgl *g, u32 *pixels, int width, int height, int stride) {
    g->pixels = pixels;
    g->width = width; g->height = height; g->stride = stride;
    g->clip_x0 = 0; g->clip_y0 = 0; g->clip_x1 = width; g->clip_y1 = height;
}
void pgl_clip(Pgl *g, int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g->width) x1 = g->width;
    if (y1 > g->height) y1 = g->height;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    g->clip_x0 = x0; g->clip_y0 = y0; g->clip_x1 = x1; g->clip_y1 = y1;
}
void pgl_clear(Pgl *g, u32 color) {
    pgl_fill_rect(g, g->clip_x0, g->clip_y0, g->clip_x1 - g->clip_x0, g->clip_y1 - g->clip_y0, color);
}
void pgl_fill_rect(Pgl *g, int x, int y, int w, int h, u32 color) {
    if (w <= 0 || h <= 0) return;
    int x0 = x < g->clip_x0 ? g->clip_x0 : x;
    int y0 = y < g->clip_y0 ? g->clip_y0 : y;
    int x1 = x + w > g->clip_x1 ? g->clip_x1 : x + w;
    int y1 = y + h > g->clip_y1 ? g->clip_y1 : y + h;
    if (x1 <= x0 || y1 <= y0) return;
    for (int j = y0; j < y1; j++) {
        u32 *row = &g->pixels[j * g->stride + x0];
        for (int i = x0; i < x1; i++) row[i - x0] = color;
    }
}
void pgl_stroke_rect(Pgl *g, int x, int y, int w, int h, u32 color) {
    if (w <= 0 || h <= 0) return;
    pgl_fill_rect(g, x, y, w, 1, color);
    pgl_fill_rect(g, x, y + h - 1, w, 1, color);
    pgl_fill_rect(g, x, y, 1, h, color);
    pgl_fill_rect(g, x + w - 1, y, 1, h, color);
}
static void pgl_hline(Pgl *g, int x0, int x1, int y, u32 color) {
    if (y < g->clip_y0 || y >= g->clip_y1) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < g->clip_x0) x0 = g->clip_x0;
    if (x1 > g->clip_x1 - 1) x1 = g->clip_x1 - 1;
    if (x1 < x0) return;
    u32 *row = &g->pixels[y * g->stride];
    for (int x = x0; x <= x1; x++) row[x] = color;
}
void pgl_line(Pgl *g, int x0, int y0, int x1, int y1, u32 color) {
    int dx = x1 - x0, sx = dx < 0 ? -1 : 1;
    if (dx < 0) dx = -dx;
    int dy = y1 - y0, sy = dy < 0 ? -1 : 1;
    if (dy < 0) dy = -dy;
    int err = (dx > dy ? dx : -dy) / 2, e2;
    for (;;) {
        pgl_fill_rect(g, x0, y0, 1, 1, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 < dy) { err += dx; y0 += sy; }
    }
}
void pgl_fill_triangle(Pgl *g, int x0, int y0, int x1, int y1, int x2, int y2, u32 color) {
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    if (minx < g->clip_x0) minx = g->clip_x0;
    if (miny < g->clip_y0) miny = g->clip_y0;
    if (maxx > g->clip_x1 - 1) maxx = g->clip_x1 - 1;
    if (maxy > g->clip_y1 - 1) maxy = g->clip_y1 - 1;
    int area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (area == 0) return;
    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            int w0 = (x1 - x) * (y2 - y) - (y1 - y) * (x2 - x);
            int w1 = (x2 - x) * (y0 - y) - (y2 - y) * (x0 - x);
            int w2 = (x0 - x) * (y1 - y) - (y0 - y) * (x1 - x);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
                g->pixels[y * g->stride + x] = color;
        }
    }
}
void pgl_fill_circle(Pgl *g, int cx, int cy, int r, u32 color) {
    if (r <= 0) return;
    int r2 = r * r;
    for (int y = -r; y <= r; y++) {
        int span = r2 - y * y;
        if (span < 0) continue;
        int dx = 0; while ((dx + 1) * (dx + 1) <= span) dx++;
        pgl_hline(g, cx - dx, cx + dx, cy + y, color);
    }
}
void pgl_stroke_circle(Pgl *g, int cx, int cy, int r, u32 color) {
    if (r <= 0) return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        pgl_fill_rect(g, cx + x, cy + y, 1, 1, color);
        pgl_fill_rect(g, cx + y, cy + x, 1, 1, color);
        pgl_fill_rect(g, cx - y, cy + x, 1, 1, color);
        pgl_fill_rect(g, cx - x, cy + y, 1, 1, color);
        pgl_fill_rect(g, cx - x, cy - y, 1, 1, color);
        pgl_fill_rect(g, cx - y, cy - x, 1, 1, color);
        pgl_fill_rect(g, cx + y, cy - x, 1, 1, color);
        pgl_fill_rect(g, cx + x, cy - y, 1, 1, color);
        y++;
        if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
    }
}
void pgl_blit(Pgl *g, int x, int y, int w, int h, const u8 *rgba, int sw, int sh) {    if (!rgba || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return;
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < g->clip_y0 || yy >= g->clip_y1) continue;
        int sy = (int)((long)j * sh / h);
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < g->clip_x0 || xx >= g->clip_x1) continue;
            int sx = (int)((long)i * sw / w);
            const u8 *p = rgba + 4 * (sy * sw + sx);
            int a = p[3];
            if (!a) continue;
            u32 c = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
            u32 *d = &g->pixels[yy * g->stride + xx];
            if (a >= 255) *d = c;
            else {
                int inv = 255 - a;
                int r = ((((*d >> 16) & 255) * inv + p[0] * a) / 255);
                int gg = ((((*d >> 8) & 255) * inv + p[1] * a) / 255);
                int b = (((*d & 255) * inv + p[2] * a) / 255);
                *d = ((u32)r << 16) | ((u32)gg << 8) | (u32)b;
            }
        }
    }
}
