#ifndef POLLIK_APP_INTERNAL_H
#define POLLIK_APP_INTERNAL_H
#include "app_host.h"
#include "apps.h"
/* Short drawing names retain the original client painting expressions. */
#define rect ui_bridge_rect
#define roundrect ui_bridge_roundrect
#define roundrect_border ui_bridge_roundrect_border
#define rounded ui_bridge_rounded
#define text ui_bridge_text
#define centered app_draw_centered
#define mono app_draw_mono
#define letter app_draw_letter
static inline int len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static inline void copy(char *d, const char *s) { while ((*d++ = *s++)) {} }
static inline int eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static inline void append_str(char *dest, const char *src, int max_len) {
    int i = 0;
    while (dest[i] && i < max_len - 1) i++;
    while (*src && i < max_len - 1) dest[i++] = *src++;
    dest[i] = 0;
}
typedef struct { int x, y, w, h; } AppRect;
static inline int app_hit(AppRect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
/* Small allocation-free text helpers; single-line labels truncate, boxes wrap. */
static inline void app_label(int x, int y, int w, const char *s, u32 c, int scale) {
    int end = x + w;
    while (*s && *s != '\n') {
        u8 ch = *s++;
        if (ch < 32 || ch >= 127) continue;
        int advance = sys_get_glyph_advance(ch, scale);
        if (x + advance > end) break;
        letter(x, y, ch, c, scale); x += advance;
    }
}
static inline void app_text_box(AppRect r, const char *s, u32 c, int scale, int line_h) {
    int x = r.x, y = r.y;
    while (*s && y + line_h <= r.y + r.h) {
        u8 ch = *s++;
        if (ch == '\n') { x = r.x; y += line_h; continue; }
        if (ch < 32 || ch >= 127) continue;
        int advance = sys_get_glyph_advance(ch, scale);
        if (x + advance > r.x + r.w) { x = r.x; y += line_h; }
        if (y + line_h > r.y + r.h) break;
        letter(x, y, ch, c, scale); x += advance;
    }
}
/* In-kernel clipboard shared by Notes and Terminal; this is not the host OS
 * clipboard. */
void app_clipboard_copy(const char *text, int length);
int app_clipboard_paste(char *out, int capacity);
/* Shared Notes/Terminal append-only editing. Return 1 when the original
 * handler would mark a note changed (even empty backspace). */
int app_edit_key(char *buf, int *n, int max, u8 code, char ch, int control);
#endif
