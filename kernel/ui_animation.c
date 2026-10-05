#include "ui_animation.h"
#include "wm.h"

static WindowAnim g_window_anims[APP_COUNT];
static int g_dock_launch_app = -1;
static u32 g_dock_launch_start_ms = 0;

int ease_out_cubic(int t) {
    if (t <= 0) return 0;
    if (t >= 256) return 256;
    int inv = 256 - t;
    int inv3 = (inv * inv * inv) >> 16;
    return 256 - inv3;
}

int ease_in_cubic(int t) {
    if (t <= 0) return 0;
    if (t >= 256) return 256;
    return (t * t * t) >> 16;
}

int ease_in_out_cubic(int t) {
    if (t <= 0) return 0;
    if (t >= 256) return 256;
    if (t < 128) {
        return (t * t * t) >> 14;
    } else {
        int inv = 256 - t;
        return 256 - ((inv * inv * inv) >> 14);
    }
}

void ui_anim_init(void) {
    memset(g_window_anims, 0, sizeof(g_window_anims));
    g_dock_launch_app = -1;
    g_dock_launch_start_ms = 0;
}

void ui_anim_cancel(int app_id) {
    if (app_id >= 0 && app_id < APP_COUNT) {
        g_window_anims[app_id].active = 0;
    }
}

int ui_anim_has_active(void) {
    for (int i = 0; i < APP_COUNT; i++) {
        if (g_window_anims[i].active) return 1;
    }
    return (g_dock_launch_app >= 0);
}

const WindowAnim *ui_anim_get(int app_id) {
    if (app_id >= 0 && app_id < APP_COUNT && g_window_anims[app_id].active) {
        return &g_window_anims[app_id];
    }
    return 0;
}

void ui_anim_start_open(int app_id, int x, int y, int w, int h) {
    if (app_id < 0 || app_id >= APP_COUNT) return;
    WindowAnim *a = &g_window_anims[app_id];
    a->active = 1;
    a->type = WINDOW_ANIM_OPEN;
    a->app_id = app_id;
    a->start_ms = wm_time_ms();
    a->duration_ms = 300;

    int sw = (w * 86) / 100;
    int sh = (h * 86) / 100;
    a->start_x = x + (w - sw) / 2;
    a->start_y = y + (h - sh) / 2 + 8;
    a->start_w = sw;
    a->start_h = sh;

    a->end_x = x;
    a->end_y = y;
    a->end_w = w;
    a->end_h = h;

    a->cur_x = a->start_x;
    a->cur_y = a->start_y;
    a->cur_w = a->start_w;
    a->cur_h = a->start_h;
    a->cur_alpha = 110;

    wm_invalidate_rect(x - 8, y - 8, w + 16, h + 16);
}

void ui_anim_start_minimize(int app_id, int dock_cx, int dock_cy) {
    if (app_id < 0 || app_id >= APP_COUNT) return;
    Window *w = wm_get_window(app_id);
    if (!w) return;

    const WindowAnim *current = ui_anim_get(app_id);
    int start_x = current ? current->cur_x : w->x;
    int start_y = current ? current->cur_y : w->y;
    int start_w = current ? current->cur_w : w->width;
    int start_h = current ? current->cur_h : w->height;
    WindowAnim *a = &g_window_anims[app_id];
    a->active = 1;
    a->type = WINDOW_ANIM_MINIMIZE;
    a->app_id = app_id;
    a->start_ms = wm_time_ms();
    a->duration_ms = 180;

    a->start_x = start_x;
    a->start_y = start_y;
    a->start_w = start_w;
    a->start_h = start_h;

    a->end_w = 36;
    a->end_h = 36;
    a->end_x = dock_cx - a->end_w / 2;
    a->end_y = dock_cy - a->end_h / 2;

    a->cur_x = start_x;
    a->cur_y = start_y;
    a->cur_w = start_w;
    a->cur_h = start_h;
    a->cur_alpha = 256;

    wm_invalidate_rect(start_x - 8, start_y - 8, start_w + 16, start_h + 16);
}

void ui_anim_start_restore(int app_id, int dock_cx, int dock_cy, int target_x, int target_y, int target_w, int target_h) {
    if (app_id < 0 || app_id >= APP_COUNT) return;
    WindowAnim *a = &g_window_anims[app_id];
    a->active = 1;
    a->type = WINDOW_ANIM_RESTORE;
    a->app_id = app_id;
    a->start_ms = wm_time_ms();
    a->duration_ms = 280;

    a->start_w = 36;
    a->start_h = 36;
    a->start_x = dock_cx - a->start_w / 2;
    a->start_y = dock_cy - a->start_h / 2;

    a->end_x = target_x;
    a->end_y = target_y;
    a->end_w = target_w;
    a->end_h = target_h;

    a->cur_x = a->start_x;
    a->cur_y = a->start_y;
    a->cur_w = a->start_w;
    a->cur_h = a->start_h;
    a->cur_alpha = 90;

    wm_invalidate_rect(target_x - 8, target_y - 8, target_w + 16, target_h + 16);
}

void ui_anim_start_close(int app_id) {
    if (app_id < 0 || app_id >= APP_COUNT) return;
    Window *w = wm_get_window(app_id);
    if (!w) return;

    WindowAnim *a = &g_window_anims[app_id];
    a->active = 1;
    a->type = WINDOW_ANIM_CLOSE;
    a->app_id = app_id;
    a->start_ms = wm_time_ms();
    a->duration_ms = 140;

    a->start_x = w->x;
    a->start_y = w->y;
    a->start_w = w->width;
    a->start_h = w->height;

    int ew = (w->width * 90) / 100;
    int eh = (w->height * 90) / 100;
    a->end_x = w->x + (w->width - ew) / 2;
    a->end_y = w->y + (w->height - eh) / 2 + 8;
    a->end_w = ew;
    a->end_h = eh;

    a->cur_x = a->start_x;
    a->cur_y = a->start_y;
    a->cur_w = a->start_w;
    a->cur_h = a->start_h;
    a->cur_alpha = 256;

    wm_invalidate_rect(w->x - 8, w->y - 8, w->width + 16, w->height + 16);
}

void ui_anim_update(u32 now_ms) {
    for (int i = 0; i < APP_COUNT; i++) {
        WindowAnim *a = &g_window_anims[i];
        if (!a->active) continue;

        u32 elapsed = now_ms - a->start_ms;
        int finished = (elapsed >= a->duration_ms);

        int t = finished ? 256 : (int)((elapsed * 256u) / a->duration_ms);
        int factor;
        if (a->type == WINDOW_ANIM_MINIMIZE || a->type == WINDOW_ANIM_CLOSE) {
            factor = ease_in_cubic(t);
        } else {
            factor = ease_out_cubic(t);
        }

        int old_x = a->cur_x, old_y = a->cur_y, old_w = a->cur_w, old_h = a->cur_h;

        a->cur_x = a->start_x + (((a->end_x - a->start_x) * factor) >> 8);
        a->cur_y = a->start_y + (((a->end_y - a->start_y) * factor) >> 8);
        a->cur_w = a->start_w + (((a->end_w - a->start_w) * factor) >> 8);
        a->cur_h = a->start_h + (((a->end_h - a->start_h) * factor) >> 8);
        /* The compositor scales the client buffer by cur_w/cur_h. A zero extent
         * here is a Ring 0 divide-by-zero (triple fault -> spontaneous reboot). */
        if (a->cur_w < 1) a->cur_w = 1;
        if (a->cur_h < 1) a->cur_h = 1;

        if (a->type == WINDOW_ANIM_OPEN || a->type == WINDOW_ANIM_RESTORE) {
            a->cur_alpha = 140 + ((116 * factor) >> 8);
        } else if (a->type == WINDOW_ANIM_MINIMIZE) {
            a->cur_alpha = 256 - ((166 * factor) >> 8);
        } else if (a->type == WINDOW_ANIM_CLOSE) {
            a->cur_alpha = 256 - ((256 * factor) >> 8);
        }

        /* Invalidate bounding box union */
        int min_x = old_x < a->cur_x ? old_x : a->cur_x;
        int min_y = old_y < a->cur_y ? old_y : a->cur_y;
        int max_x = (old_x + old_w > a->cur_x + a->cur_w) ? old_x + old_w : a->cur_x + a->cur_w;
        int max_y = (old_y + old_h > a->cur_y + a->cur_h) ? old_y + old_h : a->cur_y + a->cur_h;
        wm_invalidate_rect(min_x - 8, min_y - 8, (max_x - min_x) + 16, (max_y - min_y) + 16);

        if (finished) {
            a->active = 0;
            Window *w = wm_get_window(a->app_id);
            if (w) {
                if (a->type == WINDOW_ANIM_MINIMIZE) {
                    /* WM owns the saved rectangle/state and focus handoff. */
                    wm_minimize(a->app_id);
                } else if (a->type == WINDOW_ANIM_OPEN || a->type == WINDOW_ANIM_RESTORE) {
                    /* wm_open/wm_unminimize already established authoritative
                     * geometry. Finishing a visual animation must not demote
                     * a restored maximized window or undo a user action. */
                } else if (a->type == WINDOW_ANIM_CLOSE) {
                    w->open = 0;
                    w->visible = 0;
                }
            }
        }
    }

    if (g_dock_launch_app >= 0) {
        if (now_ms - g_dock_launch_start_ms >= 420) {
            g_dock_launch_app = -1;
        }
    }
}

void ui_anim_dock_launch(int app_id) {
    g_dock_launch_app = app_id;
    g_dock_launch_start_ms = wm_time_ms();
}

int ui_anim_dock_is_launching(int app_id) {
    return (g_dock_launch_app == app_id);
}

int ui_anim_dock_bounce_offset(int app_id, u32 now_ms) {
    if (g_dock_launch_app != app_id) return 0;
    u32 elapsed = now_ms - g_dock_launch_start_ms;
    if (elapsed >= 420) return 0;
    /* One gentle lift; the Dock renderer also uses this offset to scale the
     * icon up slightly, avoiding the abrupt multi-hop launch bounce. */
    int t = (int)((elapsed * 256u) / 420u);
    int arch = (t * (256 - t)) >> 8;
    return -((arch * 8) >> 6);
}
