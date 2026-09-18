#include "shell_internal.h"
#include "pmm.h"
#include "klog.h"
#include "ui_data.h"

static u32 *pixels, *wallpaper;
static int wallpaper_theme = -1;
#define DOCK_CACHE_WIDTH (NUM_APPS * 68 + 144)
static u32 dock_background[DOCK_CACHE_WIDTH * 145];
static int dock_background_valid;
static int dirty_client[NUM_APPS] = {1, 1, 1, 1, 1, 1};
static int surface_active_state[NUM_APPS] = {-1, -1, -1, -1, -1, -1};
static int surface_w[NUM_APPS], surface_h[NUM_APPS];
static int minimizing_app = -1;
static u32 minimize_start;
static int minimize_shadow_active, minimize_shadow_normal;
static int cursor_previous_x, cursor_previous_y, cursor_previous_kind, cursor_previous_valid;
static u32 perf_paint_us, perf_present_us;
static void perf_begin(void) {
    wm_perf_frame_begin();
    perf_paint_us = perf_present_us = 0;
    g_perf_stats.composed_pixels = g_perf_stats.effective_rects = 0;
}

/* Half-open visual bounds include the outer shadow's +14 bottom extent.
 * Old geometry is conservatively treated as normal across state changes. */
static GraphicsClip window_visual_bounds(int x, int y, int w, int h, int shadow) {
    return (GraphicsClip){x - (shadow ? 7 : 0), y,
                          x + w + (shadow ? 7 : 0), y + h + (shadow ? 14 : 0)};
}
static GraphicsClip clip_union(GraphicsClip a, GraphicsClip b) {
    return (GraphicsClip){a.x1 < b.x1 ? a.x1 : b.x1, a.y1 < b.y1 ? a.y1 : b.y1,
                          a.x2 > b.x2 ? a.x2 : b.x2, a.y2 > b.y2 ? a.y2 : b.y2};
}
static GraphicsClip clip_intersection(GraphicsClip a, GraphicsClip b) {
    return (GraphicsClip){a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1,
                          a.x2 < b.x2 ? a.x2 : b.x2, a.y2 < b.y2 ? a.y2 : b.y2};
}

void compositor_invalidate(int id) {
    if (id >= 0 && id < NUM_APPS) dirty_client[id] = 1;
}
void compositor_minimize(int id) {
    Window *w = wm_get_window(id);
    minimizing_app = -1;
    /* Only a completed, correctly sized cache can become a ghost. Never
     * repaint a minimized client or animate its authoritative WM geometry. */
    if (shell.animations_mode && g_surfaces[id].pixels && surface_active_state[id] >= 0 &&
        surface_w[id] == w->width && surface_h[id] == w->height) {
        minimizing_app = id;
        minimize_start = wm_time_ms();
        minimize_shadow_active = surface_active_state[id];
        minimize_shadow_normal = w->state == WINDOW_STATE_NORMAL;
    }
}
void compositor_cancel_minimize(int id) {
    if (id < 0 || minimizing_app == id) minimizing_app = -1;
}
int compositor_minimizing(int id) { return minimizing_app == id; }
int compositor_animate(u32 now) {
    if (minimizing_app >= 0 && (!shell.animations_mode || now - minimize_start >= MINIMIZE_MS)) {
        minimizing_app = -1; /* WM minimized instantly; only the cached ghost expires. */
        request_scene_redraw();
    }
    return shell.animations_mode && minimizing_app >= 0;
}
static void set_target(u32 *buf, int w, int h, int stride) { set_draw_target(buf, w, h, stride); }
static int render_window_to_surface(int id, int is_active) {
    int ww = window_width(id), wh = window_height(id);
    /* wm_init() allocates screen width * height pixels per surface.
     * Validate against that capacity, not the obsolete 2560-wide limit. */
    if (ww <= 0 || wh <= 0 || ww > shell.width || wh > shell.height) return 0;
    u32 *buf = g_surfaces[id].pixels;
    if (!buf) return 0;
    u64 paint_start = wm_time_us();
    if (surface_w[id] != ww || surface_h[id] != wh)
        gui_app_resized(id, ww, wh);
    g_surfaces[id].width = ww;
    g_surfaces[id].height = wh;
    g_surfaces[id].stride = ww;
    g_surfaces[id].stride_pixels = ww;
    GraphicsClip scene_clip = graphics_get_clip();
    set_target(buf, ww, wh, ww);
    const GuiApp *app = gui_app_get(id);
    u32 body = is_active ? app->body_active : app->body_inactive;
    u32 tb_bg = is_active ? 0xf2eff6 : 0xe8e4ed;
    u32 sep_col = is_active ? 0xded8e7 : 0xdcd6e4;
    u32 border_col = is_active ? 0xcbc4d8 : 0xdad4e2;
    int wx = 0, wy = 0;
    /* Cache straight RGB over the entire frame, including the exterior.
     * Geometry alpha is applied exactly once over the real scene below. */
    rect(wx, wy, ww, wh, body);
    rect(wx, wy, ww, 34, tb_bg);
    rect(wx + 1, wy + 33, ww - 2, 1, sep_col);
    rect(wx, wy + 12, 1, wh - 24, border_col);
    rect(wx + ww - 1, wy + 12, 1, wh - 24, border_col);
    rect(wx + 12, wy, ww - 24, 1, border_col);
    rect(wx + 12, wy + wh - 1, ww - 24, 1, border_col);
    u32 c_close = is_active ? 0xef5350 : 0xa83e3c;
    u32 c_min = is_active ? 0xfb8c00 : 0xad620a;
    u32 c_max = is_active ? 0x43a047 : 0x327537;
    roundrect(wx + 12, wy + 11, 12, 12, 3, c_close);
    roundrect(wx + 30, wy + 11, 12, 12, 3, c_min);
    roundrect(wx + 48, wy + 11, 12, 12, 3, c_max);
    if (is_active && g_hovered_window == id && shell.hovered_btn > 0) {
        if (shell.hovered_btn == 1) {
            u32 x_col = 0x5a0003;
            for (int d = 0; d < 6; d++) {
                rect(wx + 15 + d, wy + 14 + d, 1, 1, x_col);
                rect(wx + 20 - d, wy + 14 + d, 1, 1, x_col);
            }
        } else if (shell.hovered_btn == 2) {
            rect(wx + 33, wy + 16, 6, 2, 0x5b3c00);
        } else if (windows[id].state == WINDOW_STATE_MAXIMIZED || windows[id].state == WINDOW_STATE_SNAPPED) {
            rect(wx + 52, wy + 13, 5, 4, 0x004d11);
            rect(wx + 53, wy + 14, 3, 2, 0x43a047);
            rect(wx + 49, wy + 16, 5, 4, 0x004d11);
            rect(wx + 50, wy + 17, 3, 2, 0x43a047);
        } else {
            rect(wx + 51, wy + 14, 3, 1, 0x004d11);
            rect(wx + 51, wy + 15, 1, 2, 0x004d11);
            rect(wx + 55, wy + 18, 3, 1, 0x004d11);
            rect(wx + 57, wy + 16, 1, 2, 0x004d11);
        }
    }
    text(wx + 70, wy + 11, app->name, is_active ? 0x241e30 : 0x625b70, 1);
    gui_app_render(id, ww, wh, is_active);
    set_target(pixels, shell.width, shell.height, shell.width);
    graphics_set_clip(scene_clip);
    dirty_client[id] = 0;
    surface_active_state[id] = is_active;
    surface_w[id] = ww;
    surface_h[id] = wh;
    perf_paint_us += (u32)(wm_time_us() - paint_start);
    wm_perf_record_client_paint();
    return 1;
}
static void draw_window_shadow(int wx, int wy, int ww, int wh, int is_active) {
    int op1 = is_active ? 40 : 20, op2 = is_active ? 22 : 10, op3 = is_active ? 10 : 5;
    GraphicsClip saved = graphics_get_clip();
    GraphicsClip bounds = window_visual_bounds(wx, wy, ww, wh, 1);
    /* Disjoint bands retain shadow behind transparent rounded corners, but
     * never blend the large opaque client interior that the blit replaces. */
    int corner = wh < 24 ? wh / 2 : 12;
    GraphicsClip bands[4] = {
        {bounds.x1, bounds.y1, bounds.x2, wy + corner},
        {bounds.x1, wy + wh - corner, bounds.x2, bounds.y2},
        {bounds.x1, wy + corner, wx, wy + wh - corner},
        {wx + ww, wy + corner, bounds.x2, wy + wh - corner}
    };
    for (int i = 0; i < 4; i++) {
        GraphicsClip clip = clip_intersection(saved, bands[i]);
        if (clip.x1 >= clip.x2 || clip.y1 >= clip.y2) continue;
        graphics_set_clip(clip);
        rounded(wx - 2, wy + 2, ww + 4, wh + 3, 14, 0x181024, op1);
        rounded(wx - 4, wy + 3, ww + 8, wh + 6, 16, 0x181024, op2);
        rounded(wx - 7, wy + 5, ww + 14, wh + 9, 18, 0x181024, op3);
    }
    graphics_set_clip(saved);
}
static void compose_window_surface(int id, int is_active) {
    int wx = windows[id].x, wy = windows[id].y;
    int ww = window_width(id), wh = window_height(id);
    if (ww <= 0 || wh <= 0) return;
    /* A minimizing ghost uses only the last painted cache, including its
     * old active appearance. Translation never changes Window geometry. */
    if (id == minimizing_app) {
        if (!g_surfaces[id].pixels || surface_w[id] != ww || surface_h[id] != wh) return;
        u32 elapsed = wm_time_ms() - minimize_start;
        if (elapsed > MINIMIZE_MS) elapsed = MINIMIZE_MS;
        wy += (int)((u32)(shell.height - wy) * elapsed / MINIMIZE_MS);
    } else if (dirty_client[id] || surface_active_state[id] != is_active ||
               surface_w[id] != ww || surface_h[id] != wh) {
        if (!render_window_to_surface(id, is_active)) return;
    }
    if (id == minimizing_app ? minimize_shadow_normal : windows[id].state == WINDOW_STATE_NORMAL)
        draw_window_shadow(wx, wy, ww, wh, id == minimizing_app ? minimize_shadow_active : is_active);
    u32 *src = g_surfaces[id].pixels;
    int src_stride = g_surfaces[id].stride;
    if (src_stride <= 0) src_stride = ww;
    int x1 = wx < 0 ? 0 : wx, x2 = wx + ww > shell.width ? shell.width : wx + ww;
    int y1 = wy < 0 ? 0 : wy, y2 = wy + wh > shell.height ? shell.height : wy + wh;
    GraphicsClip clip = graphics_get_clip();
    if (x1 < clip.x1) x1 = clip.x1;
    if (x2 > clip.x2) x2 = clip.x2;
    if (y1 < clip.y1) y1 = clip.y1;
    if (y2 > clip.y2) y2 = clip.y2;
    if (x2 <= x1 || y2 <= y1) return;
    int span = x2 - x1, src_x_offset = x1 - wx;
    int radius = 12;
    if (radius > ww / 2) radius = ww / 2;
    if (radius > wh / 2) radius = wh / 2;
    for (int y = y1; y < y2; y++) {
        int local_y = y - wy;
        u32 *dst = &pixels[y * shell.width + x1];
        u32 *s = &src[local_y * src_stride + src_x_offset];
        if (local_y >= radius && local_y < wh - radius) {
            memcpy(dst, s, (u32)span * sizeof(u32));
        } else {
            int cy = local_y < radius ? local_y : wh - 1 - local_y;
            for (int i = 0; i < span; i++) {
                int cx = src_x_offset + i;
                if (cx >= ww - radius) cx = ww - 1 - cx;
                int coverage = cx < radius ? graphics_corner_coverage(radius, cx, cy) : 64;
                if (coverage == 64) dst[i] = s[i];
                else if (coverage) dst[i] = blend(dst[i], s[i], coverage * 4);
            }
        }
    }
}
static void draw_snap_preview(void) {
    if (shell.snap_preview == SNAP_NONE) return;
    Rect r;
    wm_get_snap_bounds(shell.snap_preview, &r);
    if (r.w <= 0 || r.h <= 0) return;
    rounded(r.x, r.y, r.w, r.h, 14, 0x8472cc, 50);
    rounded(r.x + 3, r.y + 3, r.w - 6, r.h - 6, 11, 0xffffff, 35);
}
static inline void set_cursor_px(int x, int y, u32 col) {
    rect(x, y, 1, 1, col);
}
static void draw_resize_cursor(int cx, int cy, int kind) {
    u32 c_out = 0x181222, c_in = 0xffffff;
    int ox = cx + 8, oy = cy + 8;
    if (kind == CURSOR_RESIZE_H) {
        for (int x = -5; x <= 5; x++) {
            set_cursor_px(ox + x, oy - 1, c_out);
            set_cursor_px(ox + x, oy, c_in);
            set_cursor_px(ox + x, oy + 1, c_out);
        }
        for (int i = 0; i <= 4; i++) {
            int x = -5 - i;
            for (int y = -i; y <= i; y++) {
                u32 c = (i == 4 || y == -i || y == i) ? c_out : c_in;
                set_cursor_px(ox + x, oy + y, c);
            }
        }
        for (int i = 0; i <= 4; i++) {
            int x = 5 + i;
            for (int y = -i; y <= i; y++) {
                u32 c = (i == 4 || y == -i || y == i) ? c_out : c_in;
                set_cursor_px(ox + x, oy + y, c);
            }
        }
    } else if (kind == CURSOR_RESIZE_V) {
        for (int y = -5; y <= 5; y++) {
            set_cursor_px(ox - 1, oy + y, c_out);
            set_cursor_px(ox, oy + y, c_in);
            set_cursor_px(ox + 1, oy + y, c_out);
        }
        for (int i = 0; i <= 4; i++) {
            int y = -5 - i;
            for (int x = -i; x <= i; x++) {
                u32 c = (i == 4 || x == -i || x == i) ? c_out : c_in;
                set_cursor_px(ox + x, oy + y, c);
            }
        }
        for (int i = 0; i <= 4; i++) {
            int y = 5 + i;
            for (int x = -i; x <= i; x++) {
                u32 c = (i == 4 || x == -i || x == i) ? c_out : c_in;
                set_cursor_px(ox + x, oy + y, c);
            }
        }
    } else if (kind == CURSOR_RESIZE_NWSE) {
        for (int d = -5; d <= 5; d++) {
            set_cursor_px(ox + d - 1, oy + d, c_out);
            set_cursor_px(ox + d + 1, oy + d, c_out);
            set_cursor_px(ox + d, oy + d, c_in);
        }
        for (int i = 0; i <= 4; i++) {
            set_cursor_px(ox - 5 - i, oy - 5, c_out);
            set_cursor_px(ox - 5, oy - 5 - i, c_out);
            if (i > 0) {
                set_cursor_px(ox - 5 - i, oy - 5 - 1, c_in);
                set_cursor_px(ox - 5 - 1, oy - 5 - i, c_in);
            }
            set_cursor_px(ox - 5 - i, oy - 5 - i, c_out);
        }
        for (int i = 0; i <= 4; i++) {
            set_cursor_px(ox + 5 + i, oy + 5, c_out);
            set_cursor_px(ox + 5, oy + 5 + i, c_out);
            if (i > 0) {
                set_cursor_px(ox + 5 + i, oy + 5 + 1, c_in);
                set_cursor_px(ox + 5 + 1, oy + 5 + i, c_in);
            }
            set_cursor_px(ox + 5 + i, oy + 5 + i, c_out);
        }
    } else if (kind == CURSOR_RESIZE_NESW) {
        for (int d = -5; d <= 5; d++) {
            set_cursor_px(ox - d - 1, oy + d, c_out);
            set_cursor_px(ox - d + 1, oy + d, c_out);
            set_cursor_px(ox - d, oy + d, c_in);
        }
        for (int i = 0; i <= 4; i++) {
            set_cursor_px(ox + 5 + i, oy - 5, c_out);
            set_cursor_px(ox + 5, oy - 5 - i, c_out);
            if (i > 0) {
                set_cursor_px(ox + 5 + i, oy - 5 - 1, c_in);
                set_cursor_px(ox + 5 - 1, oy - 5 - i, c_in);
            }
            set_cursor_px(ox + 5 + i, oy - 5 + i, c_out);
        }
        for (int i = 0; i <= 4; i++) {
            set_cursor_px(ox - 5 - i, oy + 5, c_out);
            set_cursor_px(ox - 5, oy + 5 + i, c_out);
            if (i > 0) {
                set_cursor_px(ox - 5 - i, oy + 5 + 1, c_in);
                set_cursor_px(ox - 5 + 1, oy + 5 + i, c_in);
            }
            set_cursor_px(ox - 5 - i, oy + 5 - i, c_out);
        }
    }
}
/* Compact antialiased arrow: dark keyline, ivory face and a soft offset
 * shadow. Coordinates are quarter-pixels; the hotspot remains (mx, my). */
static int arrow_inside(int x, int y, int inset) {
    static const int outer[][2] = {{0,0},{0,88},{23,67},{40,103},{54,96},{37,61},{68,61}};
    static const int inner[][2] = {{5,11},{5,76},{25,59},{43,97},{48,94},{30,56},{55,56}};
    const int (*p)[2] = inset ? inner : outer;
    int inside = 0;
    for (int i = 0, j = 6; i < 7; j = i++) {
        if ((p[i][1] > y) != (p[j][1] > y)) {
            int cross = (p[j][0] - p[i][0]) * (y - p[i][1]) -
                        (x - p[i][0]) * (p[j][1] - p[i][1]);
            if ((p[j][1] > p[i][1]) ? cross > 0 : cross < 0) inside = !inside;
        }
    }
    return inside;
}
static void draw_arrow_cursor(int mx, int my) {
    GraphicsClip clip = graphics_get_clip();
    for (int y = 0; y < 29; y++) for (int x = 0; x < 20; x++) {
        int edge = 0, face = 0, shadow = 0;
        for (int sy = 1; sy <= 3; sy += 2) for (int sx = 1; sx <= 3; sx += 2) {
            edge += arrow_inside(x * 4 + sx, y * 4 + sy, 0);
            face += arrow_inside(x * 4 + sx, y * 4 + sy, 1);
            shadow += arrow_inside(x * 4 + sx - 4, y * 4 + sy - 8, 0);
        }
        int xx = mx + x, yy = my + y;
        if (xx < clip.x1 || xx >= clip.x2 || yy < clip.y1 || yy >= clip.y2) continue;
        u32 *p = &pixels[yy * shell.width + xx];
        if (shadow) *p = blend(*p, 0x181222, shadow * 18);
        if (edge) *p = blend(*p, 0x211a30, edge * 64);
        if (face) *p = blend(*p, 0xfffdf8, face * 64);
    }
}
#define CURSOR_SAVE_W 34
#define CURSOR_SAVE_H 38
static GraphicsClip cursor_bounds(int x, int y) {
    /* Resize arrows reach one pixel above/left of their nominal origin. */
    return (GraphicsClip){x - 2, y - 2, x + 32, y + 36};
}
static u32 present_with_cursor(GraphicsClip damage, int scene_changed) {
    int mx = input_pointer_x(), my = input_pointer_y(), kind = input_cursor_kind();
    GraphicsClip screen = {0, 0, shell.width, shell.height};
    GraphicsClip current = cursor_bounds(mx, my);
    GraphicsClip cursor_clip = clip_intersection(current, screen);
    int moved = !cursor_previous_valid || mx != cursor_previous_x || my != cursor_previous_y ||
                kind != cursor_previous_kind;
    if (!scene_changed) damage = current;
    if (moved || !scene_changed) {
        damage = clip_union(damage, current);
        if (cursor_previous_valid)
            damage = clip_union(damage, cursor_bounds(cursor_previous_x, cursor_previous_y));
    }
    damage = clip_intersection(damage, screen);
    if (damage.x1 >= damage.x2 || damage.y1 >= damage.y2) return 0;
    GraphicsClip saved_clip = graphics_get_clip();
    u32 saved[CURSOR_SAVE_W * CURSOR_SAVE_H];
    int cw = cursor_clip.x2 - cursor_clip.x1;
    if (cw > 0) for (int y = cursor_clip.y1; y < cursor_clip.y2; y++)
        memcpy(saved + (y - cursor_clip.y1) * CURSOR_SAVE_W,
               pixels + y * shell.width + cursor_clip.x1, (u32)cw * sizeof(u32));
    graphics_set_clip(screen);
    if (kind == CURSOR_DEFAULT) draw_arrow_cursor(mx, my);
    else if (kind == CURSOR_IBEAM || kind == CURSOR_POINTER)
        sprite(mx, my, 32, 36, cursors_index[kind], cursors_alpha[kind], cursors_palette[kind], 32, 36);
    else draw_resize_cursor(mx, my, kind);
    /* One transfer includes the new cursor and restores its old footprint.
     * This is not an atomic flip or a guarantee against scanout tearing. */
    u64 present_start = wm_time_us();
    framebuffer_present(pixels, damage.x1, damage.y1, damage.x2 - damage.x1, damage.y2 - damage.y1);
    perf_present_us += (u32)(wm_time_us() - present_start);
    /* Composed pixels count processed clip areas (scene plus cursor), not the
     * often much larger bounding framebuffer transfer. Includes overdraw. */
    if (cw > 0 && cursor_clip.y2 > cursor_clip.y1) {
        g_perf_stats.composed_pixels += (u32)cw * (cursor_clip.y2 - cursor_clip.y1);
        g_perf_stats.effective_rects++;
    }
    if (cw > 0) for (int y = cursor_clip.y1; y < cursor_clip.y2; y++)
        memcpy(pixels + y * shell.width + cursor_clip.x1,
               saved + (y - cursor_clip.y1) * CURSOR_SAVE_W, (u32)cw * sizeof(u32));
    graphics_set_clip(saved_clip);
    cursor_previous_x = mx;
    cursor_previous_y = my;
    cursor_previous_kind = kind;
    cursor_previous_valid = 1;
    return (u32)(damage.x2 - damage.x1) * (u32)(damage.y2 - damage.y1);
}
void compositor_draw_cursor(int full) {
    if (!full && cursor_previous_valid && cursor_previous_x == input_pointer_x() &&
        cursor_previous_y == input_pointer_y() && cursor_previous_kind == input_cursor_kind()) return;
    perf_begin();
    u32 count = present_with_cursor((GraphicsClip){0, 0, 0, 0}, 0);
    if (count) g_perf_stats.cursor_frames++;
    wm_perf_frame_end(0, perf_paint_us, perf_present_us, 0, count);
}
void compositor_paint(int full) {
    perf_begin();
    int width = shell.width, height = shell.height;
    if (shell.scene_dirty || shell.snap_preview != SNAP_NONE || shell.alttab_open || minimizing_app >= 0) full = 1;
    int dock_x = (width - DOCK_CACHE_WIDTH) / 2, dock_w = DOCK_CACHE_WIDTH, dock_y = height - 145, dock_h = 145;
    if (dock_x < 0) dock_x = 0;
    if (dock_x + dock_w > width) dock_w = width - dock_x;
    if (!full && dock_background_valid && wallpaper_theme == shell.theme &&
        !g_active_menu.active && !g_active_dialog.active) {
        GraphicsClip dock_clip = {dock_x, dock_y, dock_x + dock_w, dock_y + dock_h};
        graphics_set_clip(dock_clip);
        for (int j = 0; j < dock_h; j++)
            memcpy(pixels + (dock_y + j) * width + dock_x, dock_background + j * DOCK_CACHE_WIDTH, (u32)dock_w * 4);
        dock_draw_content();
        ui_draw_notifications(wm_time_ms());
        desktop_draw_overlays();
        g_perf_stats.composed_pixels = (u32)dock_w * dock_h;
        g_perf_stats.effective_rects = 1;
        u32 count = present_with_cursor(dock_clip, 1);
        graphics_set_clip((GraphicsClip){0, 0, width, height});
        if (count) g_perf_stats.dock_frames++;
        wm_perf_frame_end(0, perf_paint_us, perf_present_us, 0, count);
        return;
    }
    if (!full) full = 1;
    if (wallpaper_theme != shell.theme) {
        desktop_paint_wallpaper(wallpaper);
        wallpaper_theme = shell.theme;
        full = 1;
    }
    int ux1 = 0, uy1 = 0, ux2 = width, uy2 = height;
    if (full == 2) {
        int target = shell.drag_app >= 0 ? shell.drag_app : shell.resizing;
        if (target >= 0) {
            int cur_x = windows[target].x, cur_y = windows[target].y;
            int cur_w = window_width(target), cur_h = window_height(target);
            GraphicsClip old = window_visual_bounds(shell.drag_old_x, shell.drag_old_y,
                                                     shell.drag_old_w, shell.drag_old_h, 1);
            GraphicsClip current = window_visual_bounds(cur_x, cur_y, cur_w, cur_h,
                                                        windows[target].state == WINDOW_STATE_NORMAL);
            GraphicsClip damage = clip_union(old, current);
            ux1 = damage.x1; uy1 = damage.y1; ux2 = damage.x2; uy2 = damage.y2;
            if (ux1 < 0) ux1 = 0;
            if (uy1 < 0) uy1 = 0;
            if (ux2 > width) ux2 = width;
            if (uy2 > height) uy2 = height;
            if (ux1 >= ux2 || uy1 >= uy2) return;
        } else {
            full = 1;
        }
    }
    graphics_set_clip((GraphicsClip){ux1, uy1, ux2, uy2});
    if (full == 2) {
        int uw = ux2 - ux1;
        for (int y = uy1; y < uy2; y++)
            memcpy(pixels + y * width + ux1, wallpaper + y * width + ux1, (u32)uw * 4);
    } else {
        u32 *to = pixels, *from = wallpaper;
        u32 count = (u32)width * height;
        __asm__ volatile("cld; rep movsl" : "+D"(to), "+S"(from), "+c"(count) :: "memory");
    }
    int top = active_app();
    if (full == 1 || uy1 < 32) desktop_draw_bar();
    for (int z = 0; z < NUM_APPS; z++) {
        int id = z_order[z];
        if (!windows[id].open) continue;
        if (windows[id].minimized && id != minimizing_app) continue;
        GraphicsClip visual = window_visual_bounds(windows[id].x, windows[id].y,
                                                    window_width(id), window_height(id),
                                                    windows[id].state == WINDOW_STATE_NORMAL);
        if (full != 2 || (visual.x2 > ux1 && visual.x1 < ux2 &&
                          visual.y2 > uy1 && visual.y1 < uy2))
            compose_window_surface(id, id == top);
    }
    draw_snap_preview();
    if (full == 1 || uy2 > height - 145) {
        dock_draw_pill();
        /* Untouched cache pixels must remain pre-icon/pre-overlay pixels,
         * not be overwritten with the already-composited scene. */
        GraphicsClip cache_clip = clip_intersection(graphics_get_clip(),
            (GraphicsClip){dock_x, dock_y, dock_x + dock_w, dock_y + dock_h});
        if (cache_clip.x2 > cache_clip.x1) for (int y = cache_clip.y1; y < cache_clip.y2; y++)
            memcpy(dock_background + (y - dock_y) * DOCK_CACHE_WIDTH + cache_clip.x1 - dock_x,
                   pixels + y * width + cache_clip.x1, (u32)(cache_clip.x2 - cache_clip.x1) * 4);
        if (full == 1) dock_background_valid = 1;
        dock_draw_content();
    }
    if (g_active_dialog.active) ui_draw_dialog();
    if (g_active_menu.active) ui_draw_menu(&g_active_menu);
    ui_draw_notifications(wm_time_ms());
    desktop_draw_overlays();
    g_perf_stats.composed_pixels = (u32)(ux2 - ux1) * (uy2 - uy1);
    g_perf_stats.effective_rects = 1;
    u32 pix_pres = present_with_cursor((GraphicsClip){ux1, uy1, ux2, uy2}, 1);
    graphics_set_clip((GraphicsClip){0, 0, width, height});
    if (full == 2) {
        int target = shell.drag_app >= 0 ? shell.drag_app : shell.resizing;
        if (target >= 0) {
            shell.drag_old_x = windows[target].x;
            shell.drag_old_y = windows[target].y;
            shell.drag_old_w = window_width(target);
            shell.drag_old_h = window_height(target);
        }
    } else {
        int target = shell.drag_app >= 0 ? shell.drag_app : shell.resizing >= 0 ? shell.resizing : active_app();
        if (target >= 0) {
            shell.drag_old_x = windows[target].x;
            shell.drag_old_y = windows[target].y;
            shell.drag_old_w = window_width(target);
            shell.drag_old_h = window_height(target);
        }
    }
    wm_perf_frame_end(0, perf_paint_us, perf_present_us, full == 1, pix_pres);
    wm_check_canaries();
    shell.dirty = 0;
    shell.scene_dirty = 0;
}
void compositor_init(void) {
    extern int framebuffer_width(void), framebuffer_height(void);
    int width = framebuffer_width(), height = framebuffer_height();
    u64 scene_bytes = (u64)(u32)width * (u32)height * sizeof(u32);
    if (width <= 0 || height <= 0 || scene_bytes > 0xFFFFFFFFu - (PMM_PAGE_SIZE - 1u)) {
        serial("GFX invalid software buffer dimensions\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    u32 scene_pages = ((u32)scene_bytes + PMM_PAGE_SIZE - 1u) / PMM_PAGE_SIZE;
    pixels = (u32 *)pmm_alloc_pages(scene_pages);
    if (pixels) wallpaper = (u32 *)pmm_alloc_pages(scene_pages);
    if (!pixels || !wallpaper) {
        if (pixels) pmm_free_pages((uintptr_t)pixels, scene_pages);
        pixels = 0;
        serial("GFX insufficient RAM for scene and wallpaper\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    memset(pixels, 0, scene_pages * PMM_PAGE_SIZE);
    memset(wallpaper, 0, scene_pages * PMM_PAGE_SIZE);
    set_target(pixels, width, height, width);
    klog_dec(KLOG_CAT_GUI, "Scene and wallpaper allocated bytes: ", scene_pages * PMM_PAGE_SIZE * 2u);
}
