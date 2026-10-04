#include "media.h"
#include "mem.h"
#include "pmm.h"

/* -------------------------------------------------------------------------
 * Still images: stb_image (PNG/JPEG/BMP/GIF first frame/TGA/PSD/PNM).
 * ------------------------------------------------------------------------- */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS 4096
#define STBI_ASSERT(x) ((void)0)
#define MEDIA_STBI_MAGIC 0x4d494d47u
#define MEDIA_STBI_PMM_THRESHOLD (256u * 1024u)
typedef struct { u32 magic, size, pages, pmm_backed; } MediaStbiBlock;
static void *media_stbi_alloc(u32 size) {
    if (!size) size = 1;
    if (size > 0xfffff000u - (u32)sizeof(MediaStbiBlock)) return 0;
    u32 total = size + (u32)sizeof(MediaStbiBlock);
    MediaStbiBlock *block;
    if (size >= MEDIA_STBI_PMM_THRESHOLD) {
        u32 pages = (total + PMM_PAGE_SIZE - 1u) / PMM_PAGE_SIZE;
        uintptr_t base = pmm_alloc_pages(pages);
        if (!base) return 0;
        block = (MediaStbiBlock *)base;
        block->pages = pages;
        block->pmm_backed = 1;
    } else {
        block = (MediaStbiBlock *)kmalloc(total);
        if (!block) return 0;
        block->pages = 0;
        block->pmm_backed = 0;
    }
    block->magic = MEDIA_STBI_MAGIC;
    block->size = size;
    return block + 1;
}
static void media_stbi_free(void *ptr) {
    if (!ptr) return;
    MediaStbiBlock *block = (MediaStbiBlock *)ptr - 1;
    if (block->magic != MEDIA_STBI_MAGIC) return;
    u32 pages = block->pages;
    int pmm_backed = block->pmm_backed;
    block->magic = 0;
    if (pmm_backed) pmm_free_pages((uintptr_t)block, pages);
    else kfree(block);
}
static void *media_stbi_realloc(void *ptr, u32 size) {
    if (!ptr) return media_stbi_alloc(size);
    if (!size) { media_stbi_free(ptr); return 0; }
    MediaStbiBlock *old = (MediaStbiBlock *)ptr - 1;
    if (old->magic != MEDIA_STBI_MAGIC) return 0;
    void *next = media_stbi_alloc(size);
    if (!next) return 0;
    u32 copy = old->size < size ? old->size : size;
    memcpy(next, ptr, copy);
    media_stbi_free(ptr);
    return next;
}
#define STBI_MALLOC(n) media_stbi_alloc((u32)(n))
#define STBI_REALLOC(p,n) media_stbi_realloc((p),(u32)(n))
#define STBI_FREE(p) media_stbi_free(p)
#define abs(x) ((x) < 0 ? -(x) : (x))
#include "../third_party/stb/stb_image.h"

u8 *media_decode(const u8 *data, u32 len, int *w, int *h) {
    if (!data || len < 8) return 0;
    int iw = 0, ih = 0, c = 0;
    if (!stbi_info_from_memory(data, len, &iw, &ih, &c)) return 0;
    if (iw <= 0 || ih <= 0 || iw > 4096 || ih > 4096 || (u32)iw * (u32)ih > 4194304u) return 0;
    int out_c = 0;
    u8 *px = stbi_load_from_memory(data, len, &iw, &ih, &out_c, 4);
    if (!px) return 0;
    if (w) *w = iw;
    if (h) *h = ih;
    return px;
}
void media_free(void *p) { if (p) stbi_image_free(p); }

/* -------------------------------------------------------------------------
 * Animated GIF: minimal LZW decoder + frame compositor.
 * ------------------------------------------------------------------------- */
typedef struct {
    int x, y, w, h;
    int delay, disposal, transparent, mcs, interlace;
    u8 *pal;
    int pal_size;
    u8 *lzw;
    u32 lzw_len;
} GifFrame;

struct MediaGif {
    const u8 *data;
    u32 len;
    int w, h;
    const u8 *gpal;
    int gpal_size;
    GifFrame *frames;
    int nframes;
    u8 *canvas;
    u8 *saved;
    int idx;
    u32 last_tick;
};

static int gif_lzw(const u8 *in, u32 in_len, u8 *out, int out_len, int mcs) {
    if (mcs < 2 || mcs > 8) return 0;
    static u16 prefix[4096];
    static u8 suffix[4096];
    static u8 stack[4096];
    int clear = 1 << mcs, end = clear + 1;
    int next = end + 1, code_size = mcs + 1;
    for (int i = 0; i < clear; i++) { prefix[i] = 0xFFFF; suffix[i] = (u8)i; }
    int out_pos = 0, prev = -1;
    u32 bitbuf = 0, pos = 0;
    int bitcnt = 0;
    while (out_pos < out_len) {
        while (bitcnt < code_size) {
            if (pos >= in_len) return out_pos;
            bitbuf |= (u32)in[pos++] << bitcnt;
            bitcnt += 8;
        }
        int code = (int)(bitbuf & ((1u << code_size) - 1));
        bitbuf >>= code_size;
        bitcnt -= code_size;
        if (code == clear) { code_size = mcs + 1; next = end + 1; prev = -1; continue; }
        if (code == end) break;
        int cur = code, sp = 0;
        if (code >= next) {
            if (prev < 0 || sp >= 4096) return out_pos;
            stack[sp++] = suffix[prev];
            cur = prev;
        }
        while (cur >= clear) {
            if (sp >= 4096) return out_pos;
            stack[sp++] = suffix[cur];
            cur = prefix[cur];
        }
        stack[sp++] = suffix[cur];
        int first = suffix[cur];
        while (sp > 0 && out_pos < out_len) out[out_pos++] = stack[--sp];
        if (prev >= 0 && next < 4096) {
            prefix[next] = (u16)prev;
            suffix[next] = (u8)first;
            next++;
            if (next == (1 << code_size) && code_size < 12) code_size++;
        }
        prev = code;
    }
    return out_pos;
}

static void gif_blit_frame(MediaGif *g, GifFrame *f) {
    u32 count = (u32)f->w * (u32)f->h;
    u8 *idx = kmalloc(count ? count : 1);
    if (!idx) return;
    memset(idx, 0, count);
    int decoded = gif_lzw(f->lzw, f->lzw_len, idx, (int)count, f->mcs);
    int order[4] = {0, 0, 0, 0};
    if (f->interlace) {
        int starts[4] = {0, 4, 2, 1}, steps[4] = {8, 8, 4, 2};
        for (int pass = 0; pass < 4; pass++) order[pass] = starts[pass];
        int src_row = 0;
        for (int pass = 0; pass < 4; pass++) {
            for (int y = starts[pass]; y < f->h; y += steps[pass]) {
                for (int x = 0; x < f->w; x++) {
                    u32 source = (u32)src_row * f->w + (u32)x;
                    if (source >= (u32)decoded) continue;
                    u8 v = idx[source];
                    int cx = f->x + x, cy = f->y + y;
                    if (cx < 0 || cx >= g->w || cy < 0 || cy >= g->h) continue;
                    if (v == f->transparent || !f->pal || v >= f->pal_size) continue;
                    const u8 *p = f->pal + (int)v * 3;
                    u8 *dst = g->canvas + 4 * ((u32)cy * g->w + cx);
                    dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2]; dst[3] = 255;
                }
                src_row++;
            }
        }
    } else {
        for (int y = 0; y < f->h; y++) {
            int cy = f->y + y;
            if (cy < 0 || cy >= g->h) continue;
            for (int x = 0; x < f->w; x++) {
                int cx = f->x + x;
                if (cx < 0 || cx >= g->w) continue;
                u32 source = (u32)y * f->w + (u32)x;
                if (source >= (u32)decoded) continue;
                u8 v = idx[source];
                if (v == f->transparent || !f->pal || v >= f->pal_size) continue;
                const u8 *p = f->pal + (int)v * 3;
                u8 *dst = g->canvas + 4 * ((u32)cy * g->w + cx);
                dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2]; dst[3] = 255;
            }
        }
    }
    kfree(idx);
}

MediaGif *media_gif_open(const u8 *data, u32 len) {
    if (!data || len < 14 || memcmp(data, "GIF8", 4)) return 0;
    MediaGif *g = kmalloc(sizeof(MediaGif));
    if (!g) return 0;
    memset(g, 0, sizeof(*g));
    g->data = data; g->len = len;
    g->w = data[6] | (data[7] << 8);
    g->h = data[8] | (data[9] << 8);
    u8 packed = data[10];
    if (g->w <= 0 || g->h <= 0 || g->w > 4096 || g->h > 4096 || (u32)g->w * g->h > 4194304u) { kfree(g); return 0; }
    u32 pos = 13;
    if (packed & 0x80) {
        int n = 2 << (packed & 7);
        if (pos + (u32)n * 3 > len) { kfree(g); return 0; }
        g->gpal = data + pos; g->gpal_size = n; pos += (u32)n * 3;
    }
    int cap = 8;
    g->frames = kmalloc((u32)cap * sizeof(GifFrame));
    if (!g->frames) { kfree(g); return 0; }
    int delay = 100, disposal = 0, transparent = -1;
    while (pos < len) {
        u8 b = data[pos++];
        if (b == 0x3B) break;
        if (b == 0x21) {
            if (pos >= len) break;
            u8 label = data[pos++];
            if (label == 0xF9) {
                if (pos >= len) break;
                u8 sz = data[pos++];
                if (sz >= 4 && pos + sz <= len) {
                    u8 p = data[pos];
                    delay = data[pos + 1] | (data[pos + 2] << 8);
                    transparent = (p & 1) ? data[pos + 3] : -1;
                    disposal = (p >> 2) & 7;
                }
                pos += sz;
                while (pos < len && data[pos]) pos += (u32)data[pos] + 1;
                if (pos < len) pos++;
            } else {
                while (pos < len && data[pos]) pos += (u32)data[pos] + 1;
                if (pos < len) pos++;
            }
        } else if (b == 0x2C) {
            if (pos + 9 > len) break;
            int ix = data[pos] | (data[pos + 1] << 8);
            int iy = data[pos + 2] | (data[pos + 3] << 8);
            int iw = data[pos + 4] | (data[pos + 5] << 8);
            int ih = data[pos + 6] | (data[pos + 7] << 8);
            u8 ip = data[pos + 8];
            pos += 9;
            const u8 *pal = g->gpal; int psz = g->gpal_size;
            if (ip & 0x80) {
                int n = 2 << (ip & 7);
                if (pos + (u32)n * 3 > len) break;
                pal = data + pos; psz = n; pos += (u32)n * 3;
            }
            if (!pal || psz < 2 || g->nframes >= 256) break;
            int interlace = (ip & 0x40) ? 1 : 0;
            if (pos >= len) break;
            int mcs = data[pos++];
            u32 total = 0, q = pos;
            int malformed = 0;
            while (q < len && data[q]) {
                u32 block = data[q];
                if (block > len - q - 1 || total > len - block) { malformed = 1; break; }
                total += block;
                q += block + 1;
            }
            if (malformed || q >= len) break;
            u8 *lzw = kmalloc(total ? total : 1);
            if (!lzw) break;
            u32 o = 0; q = pos;
            while (q < len && data[q]) {
                u32 block = data[q];
                if (block > len - q - 1 || block > total - o) { malformed = 1; break; }
                memcpy(lzw + o, data + q + 1, block);
                o += block; q += block + 1;
            }
            if (malformed || q >= len) { kfree(lzw); break; }
            pos = q + 1;
            if (iw <= 0 || ih <= 0 || (u32)iw * ih > 4194304u) { kfree(lzw); continue; }
            u8 *frame_pal = kmalloc((u32)psz * 3u);
            if (!frame_pal) { kfree(lzw); break; }
            memcpy(frame_pal, pal, (u32)psz * 3u);
            if (g->nframes == cap) {
                cap *= 2;
                if (cap > 256) cap = 256;
                GifFrame *nf = krealloc(g->frames, (u32)cap * sizeof(GifFrame));
                if (!nf) { kfree(frame_pal); kfree(lzw); break; }
                g->frames = nf;
            }
            GifFrame *f = &g->frames[g->nframes++];
            f->x = ix; f->y = iy; f->w = iw; f->h = ih;
            f->delay = delay; f->disposal = disposal; f->transparent = transparent;
            f->mcs = mcs; f->interlace = interlace; f->pal = frame_pal; f->pal_size = psz;
            f->lzw = lzw; f->lzw_len = total;
        } else {
            break;
        }
    }
    if (g->nframes == 0) { media_gif_close(g); return 0; }
    g->canvas = kmalloc((u32)g->w * (u32)g->h * 4);
    if (!g->canvas) { media_gif_close(g); return 0; }
    memset(g->canvas, 0, (u32)g->w * (u32)g->h * 4);
    g->saved = 0; g->idx = -1;
    return g;
}

void media_gif_close(MediaGif *g) {
    if (!g) return;
    if (g->frames) {
        for (int i = 0; i < g->nframes; i++) {
            if (g->frames[i].lzw) kfree(g->frames[i].lzw);
            if (g->frames[i].pal) kfree(g->frames[i].pal);
        }
        kfree(g->frames);
    }
    if (g->canvas) kfree(g->canvas);
    if (g->saved) kfree(g->saved);
    kfree(g);
}
int media_gif_width(MediaGif *g) { return g ? g->w : 0; }
int media_gif_height(MediaGif *g) { return g ? g->h : 0; }
int media_gif_animating(MediaGif *g) { return g && g->nframes > 1; }

static u32 gif_delay_ticks(GifFrame *f) {
    u32 centiseconds = f->delay > 0 ? (u32)f->delay : 10u;
    if (centiseconds < 2) centiseconds = 2;
    return (centiseconds * 120u + 99u) / 100u;
}

void media_gif_rewind(MediaGif *g) {
    if (!g) return;
    g->idx = -1;
    if (g->canvas) memset(g->canvas, 0, (u32)g->w * (u32)g->h * 4);
}

const u8 *media_gif_canvas(MediaGif *g, int animate) {    if (!g || !g->canvas) return 0;
    u32 now = ticks;
    if (g->idx < 0) {
        memset(g->canvas, 0, (u32)g->w * (u32)g->h * 4);
        GifFrame *f = &g->frames[0];
        if (f->disposal == 3) {
            if (!g->saved) g->saved = kmalloc((u32)g->w * (u32)g->h * 4);
            if (g->saved) memcpy(g->saved, g->canvas, (u32)g->w * (u32)g->h * 4);
        }
        gif_blit_frame(g, f);
        g->idx = 0;
        g->last_tick = now;
        return g->canvas;
    }
    if (!animate || g->nframes <= 1) return g->canvas;
    GifFrame *cur = &g->frames[g->idx];
    if (now - g->last_tick < gif_delay_ticks(cur)) return g->canvas;
    /* Apply the finished frame's disposal, then draw the next one. */
    if (cur->disposal == 3) {
        if (g->saved) memcpy(g->canvas, g->saved, (u32)g->w * (u32)g->h * 4);
    } else if (cur->disposal == 2) {
        for (int y = 0; y < cur->h; y++) {
            int cy = cur->y + y;
            if (cy < 0 || cy >= g->h) continue;
            for (int x = 0; x < cur->w; x++) {
                int cx = cur->x + x;
                if (cx < 0 || cx >= g->w) continue;
                u8 *dst = g->canvas + 4 * ((u32)cy * g->w + cx);
                dst[0] = dst[1] = dst[2] = dst[3] = 0;
            }
        }
    }
    g->idx = (g->idx + 1) % g->nframes;
    GifFrame *nxt = &g->frames[g->idx];
    if (nxt->disposal == 3) {
        if (!g->saved) g->saved = kmalloc((u32)g->w * (u32)g->h * 4);
        if (g->saved) memcpy(g->saved, g->canvas, (u32)g->w * (u32)g->h * 4);
    }
    gif_blit_frame(g, nxt);
    g->last_tick = now;
    return g->canvas;
}

int media_gif_due(MediaGif *g) {
    if (!g || g->idx < 0 || g->nframes <= 1) return 0;
    return (ticks - g->last_tick) >= gif_delay_ticks(&g->frames[g->idx]);
}

/* -------------------------------------------------------------------------
 * Pollik Video (.pkv): tiny MJPEG container.
 *   "PKV1" | u16 w | u16 h | u16 frames | u16 delay_ms | (u32 off,u32 len)*N
 * ------------------------------------------------------------------------- */
struct MediaClip {
    const u8 *data;
    u32 len;
    int w, h, nframes, delay;
    u32 *off;
    u32 *flen;
    u8 *canvas;
    int idx;
    u32 last_tick;
};

static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static u32 clip_delay_ticks(MediaClip *c) { return ((u32)c->delay * 120u + 999u) / 1000u; }

MediaClip *media_clip_open(const u8 *data, u32 len) {
    if (!data || len < 12 || memcmp(data, "PKV1", 4)) return 0;
    int w = rd16(data + 4), h = rd16(data + 6);
    int n = rd16(data + 8), delay = rd16(data + 10);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || (u32)w * h > 4194304u || n <= 0 || n > 4096) return 0;
    if (12u + (u32)n * 8u > len) return 0;
    MediaClip *c = kmalloc(sizeof(MediaClip));
    if (!c) return 0;
    memset(c, 0, sizeof(*c));
    c->data = data; c->len = len; c->w = w; c->h = h; c->nframes = n;
    c->delay = delay < 20 ? 100 : delay;
    c->off = kmalloc((u32)n * 4);
    c->flen = kmalloc((u32)n * 4);
    c->canvas = kmalloc((u32)w * (u32)h * 4);
    if (!c->off || !c->flen || !c->canvas) { media_clip_close(c); return 0; }
    for (int i = 0; i < n; i++) {
        c->off[i] = rd32(data + 12 + i * 8);
        c->flen[i] = rd32(data + 16 + i * 8);
        if (c->off[i] + c->flen[i] > len) { media_clip_close(c); return 0; }
    }
    memset(c->canvas, 0, (u32)w * (u32)h * 4);
    c->idx = -1;
    return c;
}

void media_clip_close(MediaClip *c) {
    if (!c) return;
    if (c->off) kfree(c->off);
    if (c->flen) kfree(c->flen);
    if (c->canvas) kfree(c->canvas);
    kfree(c);
}
int media_clip_width(MediaClip *c) { return c ? c->w : 0; }
int media_clip_height(MediaClip *c) { return c ? c->h : 0; }
int media_clip_animating(MediaClip *c) { return c && c->nframes > 1; }
void media_clip_rewind(MediaClip *c) { if (c) { c->idx = -1; if (c->canvas) memset(c->canvas, 0, (u32)c->w * c->h * 4); } }

static void clip_decode(MediaClip *c, int fi) {
    int w = 0, h = 0;
    u8 *img = media_decode(c->data + c->off[fi], c->flen[fi], &w, &h);
    if (!img) return;
    for (int y = 0; y < c->h; y++) {
        int sy = (w == c->w) ? y : (y * h / c->h);
        for (int x = 0; x < c->w; x++) {
            int sx = (w == c->w) ? x : (x * w / c->w);
            const u8 *p = img + 4 * ((u32)sy * w + sx);
            u8 *d = c->canvas + 4 * ((u32)y * c->w + x);
            d[0] = p[0]; d[1] = p[1]; d[2] = p[2]; d[3] = 255;
        }
    }
    media_free(img);
}

const u8 *media_clip_canvas(MediaClip *c, int animate) {
    if (!c || !c->canvas) return 0;
    u32 now = ticks;
    if (c->idx < 0) { clip_decode(c, 0); c->idx = 0; c->last_tick = now; return c->canvas; }
    if (!animate || c->nframes <= 1) return c->canvas;
    if (now - c->last_tick < clip_delay_ticks(c)) return c->canvas;
    c->idx = (c->idx + 1) % c->nframes;
    clip_decode(c, c->idx);
    c->last_tick = now;
    return c->canvas;
}

int media_clip_due(MediaClip *c) {
    if (!c || c->idx < 0 || c->nframes <= 1) return 0;
    return (ticks - c->last_tick) >= clip_delay_ticks(c);
}
