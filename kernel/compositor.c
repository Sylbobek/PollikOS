#include "shell_internal.h"
#include "graphics.h"
#include "ui.h"
#include "pmm.h"
#include "mem.h"
#include "klog.h"
#include "ui_data.h"
#include "desktop_items.h"
#include "auth.h"
#include "ui_animation.h"
#include "trash.h"
#include "cursor_sprites.h"

static u32 *pixels, *wallpaper;
static int wallpaper_theme = -1;
#define DOCK_CACHE_WIDTH (NUM_APPS * 68 + 144)
static u32 dock_background[DOCK_CACHE_WIDTH * 145];
static int dock_background_valid;
static int dirty_client[NUM_APPS] = {1, 1, 1, 1, 1, 1, 1};
static int surface_active_state[NUM_APPS] = {-1, -1, -1, -1, -1, -1, -1};
static int surface_theme_state[NUM_APPS] = {-1, -1, -1, -1, -1, -1, -1};
static int surface_w[NUM_APPS], surface_h[NUM_APPS];
static int cursor_previous_x, cursor_previous_y, cursor_previous_kind, cursor_previous_valid;
static u32 perf_paint_us, perf_present_us;
#define COMPOSITOR_DAMAGE_CAP 24
static GraphicsClip g_damage[COMPOSITOR_DAMAGE_CAP];
static int g_damage_count;
static GraphicsClip client_damage[NUM_APPS];
static int client_damage_valid[NUM_APPS];
#define DOCK_GLASS_BLUR_RADIUS 3
enum { DOCK_GLASS_BLUR_ROWS = DOCK_GLASS_BLUR_RADIUS * 2 + 1 };
static u32 dock_glass_blur_rows[DOCK_GLASS_BLUR_ROWS][DOCK_CACHE_WIDTH];
static u32 dock_glass_next_blur_row[DOCK_CACHE_WIDTH];
static int dock_glass_vertical_sum[3][DOCK_CACHE_WIDTH];

static void dock_glass_blur_row(int source_y, int x1, int x2, int width, int height,
                                u32 *out) {
    int red = 0, green = 0, blue = 0;
    if (source_y < 0) source_y = 0;
    if (source_y >= height) source_y = height - 1;
    for (int k = -DOCK_GLASS_BLUR_RADIUS; k <= DOCK_GLASS_BLUR_RADIUS; k++) {
        int sx = x1 + k;
        if (sx < 0) sx = 0;
        if (sx >= width) sx = width - 1;
        u32 color = pixels[source_y * width + sx];
        red += (color >> 16) & 255;
        green += (color >> 8) & 255;
        blue += color & 255;
    }
    for (int x = x1; x < x2; x++) {
        out[x - x1] = (u32)(((red + DOCK_GLASS_BLUR_ROWS / 2) / DOCK_GLASS_BLUR_ROWS << 16) |
                            ((green + DOCK_GLASS_BLUR_ROWS / 2) / DOCK_GLASS_BLUR_ROWS << 8) |
                            ((blue + DOCK_GLASS_BLUR_ROWS / 2) / DOCK_GLASS_BLUR_ROWS));
        if (x + 1 < x2) {
            int leaving_x = x - DOCK_GLASS_BLUR_RADIUS;
            int entering_x = x + DOCK_GLASS_BLUR_RADIUS + 1;
            if (leaving_x < 0) leaving_x = 0;
            if (entering_x >= width) entering_x = width - 1;
            u32 leaving = pixels[source_y * width + leaving_x];
            u32 entering = pixels[source_y * width + entering_x];
            red += (int)((entering >> 16) & 255) - (int)((leaving >> 16) & 255);
            green += (int)((entering >> 8) & 255) - (int)((leaving >> 8) & 255);
            blue += (int)(entering & 255) - (int)(leaving & 255);
        }
    }
}

static int dock_glass_coverage(int width, int height, int radius, int x, int y) {
    if (radius > width / 2) radius = width / 2;
    if (radius > height / 2) radius = height / 2;
    int cx = x < radius ? x : (x >= width - radius ? width - 1 - x : radius);
    int cy = y < radius ? y : (y >= height - radius ? height - 1 - y : radius);
    return graphics_corner_coverage(radius, cx, cy);
}

static void dock_glass_blur_backdrop(int width, int height) {
    int visible[APP_COUNT];
    int count = dock_get_visible_apps(visible, APP_COUNT);
    if (count <= 0 || width <= 0 || height <= 0) return;
    int pill_w = count * 68 + 48 + (count > 1 ? DOCK_FILES_GAP : 0);
    if (pill_w > DOCK_CACHE_WIDTH) pill_w = DOCK_CACHE_WIDTH;
    int pill_x = (width - pill_w) / 2, pill_y = height - 96, pill_h = 84;
    int radius = 35;
    GraphicsClip clip = graphics_get_clip();
    int x1 = pill_x > clip.x1 ? pill_x : clip.x1;
    int x2 = pill_x + pill_w < clip.x2 ? pill_x + pill_w : clip.x2;
    int y1 = pill_y > clip.y1 ? pill_y : clip.y1;
    int y2 = pill_y + pill_h < clip.y2 ? pill_y + pill_h : clip.y2;
    if (x1 >= x2 || y1 >= y2) return;

    u32 *rows[DOCK_GLASS_BLUR_ROWS];
    u32 *next_row = dock_glass_next_blur_row;
    for (int k = 0; k < DOCK_GLASS_BLUR_ROWS; k++) {
        rows[k] = dock_glass_blur_rows[k];
        dock_glass_blur_row(y1 + k - DOCK_GLASS_BLUR_RADIUS, x1, x2,
                            width, height, rows[k]);
    }
    int span = x2 - x1;
    for (int offset = 0; offset < span; offset++) {
        int red = 0, green = 0, blue = 0;
        for (int k = 0; k < DOCK_GLASS_BLUR_ROWS; k++) {
            u32 color = rows[k][offset];
            red += (color >> 16) & 255;
            green += (color >> 8) & 255;
            blue += color & 255;
        }
        dock_glass_vertical_sum[0][offset] = red;
        dock_glass_vertical_sum[1][offset] = green;
        dock_glass_vertical_sum[2][offset] = blue;
    }
    for (int y = y1; y < y2; y++) {
        for (int x = x1; x < x2; x++) {
            int coverage = dock_glass_coverage(pill_w, pill_h, radius,
                                               x - pill_x, y - pill_y);
            if (!coverage) continue;
            int offset = x - x1;
            u32 blurred = (u32)(((dock_glass_vertical_sum[0][offset] + DOCK_GLASS_BLUR_ROWS / 2) /
                                 DOCK_GLASS_BLUR_ROWS << 16) |
                                ((dock_glass_vertical_sum[1][offset] + DOCK_GLASS_BLUR_ROWS / 2) /
                                 DOCK_GLASS_BLUR_ROWS << 8) |
                                ((dock_glass_vertical_sum[2][offset] + DOCK_GLASS_BLUR_ROWS / 2) /
                                 DOCK_GLASS_BLUR_ROWS));
            int opacity = (192 * coverage + 32) >> 6;
            u32 *dst = &pixels[y * width + x];
            *dst = blend(*dst, blurred, opacity);
        }
        if (y + 1 < y2) {
            u32 *leaving = rows[0];
            dock_glass_blur_row(y + DOCK_GLASS_BLUR_RADIUS + 1, x1, x2,
                                width, height, next_row);
            for (int offset = 0; offset < span; offset++) {
                u32 old_color = leaving[offset], new_color = next_row[offset];
                dock_glass_vertical_sum[0][offset] += (int)((new_color >> 16) & 255) -
                                                      (int)((old_color >> 16) & 255);
                dock_glass_vertical_sum[1][offset] += (int)((new_color >> 8) & 255) -
                                                      (int)((old_color >> 8) & 255);
                dock_glass_vertical_sum[2][offset] += (int)(new_color & 255) -
                                                      (int)(old_color & 255);
            }
            for (int k = 0; k < DOCK_GLASS_BLUR_ROWS - 1; k++) rows[k] = rows[k + 1];
            rows[DOCK_GLASS_BLUR_ROWS - 1] = next_row;
            next_row = leaving;
        }
    }
}

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

#ifndef POLLIK_COMPOSITOR_LEGACY_DAMAGE
/* The drag union is prepended to queued damage, so the two sources can
 * overlap even though each source is internally coalesced. Repainting an
 * overlap twice compounds translucent glass/overlays and breaks equivalence
 * with a full repaint. Merge after blur expansion, when the true paint bounds
 * are known. */
static int merge_damage_regions(GraphicsClip *regions, int count) {
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < count && !changed; i++) {
            for (int j = i + 1; j < count; j++) {
                GraphicsClip a = regions[i], b = regions[j];
                if (a.x1 < b.x2 && b.x1 < a.x2 && a.y1 < b.y2 && b.y1 < a.y2) {
                    regions[i] = clip_union(a, b);
                    regions[j] = regions[--count];
                    changed = 1;
                    break;
                }
            }
        }
    }
    return count;
}
#endif

void compositor_invalidate(int id) {
    if (id >= 0 && id < NUM_APPS) {
        dirty_client[id] = 1;
        client_damage_valid[id] = 0;
    }
}

void compositor_invalidate_all_surfaces(void) {
    for (int i = 0; i < NUM_APPS; i++) {
        dirty_client[i] = 1;
        client_damage_valid[i] = 0;
        surface_active_state[i] = -1;
        surface_theme_state[i] = -1;
    }
    dock_background_valid = 0;
    wallpaper_theme = -1;
}
/* Keep independent damaged areas independent. A union of an animated window,
 * the Dock and the cursor can otherwise turn a few small updates into a
 * near-full-screen copy every frame. */
static void add_damage(GraphicsClip bounds) {
    GraphicsClip screen = {0, 0, shell.width, shell.height};
    bounds = clip_intersection(bounds, screen);
    if (bounds.x1 >= bounds.x2 || bounds.y1 >= bounds.y2) return;
    for (int i = 0; i < g_damage_count; ++i) {
        GraphicsClip a = g_damage[i];
        if (bounds.x1 <= a.x2 && a.x1 <= bounds.x2 &&
            bounds.y1 <= a.y2 && a.y1 <= bounds.y2) {
            g_damage[i] = clip_union(a, bounds);
            /* Re-merge transitive overlaps without growing the list. */
            for (int j = 0; j < g_damage_count; ++j) {
                if (j == i) continue;
                GraphicsClip b = g_damage[j];
                if (g_damage[i].x1 <= b.x2 && b.x1 <= g_damage[i].x2 &&
                    g_damage[i].y1 <= b.y2 && b.y1 <= g_damage[i].y2) {
                    g_damage[i] = clip_union(g_damage[i], b);
                    g_damage[j] = g_damage[--g_damage_count];
                    if (j < i) --i;
                    j = -1;
                }
            }
            return;
        }
    }
    if (g_damage_count < COMPOSITOR_DAMAGE_CAP) {
        g_damage[g_damage_count++] = bounds;
    } else {
        g_damage[0] = screen;
        g_damage_count = 1;
    }
}
/* Partial damage: window regions whose client or animated geometry changed. */
void compositor_invalidate_animated(int id) {
    if (id < 0 || id >= NUM_APPS) return;
    dirty_client[id] = 1;
    client_damage_valid[id] = 0;
    if (!windows[id].open || windows[id].minimized) return;
    GraphicsClip b = window_visual_bounds(windows[id].x, windows[id].y,
                                          window_width(id), window_height(id), 1);
    add_damage(b);
}
void compositor_invalidate_region(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    add_damage((GraphicsClip){x, y, x + w, y + h});
}
void compositor_invalidate_client_region(int id, int x, int y, int w, int h) {
    if (id < 0 || id >= NUM_APPS || w <= 0 || h <= 0) return;
    int ww = window_width(id), wh = window_height(id);
    GraphicsClip local = clip_intersection((GraphicsClip){x, y, x + w, y + h},
                                            (GraphicsClip){0, 0, ww, wh});
    if (local.x1 >= local.x2 || local.y1 >= local.y2) return;
    int full_client = dirty_client[id] || surface_active_state[id] < 0 ||
                      surface_w[id] != ww || surface_h[id] != wh;
    if (full_client) {
        dirty_client[id] = 1;
        client_damage_valid[id] = 0;
    } else if (!client_damage_valid[id]) {
        client_damage[id] = local;
        client_damage_valid[id] = 1;
    } else {
        client_damage[id] = clip_union(client_damage[id], local);
    }
    if (!windows[id].open || windows[id].minimized) return;
    if (full_client) {
        add_damage(window_visual_bounds(windows[id].x, windows[id].y, ww, wh,
                                        windows[id].state == WINDOW_STATE_NORMAL));
    } else {
        add_damage((GraphicsClip){windows[id].x + local.x1, windows[id].y + local.y1,
                                  windows[id].x + local.x2, windows[id].y + local.y2});
    }
}
static int compositor_take_damage(GraphicsClip *out, int max_out) {
    int count = g_damage_count < max_out ? g_damage_count : max_out;
    for (int i = 0; i < count; ++i) out[i] = g_damage[i];
    g_damage_count = 0;
    return count;
}
void compositor_minimize(int id) {
    (void)id;
}
void compositor_cancel_minimize(int id) {
    if (id >= 0) ui_anim_cancel(id);
}
int compositor_minimizing(int id) {
    const WindowAnim *a = ui_anim_get(id);
    return (a && a->active && a->type == WINDOW_ANIM_MINIMIZE);
}
int compositor_animate(u32 now) {
    GraphicsClip old_bounds[APP_COUNT];
    WindowAnimType old_type[APP_COUNT];
    int was_active[APP_COUNT];
    int dock_needs_refresh = 0;
    for (int id = 0; id < APP_COUNT; id++) {
        const WindowAnim *a = ui_anim_get(id);
        was_active[id] = a != 0;
        if (a) {
            old_bounds[id] = window_visual_bounds(a->cur_x, a->cur_y,
                                                   a->cur_w, a->cur_h, 1);
            old_type[id] = a->type;
        }
        if (ui_anim_dock_is_launching(id)) dock_needs_refresh = 1;
    }
    ui_anim_update(now);
    for (int id = 0; id < APP_COUNT; id++) {
        if (was_active[id]) {
            add_damage(old_bounds[id]);
            const WindowAnim *a = ui_anim_get(id);
            if (a) {
                add_damage(window_visual_bounds(a->cur_x, a->cur_y,
                                                      a->cur_w, a->cur_h, 1));
            } else if ((old_type[id] == WINDOW_ANIM_OPEN ||
                        old_type[id] == WINDOW_ANIM_RESTORE) &&
                       windows[id].open && !windows[id].minimized) {
                add_damage(window_visual_bounds(windows[id].x, windows[id].y,
                    window_width(id), window_height(id),
                    windows[id].state == WINDOW_STATE_NORMAL));
            } else if (old_type[id] == WINDOW_ANIM_MINIMIZE) {
                /* The final minimize frame changes the Dock indicator from
                 * the focused state to the gray running state. */
                dock_needs_refresh = 1;
            }
        }
        if (ui_anim_dock_is_launching(id)) dock_needs_refresh = 1;
    }
    if (dock_needs_refresh) {
        int dock_y = shell.height - 145;
        if (dock_y < 0) dock_y = 0;
        add_damage((GraphicsClip){0, dock_y, shell.width, shell.height});
    }
    int active = ui_anim_has_active();
    return active;
}
static void set_target(u32 *buf, int w, int h, int stride) { set_draw_target(buf, w, h, stride); }
static int render_window_to_surface(int id, int is_active, const GraphicsClip *requested_clip) {
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
    GraphicsClip local_clip = requested_clip
        ? clip_intersection(*requested_clip, (GraphicsClip){0, 0, ww, wh})
        : (GraphicsClip){0, 0, ww, wh};
    graphics_set_clip(local_clip);
    const GuiApp *app = gui_app_get(id);
    int is_dark = ui_is_dark();
    u32 body;
    u32 tb_bg;
    u32 sep_col;
    if (is_dark) {
        if (id == APP_TERMINAL) {
            body = 0x000000;
        } else if (id == APP_POLLIKMARK) {
            body = 0x141a26;
        } else {
            body = 0x121520;
        }
        tb_bg = is_active ? 0x171b28 : 0x0f1118;
        sep_col = is_active ? 0x242a3c : 0x181c26;
    } else {
        body = (id == APP_TERMINAL) ? 0x000000 : app->body_active;
        tb_bg = is_active ? 0xf2eff6 : 0xdcd8e3;
        sep_col = is_active ? 0xded8e7 : 0xc8c2d1;
    }
    int wx = 0, wy = 0;
    /* Cache straight RGB over the entire frame, including the exterior.
     * Geometry alpha is applied exactly once over the real scene below. */
    rect(wx, wy, ww, wh, body);
    rect(wx, wy, ww, 34, tb_bg);
    rect(wx + 1, wy + 33, ww - 2, 1, sep_col);
    u32 c_close = is_active ? 0xef4444 : (is_dark ? 0x483e44 : 0xb8adb5);
    u32 c_min   = is_active ? 0xf59e0b : (is_dark ? 0x48423e : 0xb8b2ad);
    u32 c_max   = is_active ? 0x10b981 : (is_dark ? 0x3e4842 : 0xadb8b2);
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
            rect(wx + 53, wy + 14, 3, 2, 0x10b981);
            rect(wx + 49, wy + 16, 5, 4, 0x004d11);
            rect(wx + 50, wy + 17, 3, 2, 0x10b981);
        } else {
            rect(wx + 51, wy + 14, 3, 1, 0x004d11);
            rect(wx + 51, wy + 15, 1, 2, 0x004d11);
            rect(wx + 55, wy + 18, 3, 1, 0x004d11);
            rect(wx + 57, wy + 16, 1, 2, 0x004d11);
        }
    }
    u32 title_col = is_dark ? (is_active ? 0xf1f5f9 : 0x64748b) : (is_active ? 0x241e30 : 0x888094);
    text(wx + 70, wy + 11, app->name, title_col, 1);
    gui_app_render(id, ww, wh, is_active);
    set_target(pixels, shell.width, shell.height, shell.width);
    graphics_set_clip(scene_clip);
    dirty_client[id] = 0;
    if (!requested_clip) client_damage_valid[id] = 0;
    surface_active_state[id] = is_active;
    surface_theme_state[id] = is_dark;
    surface_w[id] = ww;
    surface_h[id] = wh;
    perf_paint_us += (u32)(wm_time_us() - paint_start);
    wm_perf_record_client_paint();
    return 1;
}
/* Soft ambient drop shadow so floating windows read as raised. Drawn in four
 * disjoint bands around the client so the opaque interior blit is untouched. */
static void draw_window_shadow(int wx, int wy, int ww, int wh, int is_active) {
    int op1 = is_active ? 34 : 18, op2 = is_active ? 18 : 9, op3 = is_active ? 9 : 4;
    GraphicsClip saved = graphics_get_clip();
    GraphicsClip bounds = window_visual_bounds(wx, wy, ww, wh, 1);
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
    int is_dark = ui_is_dark();

    const WindowAnim *anim = ui_anim_get(id);
    int is_anim = (anim && anim->active);

    if (is_anim) {
        wx = anim->cur_x;
        wy = anim->cur_y;
        ww = anim->cur_w;
        wh = anim->cur_h;
        if (!g_surfaces[id].pixels || surface_w[id] <= 0 || surface_theme_state[id] != is_dark) {
            render_window_to_surface(id, is_active, 0);
        }
    } else if (dirty_client[id] || surface_active_state[id] != is_active ||
               surface_theme_state[id] != is_dark ||
               surface_w[id] != ww || surface_h[id] != wh) {
        if (!render_window_to_surface(id, is_active, 0)) return;
        client_damage_valid[id] = 0;
    } else if (client_damage_valid[id]) {
        GraphicsClip update = client_damage[id];
        client_damage_valid[id] = 0;
        if (!render_window_to_surface(id, is_active, &update)) return;
    }

    if (!is_anim && windows[id].state == WINDOW_STATE_NORMAL) {
        draw_window_shadow(wx, wy, ww, wh, is_active);
    }

    u32 *src = g_surfaces[id].pixels;
    if (!src) return;
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

    int orig_w = surface_w[id];
    int orig_h = surface_h[id];
    if (orig_w <= 0 || orig_h <= 0) {
        orig_w = window_width(id);
        orig_h = window_height(id);
    }

    if (is_anim && (ww != orig_w || wh != orig_h || anim->cur_alpha < 256)) {
        int alpha = anim->cur_alpha;
        int aspan = x2 - x1;
        /* Precompute the horizontal source map once per row-run instead of
         * dividing per pixel: this path runs on every open/close/minimize
         * frame, so the saved divides dominate an animation. Bounded by the
         * scene width; values match (x - wx) * orig_w / ww exactly. */
        static int src_x_map[4096];
        if (aspan > (int)(sizeof(src_x_map) / sizeof(src_x_map[0])))
            aspan = (int)(sizeof(src_x_map) / sizeof(src_x_map[0]));
        for (int i = 0; i < aspan; i++) {
            int sx = (x1 + i - wx) * orig_w / ww;
            if (sx < 0) sx = 0;
            if (sx >= orig_w) sx = orig_w - 1;
            src_x_map[i] = sx;
        }
        for (int y = y1; y < y2; y++) {
            int src_y = (y - wy) * orig_h / wh;
            if (src_y >= orig_h) src_y = orig_h - 1;
            u32 *dst = &pixels[y * shell.width + x1];
            u32 *s_row = &src[src_y * src_stride];
            if (alpha < 256) {
                for (int i = 0; i < aspan; i++)
                    dst[i] = blend(dst[i], s_row[src_x_map[i]], alpha);
            } else {
                for (int i = 0; i < aspan; i++) dst[i] = s_row[src_x_map[i]];
            }
        }
        return;
    }

    int span = x2 - x1, src_x_offset = x1 - wx;
    int radius = (windows[id].state == WINDOW_STATE_MAXIMIZED) ? 0 : 12;
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
                /* Borderless frame: fully covered pixels take the client cache
                 * and partial pixels antialias it straight over the scene. */
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
    rounded(r.x, r.y, r.w, r.h, 14, ui_theme()->accent, 60);
    rounded(r.x + 3, r.y + 3, r.w - 6, r.h - 6, 11, 0xffffff, 35);
}
static inline void set_cursor_px(int x, int y, u32 col) {
    rect(x, y, 1, 1, col);
}
static void __attribute__((unused)) draw_resize_cursor(int cx, int cy, int kind) {
    u32 c_out = 0x181822, c_in = 0xffffff;
    int ox = cx, oy = cy;
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
/* Windows-style text I-beam: flat black outline, solid white core, centred on
 * the caret hotspot. Replaces the old gradient sprite so every cursor shares
 * one visual language. */
static void __attribute__((unused)) draw_ibeam_cursor(int cx, int cy) {
    u32 o = 0x000000, w = 0xffffff;
    int top = cy - 11, bot = cy + 11;
    rect(cx - 4, top, 9, 3, o);
    rect(cx - 3, top + 1, 7, 1, w);
    rect(cx - 4, bot - 3, 9, 3, o);
    rect(cx - 3, bot - 2, 7, 1, w);
    rect(cx - 1, top + 2, 3, bot - top - 4, o);
    rect(cx, top + 2, 1, bot - top - 4, w);
    rect(cx - 4, top + 3, 1, 1, o);
    rect(cx + 4, top + 3, 1, 1, o);
    rect(cx - 4, bot - 4, 1, 1, o);
    rect(cx + 4, bot - 4, 1, 1, o);
}
/* Windows-style pointing hand: flat black outline, white face, index finger
 * up and folded fingers marked by two separations. Hotspot near (cx, cy). */
static void __attribute__((unused)) draw_hand_cursor(int cx, int cy) {
    u32 o = 0x000000, w = 0xffffff;
    roundrect(cx + 3, cy, 7, 14, 3, o);
    roundrect(cx - 1, cy + 8, 7, 9, 3, o);
    roundrect(cx + 2, cy + 8, 15, 15, 5, o);
    roundrect(cx + 4, cy + 1, 5, 12, 2, w);
    roundrect(cx, cy + 9, 5, 7, 2, w);
    roundrect(cx + 3, cy + 9, 13, 13, 4, w);
    rect(cx + 8, cy + 10, 1, 12, o);
    rect(cx + 12, cy + 10, 1, 12, o);
}
/* Modern Windows 11 style vector arrow cursor: sharp tip, crisp dark outline,
 * brilliant pure white face and multi-layered fluent drop shadow. Coordinates
 * are quarter-pixels; the hotspot is exactly (mx, my). */
static int arrow_inside(int x, int y, int inset) {
    /* Classic Windows arrow proportions (12x20 px) at quarter-pixel scale:
     * vertical left edge, concave left-wing notch, short tail, right wing. */
    static const int outer[][2] = {{0,0},{0,64},{16,48},{28,80},{40,74},{28,44},{50,44}};
    static const int inner[][2] = {{7,10},{7,50},{17,42},{27,70},{33,66},{26,40},{42,40}};
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
#define ARROW_W 20
#define ARROW_H 29
static u8 arrow_shadow_lut[ARROW_W * ARROW_H];
static u8 arrow_edge_lut[ARROW_W * ARROW_H];
static u8 arrow_face_lut[ARROW_W * ARROW_H];
static int arrow_lut_ready;
/* Coverage samples never depend on the pointer position, so evaluate the
 * seven-edge polygon tests once instead of 4 subsamples x 12 tests per pixel
 * on every mouse packet. Values are bit-identical to the direct loop. */
static void build_arrow_lut(void) {
    for (int y = 0; y < ARROW_H; y++) for (int x = 0; x < ARROW_W; x++) {
        int edge = 0, face = 0, shadow = 0;
        for (int sy = 1; sy <= 3; sy += 2) for (int sx = 1; sx <= 3; sx += 2) {
            edge += arrow_inside(x * 4 + sx, y * 4 + sy, 0);
            face += arrow_inside(x * 4 + sx, y * 4 + sy, 1);
            shadow += arrow_inside(x * 4 + sx - 6, y * 4 + sy - 8, 0);
            shadow += arrow_inside(x * 4 + sx - 8, y * 4 + sy - 12, 0);
        }
        int i = y * ARROW_W + x;
        arrow_edge_lut[i] = (u8)edge;
        arrow_face_lut[i] = (u8)face;
        arrow_shadow_lut[i] = (u8)shadow;
    }
    arrow_lut_ready = 1;
}
static void __attribute__((unused)) draw_arrow_cursor(int mx, int my) {
    if (!arrow_lut_ready) build_arrow_lut();
    GraphicsClip clip = graphics_get_clip();
    for (int y = 0; y < ARROW_H; y++) for (int x = 0; x < ARROW_W; x++) {
        int i = y * ARROW_W + x;
        int shadow = arrow_shadow_lut[i], edge = arrow_edge_lut[i], face = arrow_face_lut[i];
        if (!(shadow | edge | face)) continue;
        int xx = mx + x, yy = my + y;
        if (xx < clip.x1 || xx >= clip.x2 || yy < clip.y1 || yy >= clip.y2) continue;
        u32 *p = &pixels[yy * shell.width + xx];
        if (shadow) *p = blend(*p, 0x0c0818, shadow * 12);
        if (edge) *p = blend(*p, 0x161420, edge * 64);
        if (face) *p = blend(*p, 0xffffff, face * 64);
    }
}
#ifdef POLLIK_INSTALL_MEDIA
#define CURSOR_SIDE 32
#else
#define CURSOR_SIDE CURSOR_MAX_SIDE
#endif
static int g_cursor_scale_index, cursor_previous_scale_index;
static u32 cursor_active_pixels[CURSOR_SIDE * CURSOR_SIDE];
static int cursor_active_sprite = -1, cursor_active_scale = -1;
static u32 cursor_saved[CURSOR_SIDE * CURSOR_SIDE];
static int cursor_saved_x, cursor_saved_y, cursor_saved_w, cursor_saved_h;
static int cursor_saved_left, cursor_saved_top;
static int cursor_saved_valid;
static u32 cursor_old_frame[CURSOR_SIDE * CURSOR_SIDE];
static u32 cursor_new_frame[CURSOR_SIDE * CURSOR_SIDE];
static int cursor_sprite_for_kind(int kind) {
    if (kind == CURSOR_IBEAM) return CURSOR_SPRITE_IBEAM;
    if (kind == CURSOR_POINTER) return CURSOR_SPRITE_HAND;
    if (kind == CURSOR_RESIZE_H) return CURSOR_SPRITE_RESIZE_EW;
    if (kind == CURSOR_RESIZE_V) return CURSOR_SPRITE_RESIZE_NS;
    if (kind == CURSOR_RESIZE_NWSE) return CURSOR_SPRITE_RESIZE_NWSE;
    if (kind == CURSOR_RESIZE_NESW) return CURSOR_SPRITE_RESIZE_NESW;
    if (kind == CURSOR_BUSY) return CURSOR_SPRITE_BUSY;
    if (kind == CURSOR_MOVE) return CURSOR_SPRITE_MOVE;
    if (kind == CURSOR_NOT_ALLOWED) return CURSOR_SPRITE_NOT_ALLOWED;
    return CURSOR_SPRITE_ARROW;
}
static void cursor_geometry(int x, int y, int kind, int *sprite_out,
                            int *left_out, int *top_out, int *flip_x_out, int *flip_y_out) {
    int sprite = cursor_sprite_for_kind(kind);
    int flip_x = 0, flip_y = 0;
    int side = cursor_sides[g_cursor_scale_index];
    int hot_x = cursor_scaled_hotspots[g_cursor_scale_index][sprite][0];
    int hot_y = cursor_scaled_hotspots[g_cursor_scale_index][sprite][1];
    if (kind == CURSOR_DEFAULT) {
        if (x - hot_x + side > shell.width) { flip_x = 1; hot_x = side - 1; }
        if (y - hot_y + side > shell.height) { flip_y = 1; hot_y = side - 1; }
    }
    *sprite_out = sprite; *left_out = x - hot_x; *top_out = y - hot_y;
    *flip_x_out = flip_x; *flip_y_out = flip_y;
}
static GraphicsClip cursor_bounds(int x, int y, int kind) {
    int sprite, left, top, flip_x, flip_y;
    cursor_geometry(x, y, kind, &sprite, &left, &top, &flip_x, &flip_y);
    (void)sprite; (void)flip_x; (void)flip_y;
    return clip_intersection((GraphicsClip){left, top, left + cursor_sides[g_cursor_scale_index],
                                             top + cursor_sides[g_cursor_scale_index]},
                             (GraphicsClip){0, 0, shell.width, shell.height});
}
static void cursor_copy_background(u32 *out, GraphicsClip rect, int x, int y,
                                   int allow_saved) {
    int w = rect.x2 - rect.x1, h = rect.y2 - rect.y1;
    if (allow_saved && cursor_saved_valid && x == cursor_saved_x && y == cursor_saved_y &&
        w == cursor_saved_w && h == cursor_saved_h) {
        memcpy(out, cursor_saved, (u32)w * (u32)h * sizeof(u32));
        return;
    }
    for (int row = 0; row < h; ++row)
        memcpy(out + row * w, pixels + (rect.y1 + row) * shell.width + rect.x1,
               (u32)w * sizeof(u32));
}
static void cursor_compose_sprite(u32 *out, GraphicsClip rect, int x, int y, int kind) {
    int sprite, left, top, flip_x, flip_y;
    cursor_geometry(x, y, kind, &sprite, &left, &top, &flip_x, &flip_y);
    int side = cursor_sides[g_cursor_scale_index];
    if (cursor_active_sprite != sprite || cursor_active_scale != g_cursor_scale_index) {
        memset(cursor_active_pixels, 0, sizeof(cursor_active_pixels));
        const u8 *run = cursor_stream + cursor_stream_offsets[g_cursor_scale_index][sprite];
        while (*run != 255) {
            int y = *run++, x = *run++, count = *run++;
            for (int i = 0; i < count; ++i) {
                u32 alpha = *run++, gray = *run++;
                cursor_active_pixels[y * side + x + i] = (alpha << 24) | (gray * 0x00010101u);
            }
        }
        cursor_active_sprite = sprite; cursor_active_scale = g_cursor_scale_index;
    }
    int w = rect.x2 - rect.x1, h = rect.y2 - rect.y1;
    for (int py = rect.y1; py < rect.y2; ++py) {
        for (int px = rect.x1; px < rect.x2; ++px) {
            int sx = px - left, sy = py - top;
            if (flip_x) sx = side - 1 - sx;
            if (flip_y) sy = side - 1 - sy;
            u32 argb = cursor_active_pixels[sy * side + sx];
            u32 alpha = argb >> 24;
            if (alpha) {
                u32 color = argb & 0x00ffffffu;
                u32 index = (u32)(py - rect.y1) * (u32)w + (u32)(px - rect.x1);
                out[index] = blend(out[index], color, (int)((alpha * 256u + 127u) / 255u));
            }
        }
    }
    (void)h;
}
static void present_clip(GraphicsClip clip) {
    GraphicsClip screen = {0, 0, shell.width, shell.height};
    clip = clip_intersection(clip, screen);
    if (clip.x1 >= clip.x2 || clip.y1 >= clip.y2) return;
    u64 present_start = wm_time_us();
    framebuffer_present(pixels, clip.x1, clip.y1, clip.x2 - clip.x1, clip.y2 - clip.y1);
    perf_present_us += (u32)(wm_time_us() - present_start);
}
static u32 present_with_cursor(const GraphicsClip *scene, int scene_count, int force_cursor) {
    int mx = input_pointer_x(), my = input_pointer_y(), kind = input_cursor_kind();
    GraphicsClip current = cursor_bounds(mx, my, kind);
    int moved = !cursor_previous_valid || mx != cursor_previous_x || my != cursor_previous_y ||
                kind != cursor_previous_kind || g_cursor_scale_index != cursor_previous_scale_index;
    u32 presented = 0;
    for (int i = 0; i < scene_count; ++i) {
        GraphicsClip clip = clip_intersection(scene[i], (GraphicsClip){0, 0, shell.width, shell.height});
        if (clip.x1 >= clip.x2 || clip.y1 >= clip.y2) continue;
        present_clip(clip);
        presented += (u32)(clip.x2 - clip.x1) * (u32)(clip.y2 - clip.y1);
    }
    int repaint_current = force_cursor || moved || !cursor_previous_valid;
    int repaint_old = moved && cursor_previous_valid;
    for (int i = 0; i < scene_count; ++i) {
        GraphicsClip overlap = clip_intersection(current, scene[i]);
        if (overlap.x1 < overlap.x2 && overlap.y1 < overlap.y2) repaint_current = 1;
    }
    if (!repaint_current) return presented;
    GraphicsClip old = cursor_previous_valid
        ? (GraphicsClip){cursor_saved_left, cursor_saved_top,
                         cursor_saved_left + cursor_saved_w, cursor_saved_top + cursor_saved_h}
        : (GraphicsClip){0,0,0,0};
    int old_w = old.x2 - old.x1, old_h = old.y2 - old.y1;
    int new_w = current.x2 - current.x1, new_h = current.y2 - current.y1;
    if (repaint_old && old_w > 0 && old_h > 0) {
        int old_saved = 1;
        for (int i = 0; i < scene_count; ++i) {
            GraphicsClip overlap = clip_intersection(old, scene[i]);
            if (overlap.x1 < overlap.x2 && overlap.y1 < overlap.y2) old_saved = 0;
        }
        cursor_copy_background(cursor_old_frame, old, cursor_previous_x, cursor_previous_y, old_saved);
    }
    if (new_w > 0 && new_h > 0) {
        cursor_copy_background(cursor_saved, current, mx, my, 0);
        memcpy(cursor_new_frame, cursor_saved, (u32)new_w * (u32)new_h * sizeof(u32));
        cursor_compose_sprite(cursor_new_frame, current, mx, my, kind);
        cursor_saved_left = current.x1; cursor_saved_top = current.y1;
        cursor_saved_x = mx; cursor_saved_y = my;
        cursor_saved_w = new_w; cursor_saved_h = new_h; cursor_saved_valid = 1;
    }
    framebuffer_present_cursor_pair(repaint_old ? cursor_old_frame : 0,
        old.x1, old.y1, old_w, old_h, new_w > 0 && new_h > 0 ? cursor_new_frame : 0,
        current.x1, current.y1, new_w, new_h);
    presented += (u32)(old_w * old_h) * (u32)(repaint_old != 0) + (u32)new_w * (u32)new_h;
    g_perf_stats.composed_pixels += (u32)new_w * (u32)new_h;
    g_perf_stats.effective_rects++;
    cursor_previous_x = mx; cursor_previous_y = my; cursor_previous_kind = kind;
    cursor_previous_scale_index = g_cursor_scale_index;
    cursor_previous_valid = 1;
    return presented;
}
void compositor_draw_cursor(int full) {
    if (!pixels || !wallpaper) return;
    if (!full && cursor_previous_valid && cursor_previous_x == input_pointer_x() &&
        cursor_previous_y == input_pointer_y() && cursor_previous_kind == input_cursor_kind() &&
        cursor_previous_scale_index == g_cursor_scale_index) return;
    perf_begin();
    u32 count = present_with_cursor(0, 0, full);
    if (count) g_perf_stats.cursor_frames++;
    wm_perf_frame_end(0, perf_paint_us, perf_present_us, 0, count);
}

int compositor_cursor_size(void) { return cursor_percents[g_cursor_scale_index]; }
void compositor_set_cursor_size(int percent) {
    for (int i = 0; i < 4; ++i) {
        if (cursor_sides[i] > CURSOR_SIDE) continue;
        if (cursor_percents[i] == percent && i != g_cursor_scale_index) {
            g_cursor_scale_index = i;
            compositor_draw_cursor(1);
            return;
        }
    }
}

#ifndef POLLIK_INSTALL_MEDIA
#define PERF_OVERLAY_X 8
#define PERF_OVERLAY_Y 35
#define PERF_OVERLAY_W 660
#define PERF_OVERLAY_H 58
static void draw_perf_overlay(void) {
    char summary[256];
    wm_perf_summary(summary, sizeof(summary));
    rect(PERF_OVERLAY_X, PERF_OVERLAY_Y, PERF_OVERLAY_W, PERF_OVERLAY_H, 0x161420);
    int line = 0;
    char *start = summary;
    for (char *p = summary; ; ++p) {
        if (*p != '\n' && *p != 0) continue;
        char end = *p;
        *p = 0;
        text(PERF_OVERLAY_X + 6, PERF_OVERLAY_Y + 3 + line * 14,
             start, line ? 0xd8d4e5 : 0xffffff, 1);
        if (!end || ++line == 4) break;
        start = p + 1;
    }
}
#endif

void compositor_invalidate_dock(void) {
    dock_background_valid = 0;
    request_scene_redraw();
}
void compositor_paint(int full) {
    perf_begin();
    int width = shell.width, height = shell.height;
    /* The auth screen owns the whole display, so never composite wallpaper,
     * icons, windows or the dock underneath it. Keeping this path cheap is what
     * makes typing a password feel as smooth as the unlocked desktop. */
    if (auth_is_active()) {
        GraphicsClip screen = {0, 0, width, height};
        g_damage_count = 0;
        graphics_set_clip(screen);
        auth_render(width, height);
        g_perf_stats.composed_pixels = (u32)width * (u32)height;
        g_perf_stats.effective_rects = 1;
        u32 count = present_with_cursor(&screen, 1, 0);
        graphics_set_clip(screen);
        wm_perf_frame_end(0, perf_paint_us, perf_present_us, 1, count);
        shell.dirty = 0;
        shell.scene_dirty = 0;
        return;
    }
    /* Clock changes repaint only the 32-pixel menu strip, preserving the
     * cached windows, desktop icons, and Dock while the user is idle. */
    if (full == 3 && !shell.scene_dirty && !shell.alttab_open && !ui_anim_has_active()) {
        GraphicsClip bar = {0, 0, width, 32};
        graphics_set_clip(bar);
        for (int y = 0; y < 32; y++)
            memcpy(pixels + y * width, wallpaper + y * width, (u32)width * 4);
        desktop_draw_bar();
        if (g_active_menu.active) ui_draw_menu(&g_active_menu);
        g_perf_stats.composed_pixels = (u32)width * 32u;
        g_perf_stats.effective_rects = 1;
        u32 count = present_with_cursor(&bar, 1, 0);
        graphics_set_clip((GraphicsClip){0, 0, width, height});
        wm_perf_frame_end(0, perf_paint_us, perf_present_us, 0, count);
        shell.dirty = 0;
        shell.scene_dirty = 0;
        return;
    }
    int visible[APP_COUNT];
    int cur_dock_count = dock_get_visible_apps(visible, APP_COUNT);
    static int last_dock_visible_count = -1;
    if (cur_dock_count != last_dock_visible_count) {
        last_dock_visible_count = cur_dock_count;
        dock_background_valid = 0;
        full = 1;
    }
    if (shell.scene_dirty || shell.alttab_open || auth_is_active()) full = 1;
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
        u32 count = present_with_cursor(&dock_clip, 1, 0);
        graphics_set_clip((GraphicsClip){0, 0, width, height});
        if (count) g_perf_stats.dock_frames++;
        wm_perf_frame_end(0, perf_paint_us, perf_present_us, 0, count);
        return;
    }
    if (!full) full = 1;
    if (wallpaper_theme != shell.theme) {
        /* Re-render the active theme once into the full-resolution u32 buffer.
         * Light previously existed only as a half-res RGB565 cache that was
         * bilinearly expanded per pixel on every repaint; dark simply memcpy'd
         * a pre-rendered u32 wallpaper. That per-pixel expansion on each damage
         * frame (drag, animation, clock tick) was the light-theme lag. */
        desktop_paint_wallpaper(wallpaper);
        wallpaper_theme = shell.theme;
        full = 1;
    }
    GraphicsClip regions[COMPOSITOR_DAMAGE_CAP];
    int region_count = 0;
    if (full == 2) {
        int target = shell.drag_app >= 0 ? shell.drag_app : shell.resizing;
        if (target >= 0) {
            GraphicsClip old = window_visual_bounds(shell.drag_old_x, shell.drag_old_y,
                                                     shell.drag_old_w, shell.drag_old_h, 1);
            GraphicsClip current = window_visual_bounds(windows[target].x, windows[target].y,
                window_width(target), window_height(target),
                windows[target].state == WINDOW_STATE_NORMAL);
            regions[region_count++] = clip_union(old, current);
        }
        region_count += compositor_take_damage(regions + region_count,
                                                COMPOSITOR_DAMAGE_CAP - region_count);
        if (!region_count) full = 1;
    }
    if (full == 2) {
        /* Blur outputs within radius R depend on backdrop pixels that may
         * themselves be outside the damage. Rebuild the backdrop dependency
         * halo too, so those samples are raw scene pixels instead of the
         * previously glass-composited frame. */
#ifdef POLLIK_COMPOSITOR_LEGACY_DAMAGE
        int blur_halo = DOCK_GLASS_BLUR_RADIUS;
#else
        int blur_halo = DOCK_GLASS_BLUR_RADIUS * 2;
#endif
        for (int i = 0; i < region_count; i++) {
            regions[i].x1 -= blur_halo;
            regions[i].y1 -= blur_halo;
            regions[i].x2 += blur_halo;
            regions[i].y2 += blur_halo;
        }
#ifndef POLLIK_COMPOSITOR_LEGACY_DAMAGE
        region_count = merge_damage_regions(regions, region_count);
#endif
    }
    if (full == 2 && !dock_background_valid) full = 1;
    if (full == 1) {
        regions[0] = (GraphicsClip){0, 0, width, height};
        region_count = 1;
        g_damage_count = 0;
    }
    if (full != 2 && full != 1) full = 1;
    int top = active_app();
    int composed_pixels = 0;
    for (int r = 0; r < region_count; ++r) {
        GraphicsClip clip = clip_intersection(regions[r], (GraphicsClip){0, 0, width, height});
        if (clip.x1 >= clip.x2 || clip.y1 >= clip.y2) continue;
        graphics_set_clip(clip);
        int span = clip.x2 - clip.x1;
        if (full == 1) {
            u32 count = (u32)width * (u32)height;
            u32 *from = wallpaper, *to = pixels;
            __asm__ volatile("cld; rep movsl" : "+D"(to), "+S"(from), "+c"(count) :: "memory");
        } else {
            for (int y = clip.y1; y < clip.y2; y++)
                memcpy(pixels + y * width + clip.x1, wallpaper + y * width + clip.x1, (u32)span * 4);
        }
        if (full == 1 || clip.y1 < 32) desktop_draw_bar();
        desktop_items_draw();
        for (int z = 0; z < NUM_APPS; z++) {
            int id = z_order[z];
            const WindowAnim *anim = ui_anim_get(id);
            int is_anim = (anim && anim->active);
            if (!windows[id].open && !is_anim) continue;
            if (windows[id].minimized && !is_anim) continue;
            GraphicsClip visual = is_anim
                ? (GraphicsClip){anim->cur_x - 8, anim->cur_y - 8, anim->cur_x + anim->cur_w + 8, anim->cur_y + anim->cur_h + 8}
                : window_visual_bounds(windows[id].x, windows[id].y,
                                       window_width(id), window_height(id),
                                       windows[id].state == WINDOW_STATE_NORMAL);
            if (full == 1 || (visual.x2 > clip.x1 && visual.x1 < clip.x2 &&
                              visual.y2 > clip.y1 && visual.y1 < clip.y2))
                compose_window_surface(id, id == top);
        }
        draw_snap_preview();
        if (dock_is_visible() && (full == 1 || clip.y2 > height - 145)) {
#ifndef POLLIK_COMPOSITOR_LEGACY_DAMAGE
            GraphicsClip glass_clip = clip;
            if (full == 2) {
                glass_clip = (GraphicsClip){regions[r].x1 + DOCK_GLASS_BLUR_RADIUS,
                    regions[r].y1 + DOCK_GLASS_BLUR_RADIUS,
                    regions[r].x2 - DOCK_GLASS_BLUR_RADIUS,
                    regions[r].y2 - DOCK_GLASS_BLUR_RADIUS};
                glass_clip = clip_intersection(glass_clip, (GraphicsClip){0, 0, width, height});
            }
            graphics_set_clip(glass_clip);
#endif
            dock_glass_blur_backdrop(width, height);
            dock_draw_pill();
#ifndef POLLIK_COMPOSITOR_LEGACY_DAMAGE
            if (full == 2) {
                GraphicsClip cache_bounds = {dock_x, dock_y, dock_x + dock_w, dock_y + dock_h};
                GraphicsClip old_ring = clip_intersection(clip, cache_bounds);
                for (int y = old_ring.y1; y < old_ring.y2; y++)
                    for (int x = old_ring.x1; x < old_ring.x2; x++)
                        if (x < glass_clip.x1 || x >= glass_clip.x2 ||
                            y < glass_clip.y1 || y >= glass_clip.y2)
                            pixels[y * width + x] = dock_background[(y - dock_y) * DOCK_CACHE_WIDTH + x - dock_x];
            }
#endif
            /* Preserve the underlying Dock background separately for the next
             * hover frame; this clip may cover only a small section of it. */
            GraphicsClip cache_clip = clip_intersection(graphics_get_clip(),
                (GraphicsClip){dock_x, dock_y, dock_x + dock_w, dock_y + dock_h});
            if (cache_clip.x1 < cache_clip.x2 && cache_clip.y1 < cache_clip.y2) {
                for (int y = cache_clip.y1; y < cache_clip.y2; y++)
                    memcpy(dock_background + (y - dock_y) * DOCK_CACHE_WIDTH + cache_clip.x1 - dock_x,
                           pixels + y * width + cache_clip.x1,
                           (u32)(cache_clip.x2 - cache_clip.x1) * 4);
            }
            if (full == 1) dock_background_valid = 1;
#ifndef POLLIK_COMPOSITOR_LEGACY_DAMAGE
            graphics_set_clip(clip);
#endif
            dock_draw_content();
        }
        if (g_active_dialog.active) ui_draw_dialog();
        if (g_active_menu.active) ui_draw_menu(&g_active_menu);
        ui_draw_notifications(wm_time_ms());
        desktop_draw_overlays();
        auth_render(width, height);
        composed_pixels += (clip.x2 - clip.x1) * (clip.y2 - clip.y1);
        g_perf_stats.composed_pixels = (u32)composed_pixels;
        g_perf_stats.effective_rects++;
#ifndef POLLIK_INSTALL_MEDIA
        if (wm_perf_overlay_is_enabled() && clip.x1 < PERF_OVERLAY_X + PERF_OVERLAY_W &&
            clip.x2 > PERF_OVERLAY_X && clip.y1 < PERF_OVERLAY_Y + PERF_OVERLAY_H &&
            clip.y2 > PERF_OVERLAY_Y) draw_perf_overlay();
#endif
    }
    g_perf_stats.composed_pixels = (u32)composed_pixels;
    u32 pix_pres = present_with_cursor(regions, region_count, 0);
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
        hal_cpu_halt_forever();
    }
    u32 scene_pages = ((u32)scene_bytes + PMM_PAGE_SIZE - 1u) / PMM_PAGE_SIZE;
    pixels = (u32 *)pmm_alloc_pages(scene_pages);
    if (pixels) wallpaper = (u32 *)pmm_alloc_pages(scene_pages);
    if (!pixels || !wallpaper) {
        if (pixels) pmm_free_pages((uintptr_t)pixels, scene_pages);
        pixels = 0;
        serial("GFX insufficient RAM for scene and wallpaper\n");
        hal_cpu_halt_forever();
    }
    memset(pixels, 0, scene_pages * PMM_PAGE_SIZE);
    memset(wallpaper, 0, scene_pages * PMM_PAGE_SIZE);
    set_target(pixels, width, height, width);
    klog_dec(KLOG_CAT_GUI, "Scene and wallpaper allocated bytes: ",
             scene_pages * PMM_PAGE_SIZE * 2u);
}
void compositor_prepare_wallpapers(void) {
    desktop_paint_wallpaper(wallpaper);
    wallpaper_theme = shell.theme;
}
void compositor_wallpaper_changed(void) {
    wallpaper_theme = -1;
    dock_background_valid = 0;
    request_scene_redraw();
}
/* Boot splash: a calm animated-looking screen that owns the display while the
 * kernel brings subsystems online, so early heavy init never shows as a frozen
 * or tearing desktop. Fully repainted each call and presented immediately. */
void compositor_splash(const char *stage, int progress) {
    int w = shell.width, h = shell.height;
    if (!pixels || w <= 0 || h <= 0) return;
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;
    graphics_init(w, h);
    set_target(pixels, w, h, w);
    graphics_set_clip((GraphicsClip){0, 0, w, h});
    for (int y = 0; y < h; y++)
        rect(0, y, w, 1, blend(0x0a0d18, 0x1d2340, y * 256 / h));
    int cx = w / 2, ly = h / 2 - 54;
    centered(0, ly, w, "Pollik OS", 0xf3f2fb, 3);
    if (stage) centered(0, ly + 48, w, stage, 0x8f99ad, 1);
    int bar_w = 260, bar_h = 8, bx = cx - bar_w / 2, by = ly + 86;
    roundrect(bx, by, bar_w, bar_h, bar_h / 2, 0x232a44);
    int fill = bar_w * progress / 100;
    if (fill > 0) roundrect(bx, by, fill, bar_h, bar_h / 2, 0x6366f1);
    framebuffer_present(pixels, 0, 0, w, h);
}
