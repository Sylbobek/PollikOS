#include "wm.h"
#include "system.h"
#include "klog.h"
#include "audio.h"
#include "gui/apps.h"
#include "gui/app_host.h"

static int g_screen_w = 1024;
static int g_screen_h = 768;

WindowSurface g_surfaces[NUM_APPS];

Window g_windows[NUM_APPS] = {
    {0, 0, 170, 125, 680, 410, WINDOW_STATE_NORMAL, 1, 0, 1, 1, 0, 480, 280, 1920, 1200, 170, 125, 680, 410, "Welcome To pollikos"},
    {1, 0, 130,  85, 680, 410, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 480, 280, 1920, 1200, 130,  85, 680, 410, "Files"},
    {2, 0, 210, 145, 680, 410, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 480, 280, 1920, 1200, 210, 145, 680, 410, "Terminal"},
    {3, 0, 170, 125, 680, 410, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 480, 280, 1920, 1200, 170, 125, 680, 410, "Notes"},
    {4, 0, 170, 125, 680, 520, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 640, 520, 1920, 1200, 170, 125, 680, 520, "Settings"},
    {5, 0, 170, 125, 680, 410, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 480, 280, 1920, 1200, 170, 125, 680, 410, "Browser"},
    {6, 0, 170, 125, 680, 410, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 480, 280, 1920, 1200, 170, 125, 680, 410, "PollikMark3D"},
#ifndef POLLIK_INSTALL_MEDIA
    {7, 0, 260, 100, 360, 500, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 320, 440, 1920, 1200, 260, 100, 360, 500, "Calculator"},
    {8, 0, 240, 100, 680, 480, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 480, 280, 1920, 1200, 240, 100, 680, 480, "Photos"},
    {9, 0, 280, 120, 720, 480, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 480, 280, 1920, 1200, 280, 120, 720, 480, "Video"},
    {10, 0, 200, 100, 680, 480, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 480, 280, 1920, 1200, 200, 100, 680, 480, "Documents"}
#endif
};

/* Default geometry per slot; restored on close so reopening starts fresh. */
static const Window g_window_defaults[NUM_APPS] = {
    {0, 0, 170, 125, 680, 410, WINDOW_STATE_NORMAL, 1, 0, 1, 1, 0, 480, 280, 1920, 1200, 170, 125, 680, 410, "Welcome To pollikos"},
    {1, 0, 130,  85, 680, 410, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 480, 280, 1920, 1200, 130,  85, 680, 410, "Files"},
    {2, 0, 210, 145, 680, 410, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 480, 280, 1920, 1200, 210, 145, 680, 410, "Terminal"},
    {3, 0, 170, 125, 680, 410, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 480, 280, 1920, 1200, 170, 125, 680, 410, "Notes"},
    {4, 0, 170, 125, 680, 520, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 640, 520, 1920, 1200, 170, 125, 680, 520, "Settings"},
    {5, 0, 170, 125, 680, 410, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 480, 280, 1920, 1200, 170, 125, 680, 410, "Browser"},
    {6, 0, 170, 125, 680, 410, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 480, 280, 1920, 1200, 170, 125, 680, 410, "PollikMark3D"},
#ifndef POLLIK_INSTALL_MEDIA
    {7, 0, 260, 100, 360, 500, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 320, 440, 1920, 1200, 260, 100, 360, 500, "Calculator"},
    {8, 0, 240, 100, 680, 480, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 480, 280, 1920, 1200, 240, 100, 680, 480, "Photos"},
    {9, 0, 280, 120, 720, 480, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 480, 280, 1920, 1200, 280, 120, 720, 480, "Video"},
    {10, 0, 200, 100, 680, 480, WINDOW_STATE_NORMAL, 0, 0, 0, 0, 0, 480, 280, 1920, 1200, 200, 100, 680, 480, "Documents"}
#endif
};

int g_z_order[NUM_APPS] = {1, 2, 3, 4, 5, 6,
#ifndef POLLIK_INSTALL_MEDIA
    7, 8, 9, 10,
#endif
    0};
int g_focused_window = 0;
int g_hovered_window = -1;
int g_dragged_window = -1;
int g_resized_window = -1;
int g_resize_edges = RESIZE_NONE;

#define MAX_DIRTY_RECTS 16
static Rect g_dirty_rects[MAX_DIRTY_RECTS];
static int g_num_dirty_rects = 0;
static int g_full_redraw = 1;

Animation g_animations[NUM_APPS];
GuiPerfStats g_perf_stats;
static int g_perf_pending_frame_kind = GUI_PERF_FRAME_PARTIAL;
#ifndef POLLIK_INSTALL_MEDIA
static int g_perf_overlay_enabled;
#endif

/* Minimize is visibility-only: keep its snapshot separate from normal restore. */
static Rect g_minimized_rect[NUM_APPS];
static WindowState g_pre_minimized_state[NUM_APPS];

static int wm_is_visible(int id) {
    Window *w = wm_get_window(id);
    return w && w->open && w->visible && !w->minimized &&
           w->state != WINDOW_STATE_MINIMIZED;
}

static int wm_clamp(long long value, int low, int high) {
    return value < low ? low : (value > high ? high : (int)value);
}

static void wm_cancel_animation(int id) {
    Animation *a = &g_animations[id];
    if (a->active) {
        wm_invalidate_rect(a->cur_x - 8, a->cur_y - 8,
                           a->cur_w + 16, a->cur_h + 16);
    }
    a->active = 0;
    a->type = ANIM_NONE;
}

static void wm_set_rect(int id, Rect r) {
    Window *w = &g_windows[id];
    wm_invalidate_window(id);
    if (w->width != r.w || w->height != r.h) {
        w->dirty = 1;
        g_surfaces[id].dirty = 1;
    }
    w->x = r.x;
    w->y = r.y;
    w->width = r.w;
    w->height = r.h;
    wm_invalidate_window(id);
}

static void wm_save_normal_rect(Window *w) {
    if (w->state != WINDOW_STATE_NORMAL) return;
    w->restore_x = w->x;
    w->restore_y = w->y;
    w->restore_w = w->width;
    w->restore_h = w->height;
}

static void wm_release_interaction(int id) {
    if (g_hovered_window == id) g_hovered_window = -1;
    if (g_dragged_window == id) g_dragged_window = -1;
    if (g_resized_window == id) {
        g_resized_window = -1;
        g_resize_edges = RESIZE_NONE;
    }
}

/* PIT channel 0 is programmed with divisor 9943 in process.c.
 * No floating point, compiler 64-bit division helper, or invented GHz fallback. */
static u32 g_tsc_per_ms, g_clock_tick, g_last_frame_ms;
static u64 g_clock_ticks, g_clock_origin, g_clock_last;
static u64 g_frame_start, g_previous_present, g_rate_start;
static u32 g_sec_input_events, g_sec_coalesced_mouse, g_sec_client_paints;
static u32 g_sec_compositor_frames, g_sec_presents;
static u64 g_sec_pixels_composed, g_sec_pixels_presented;
#define PERF_HISTORY_SIZE 128
static u32 g_frame_time_history[PERF_HISTORY_SIZE];
static u32 g_frame_interval_history[PERF_HISTORY_SIZE];
static u32 g_frame_time_idx, g_interval_idx;

/* Two hardware divisions, including high quotient, so long uptimes are safe. */
static u64 perf_div(u64 n, u32 d) {
    u32 hi = (u32)(n >> 32), lo = (u32)n, qh, ql, rem;
    __asm__("divl %4" : "=a"(qh), "=d"(rem) : "a"(hi), "d"(0), "r"(d));
    __asm__("divl %4" : "=a"(ql), "=d"(rem) : "a"(lo), "d"(rem), "r"(d));
    return ((u64)qh << 32) | ql;
}
static u64 perf_tsc(void) {
    return (u64)hal_read_tsc_serialized();
}
static int perf_tick_wait(u32 start, u32 count) {
    /* Bounded even if IRQs/PIT are broken. */
    for (u32 limit = 50000000; limit; limit--) {
        if ((u32)(ticks - start) >= count) return 1;
        if (!(limit & 65535u)) app_host_service_loading();
        hal_cpu_relax();
    }
    return 0;
}
void wm_timer_init(void) {
    u32 a, b, c, d;
    g_tsc_per_ms = 0;
    if (hal_cpu_has_cpuid() && hal_interrupts_enabled()) {
        hal_cpuid(1, 0, &a, &b, &c, &d);
        if ((d & 16) && perf_tick_wait(ticks, 1)) {
            u64 begin = perf_tsc();
            if (perf_tick_wait(ticks, 12)) {
                u64 middle = perf_tsc();
                if (perf_tick_wait(ticks, 12)) {
                    u64 end = perf_tsc();
                    u32 r1 = (u32)perf_div((middle - begin) * 1193180u, 119316000u);
                    u32 r2 = (u32)perf_div((end - middle) * 1193180u, 119316000u);
                    if (end > middle && middle > begin && r1 >= 1000 && r1 <= 10000000 &&
                        r2 >= r1 - r1 / 20 && r2 <= r1 + r1 / 20)
                        g_tsc_per_ms = r1 / 2 + r2 / 2;
                }
            }
        }
    }
    g_clock_tick = ticks;
    g_clock_ticks = g_clock_last = 0;
    if (g_tsc_per_ms) g_clock_origin = perf_tsc();
    g_perf_stats.clock_source = g_tsc_per_ms ? 1 : 0;
    g_perf_stats.clock_resolution_us = g_tsc_per_ms ? 1 : 8334;
    g_perf_stats.tsc_khz = g_tsc_per_ms;
}
u32 wm_time_ms(void) { return (u32)perf_div(wm_time_us(), 1000); }
u64 wm_time_us(void) {
    u32 tick = ticks;
    g_clock_ticks += (u32)(tick - g_clock_tick);
    g_clock_tick = tick;
    u64 now;
    if (g_tsc_per_ms) {
        u64 tsc = perf_tsc();
        if (tsc < g_clock_origin) return g_clock_last;
        u64 delta = tsc - g_clock_origin;
        u64 ms = perf_div(delta, g_tsc_per_ms);
        now = ms * 1000 + perf_div((delta - ms * g_tsc_per_ms) * 1000, g_tsc_per_ms);
    } else now = perf_div(g_clock_ticks * 9943000000ull, 1193180);
    if (now < g_clock_last) now = g_clock_last;
    return g_clock_last = now;
}

#include "pmm.h"

/* Window surfaces are allocated dynamically from PMM (identity-mapped RAM).
 * No fixed base addresses: allocation happens in wm_init() based on the
 * actual screen size, so every supported resolution gets a correct slot. */
static uintptr_t g_surface_phys[NUM_APPS];
static u32 g_surface_pages[NUM_APPS];
static u32 g_surface_capacity[NUM_APPS];

/* Check before narrowing or page rounding on the 32-bit PMM interface. */
static u32 surface_slot_pages(int w, int h, u32 *capacity) {
    if (w <= 0 || h <= 0) return 0;
    u64 words = (u64)(u32)w * (u32)h;
    u64 bytes = (words + SURFACE_CANARY_WORDS) * sizeof(u32);
    if (bytes > 0xffffffffull - (PMM_PAGE_SIZE - 1u)) return 0;
    *capacity = (u32)words;
    return (u32)((bytes + PMM_PAGE_SIZE - 1u) / PMM_PAGE_SIZE);
}

void wm_init(int screen_w, int screen_h) {
    g_screen_w = screen_w;
    g_screen_h = screen_h;
    wm_timer_init();

    /* One guard on each side of the entire drawable screen capacity. */
    u32 capacity = 0;
    u32 slot_pages = surface_slot_pages(screen_w, screen_h, &capacity);
    if (!slot_pages) {
        serial("[WM] FATAL: invalid surface capacity\n");
        hal_cpu_halt_forever();
    }
    WorkArea wa;
    wm_get_work_area(&wa);

    for (int i = 0; i < NUM_APPS; i++) {
        const GuiApp *app = gui_app_get(i);
        g_windows[i].min_w = app ? app->min_width : 480;
        g_windows[i].min_h = app ? app->min_height : 280;
        g_windows[i].max_w = screen_w;
        g_windows[i].max_h = screen_h;
        g_pre_minimized_state[i] = WINDOW_STATE_NORMAL;
        g_animations[i].active = 0;
        g_animations[i].type = ANIM_NONE;
        klog_dec(KLOG_CAT_GUI, "Allocating surface: ", (u32)i);
        klog_dec(KLOG_CAT_GUI, "Requested pages: ", slot_pages);
        klog_dec(KLOG_CAT_GUI, "Free pages before surface: ", pmm_get_free_pages_count());
        uintptr_t phys = pmm_alloc_pages(slot_pages);
        if (!phys) {
            serial("[WM] FATAL: cannot allocate window surface ");
            char n[12];
            number(n, i);
            serial(n);
            serial("\n");
            hal_cpu_halt_forever();
        }
        memset((void *)phys, 0, slot_pages * PMM_PAGE_SIZE);
        ((u32 *)phys)[0] = SURFACE_CANARY;
        ((u32 *)phys)[capacity + 1] = SURFACE_CANARY;
        g_surface_phys[i] = phys;
        g_surface_pages[i] = slot_pages;
        g_surface_capacity[i] = capacity;

        g_surfaces[i].pixels = (u32 *)phys + 1;
        g_surfaces[i].width = g_windows[i].width;
        g_surfaces[i].height = g_windows[i].height;
        g_surfaces[i].stride = g_windows[i].width;
        g_surfaces[i].stride_pixels = g_windows[i].width;
        g_surfaces[i].dirty = 1;
        g_surfaces[i].canary_head = SURFACE_CANARY;
        g_surfaces[i].canary_tail = SURFACE_CANARY;
        g_surfaces[i].content_version = 0;
    }

    wm_invalidate_all();
}

void wm_check_canaries(void) {
    for (int i = 0; i < NUM_APPS; i++) {
        if (!g_surface_phys[i])
            continue;
        u32 *base = (u32 *)g_surface_phys[i];
        if (base[0] != SURFACE_CANARY ||
            base[g_surface_capacity[i] + 1] != SURFACE_CANARY) {
            serial("[GUI MEMORY CORRUPTION] surface=");
            char s_id[16];
            number(s_id, i);
            serial(s_id);
            serial(" corrupt!\n");
        }
    }
}

void wm_render_test_pattern(u32 *dst, int w, int h, int stride) {
    if (!dst || w <= 0 || h <= 0 || stride < w) return;
    int half_w = w / 2;
    int half_h = h / 2;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            u32 color;
            /* 4 quadrants: Top-Left=Red, Top-Right=Green, Bottom-Left=Blue, Bottom-Right=White */
            if (y < half_h) {
                color = (x < half_w) ? 0x00FF0000 : 0x0000FF00;
            } else {
                color = (x < half_w) ? 0x000000FF : 0x00FFFFFF;
            }
            /* Grid overlay: 16px horizontal and vertical lines */
            if ((x % 16 == 0) || (y % 16 == 0)) {
                color = 0x00202020;
            }
            dst[y * stride + x] = color;
        }
    }
}

Window *wm_get_window(int id) {
    if (id >= 0 && id < NUM_APPS) return &g_windows[id];
    return 0;
}

WindowSurface *wm_get_surface(int id) {
    if (id >= 0 && id < NUM_APPS) return &g_surfaces[id];
    return 0;
}

int wm_active_app(void) {
    return wm_is_visible(g_focused_window) ? g_focused_window : -1;
}

int wm_cycle_candidate(int from_id, int direction) {
    if (!direction) return -1;
    int idx = -1;
    for (int i = 0; i < NUM_APPS; i++) {
        if (g_z_order[i] == from_id && wm_get_window(from_id)) idx = i;
    }
    int step = direction > 0 ? -1 : 1;
    if (idx < 0) idx = direction > 0 ? 0 : NUM_APPS - 1;
    for (int i = 0; i < NUM_APPS; i++) {
        idx = (idx + step + NUM_APPS) % NUM_APPS;
        int candidate = g_z_order[idx];
        if (candidate != from_id && wm_is_visible(candidate)) return candidate;
    }
    return -1;
}

void wm_bring_to_front(int id) {
    if (!wm_is_visible(id)) return;
    int idx = -1;
    for (int i = 0; i < NUM_APPS; i++) {
        if (g_z_order[i] == id) {
            idx = i;
            break;
        }
    }
    if (idx < 0 || idx == NUM_APPS - 1) return;
    for (int i = idx; i < NUM_APPS - 1; i++) {
        g_z_order[i] = g_z_order[i + 1];
    }
    g_z_order[NUM_APPS - 1] = id;
    /* Every newly exposed client pixel, not just its titlebar, needs damage. */
    wm_invalidate_window(id);
}

void wm_focus(int id) {
    /* Invalid/hidden requests do not destroy the existing valid focus. */
    if (!wm_is_visible(id)) return;
    int old_focused = g_focused_window;
    if (old_focused != id && wm_get_window(old_focused)) {
        g_windows[old_focused].focused = 0;
        wm_invalidate_titlebar(old_focused);
    }
    g_focused_window = id;
    g_windows[id].focused = 1;
    /* Also raise a focused window that a caller has moved behind another. */
    wm_bring_to_front(id);
    wm_invalidate_titlebar(id);
}

void wm_unfocus(void) {
    if (wm_get_window(g_focused_window)) {
        g_windows[g_focused_window].focused = 0;
        wm_invalidate_titlebar(g_focused_window);
    }
    g_focused_window = -1;
}

static void wm_focus_frontmost(void) {
    int next = wm_cycle_candidate(-1, 1);
    if (next >= 0) wm_focus(next);
    else wm_unfocus();
}

void wm_open(int id) {
    Window *w = wm_get_window(id);
    if (!w) return;
    if (w->open && (w->minimized || w->state == WINDOW_STATE_MINIMIZED)) {
        wm_unminimize(id);
        return;
    }
    if (!w->open) {
        wm_cancel_animation(id);
        w->open = 1;
        w->minimized = 0;
    }
    /* Reopening/activating preserves normal, maximized, or snapped geometry. */
    w->visible = 1;
    wm_focus(id);
    wm_invalidate_window(id);
    audio_play_sound(SOUND_CLICK);
}

void wm_close(int id) {
    Window *w = wm_get_window(id);
    if (!w) return;
    wm_cancel_animation(id);
    wm_release_interaction(id);
    wm_invalidate_window(id);
    /* A closed minimized window must not reopen in MINIMIZED state. */
    if (w->minimized || w->state == WINDOW_STATE_MINIMIZED) {
        Rect r = g_minimized_rect[id];
        w->state = g_pre_minimized_state[id];
        if (r.w > 0 && r.h > 0) wm_set_rect(id, r);
    }
    /* Reset to the slot's default geometry so a later open starts fresh. */
    const Window *def = &g_window_defaults[id];
    w->x = def->x;
    w->y = def->y;
    w->width = def->width;
    w->height = def->height;
    w->state = WINDOW_STATE_NORMAL;
    w->restore_x = def->restore_x;
    w->restore_y = def->restore_y;
    w->restore_w = def->restore_w;
    w->restore_h = def->restore_h;
    g_pre_minimized_state[id] = WINDOW_STATE_NORMAL;
    g_minimized_rect[id] = (Rect){0, 0, 0, 0};
    w->dirty = 1;
    g_surfaces[id].dirty = 1;

    w->open = 0;
    w->visible = 0;
    w->minimized = 0;
    w->focused = 0;
    if (g_focused_window == id || !wm_is_visible(g_focused_window)) {
        wm_unfocus();
        wm_focus_frontmost();
    }
    audio_play_sound(SOUND_CLICK);
}

void wm_get_work_area(WorkArea *out_wa) {
    if (!out_wa) return;
    out_wa->x = 8;
    out_wa->y = 36;
    out_wa->w = g_screen_w - 16;
    out_wa->h = g_screen_h - 142;
}

SnapTarget wm_check_snap(int px, int py) {
    int b = 12;
    int is_left = (px <= b);
    int is_right = (px >= g_screen_w - b);
    int is_top = (py <= 40);
    int is_bottom = (py >= g_screen_h - 150);

    if (is_left && is_top) return SNAP_TOP_LEFT;
    if (is_left && is_bottom) return SNAP_BOTTOM_LEFT;
    if (is_right && is_top) return SNAP_TOP_RIGHT;
    if (is_right && is_bottom) return SNAP_BOTTOM_RIGHT;

    if (is_left) return SNAP_LEFT;
    if (is_right) return SNAP_RIGHT;
    if (is_top) return SNAP_MAXIMIZE;

    return SNAP_NONE;
}

void wm_get_snap_bounds(SnapTarget target, Rect *out_rect) {
    if (!out_rect) return;
    WorkArea wa;
    wm_get_work_area(&wa);

    switch (target) {
    case SNAP_LEFT:
        out_rect->x = wa.x;
        out_rect->y = wa.y;
        out_rect->w = (wa.w - 8) / 2;
        out_rect->h = wa.h;
        break;
    case SNAP_RIGHT:
        out_rect->x = wa.x + (wa.w - 8) / 2 + 8;
        out_rect->y = wa.y;
        out_rect->w = (wa.w - 8) / 2;
        out_rect->h = wa.h;
        break;
    case SNAP_MAXIMIZE:
        out_rect->x = 0;
        out_rect->y = 32;
        out_rect->w = g_screen_w;
        out_rect->h = (g_screen_h - 96) - 32;
        break;
    case SNAP_TOP_LEFT:
        out_rect->x = wa.x;
        out_rect->y = wa.y;
        out_rect->w = (wa.w - 8) / 2;
        out_rect->h = (wa.h - 8) / 2;
        break;
    case SNAP_TOP_RIGHT:
        out_rect->x = wa.x + (wa.w - 8) / 2 + 8;
        out_rect->y = wa.y;
        out_rect->w = (wa.w - 8) / 2;
        out_rect->h = (wa.h - 8) / 2;
        break;
    case SNAP_BOTTOM_LEFT:
        out_rect->x = wa.x;
        out_rect->y = wa.y + (wa.h - 8) / 2 + 8;
        out_rect->w = (wa.w - 8) / 2;
        out_rect->h = (wa.h - 8) / 2;
        break;
    case SNAP_BOTTOM_RIGHT:
        out_rect->x = wa.x + (wa.w - 8) / 2 + 8;
        out_rect->y = wa.y + (wa.h - 8) / 2 + 8;
        out_rect->w = (wa.w - 8) / 2;
        out_rect->h = (wa.h - 8) / 2;
        break;
    default:
        out_rect->x = 0; out_rect->y = 0; out_rect->w = 0; out_rect->h = 0;
        break;
    }
}

HitTestResult wm_hit_test(int id, int px, int py) {
    if (!wm_is_visible(id)) return HIT_NONE;
    Window *w = &g_windows[id];
    int wx = w->x;
    int wy = w->y;
    int ww = w->width;
    int wh = w->height;
    int inside = px >= wx && px < wx + ww && py >= wy && py < wy + wh;

    /* Painted controls are 12x12 at (12,11), (30,11), (48,11).
     * Include their full visible bounds plus 2px padding, BEFORE resize. */
    if (inside && py >= wy + 9 && py < wy + 25) {
        if (px >= wx + 10 && px < wx + 26) return HIT_CLOSE;
        if (px >= wx + 28 && px < wx + 44) return HIT_MINIMIZE;
        if (px >= wx + 46 && px < wx + 62) return HIT_MAXIMIZE;
    }

    if (w->state == WINDOW_STATE_NORMAL) {
        int b = 6;
        int cb = 14;
        if (px >= wx - b && px < wx + ww + b && py >= wy - b && py < wy + wh + b) {
            int on_l = (px < wx + b);
            int on_r = (px >= wx + ww - b);
            int on_t = (py < wy + b);
            int on_b = (py >= wy + wh - b);

            int corner_l = (px < wx + cb);
            int corner_r = (px >= wx + ww - cb);
            int corner_t = (py < wy + cb);
            int corner_b = (py >= wy + wh - cb);

            if (corner_t && corner_l) return HIT_RESIZE_TOP_LEFT;
            if (corner_t && corner_r) return HIT_RESIZE_TOP_RIGHT;
            if (corner_b && corner_l) return HIT_RESIZE_BOTTOM_LEFT;
            if (corner_b && corner_r) return HIT_RESIZE_BOTTOM_RIGHT;

            if (on_l) return HIT_RESIZE_LEFT;
            if (on_r) return HIT_RESIZE_RIGHT;
            if (on_t) return HIT_RESIZE_TOP;
            if (on_b) return HIT_RESIZE_BOTTOM;
        }
    }

    if (inside) return py < wy + 34 ? HIT_TITLEBAR : HIT_CLIENT;
    return HIT_NONE;
}

int wm_get_resize_edges(int id, int px, int py) {
    HitTestResult res = wm_hit_test(id, px, py);
    int edges = RESIZE_NONE;
    if (res == HIT_RESIZE_LEFT || res == HIT_RESIZE_TOP_LEFT || res == HIT_RESIZE_BOTTOM_LEFT)
        edges |= RESIZE_LEFT;
    if (res == HIT_RESIZE_RIGHT || res == HIT_RESIZE_TOP_RIGHT || res == HIT_RESIZE_BOTTOM_RIGHT)
        edges |= RESIZE_RIGHT;
    if (res == HIT_RESIZE_TOP || res == HIT_RESIZE_TOP_LEFT || res == HIT_RESIZE_TOP_RIGHT)
        edges |= RESIZE_TOP;
    if (res == HIT_RESIZE_BOTTOM || res == HIT_RESIZE_BOTTOM_LEFT || res == HIT_RESIZE_BOTTOM_RIGHT)
        edges |= RESIZE_BOTTOM;
    return edges;
}

static inline int ease_out_cubic(int t) {
    int inv = 256 - t;
    int inv3 = (inv * inv * inv) >> 16;
    return 256 - inv3;
}

void wm_start_animation(int id, AnimType type, int start_x, int start_y, int start_w, int start_h,
                        int end_x, int end_y, int end_w, int end_h, u32 duration_ms) {
    Window *w = wm_get_window(id);
    if (!w || !w->open) return;
    /* Compatibility entry point: never allow legacy minimize/restore calls to
     * shrink the actual window or bypass its pre-minimized snapshot. */
    if (type == ANIM_MINIMIZE) {
        wm_minimize(id);
        return;
    }
    if ((type == ANIM_RESTORE || type == ANIM_OPEN) &&
        (w->minimized || w->state == WINDOW_STATE_MINIMIZED)) {
        wm_unminimize(id);
        return;
    }
    if (!wm_is_visible(id) || type == ANIM_NONE ||
        start_w <= 0 || start_h <= 0 || end_w <= 0 || end_h <= 0 ||
        start_w > g_screen_w || end_w > g_screen_w ||
        start_h > g_screen_h || end_h > g_screen_h) return;
    wm_cancel_animation(id);
    Animation *a = &g_animations[id];
    a->active = 1;
    a->type = type;
    a->target_id = id;
    a->start_time_ms = wm_time_ms();
    a->duration_ms = duration_ms > 0 ? duration_ms : 150;
    a->start_x = start_x;
    a->start_y = start_y;
    a->start_w = start_w;
    a->start_h = start_h;
    a->end_x = end_x;
    a->end_y = end_y;
    a->end_w = end_w;
    a->end_h = end_h;
    a->cur_x = start_x;
    a->cur_y = start_y;
    a->cur_w = start_w;
    a->cur_h = start_h;
    wm_invalidate_rect(start_x - 8, start_y - 8, start_w + 16, start_h + 16);
}

int wm_has_active_animations(void) {
    for (int i = 0; i < NUM_APPS; i++) {
        if (g_animations[i].active) return 1;
    }
    return 0;
}

void wm_update_animations(u32 now_ms) {
    for (int i = 0; i < NUM_APPS; i++) {
        Animation *a = &g_animations[i];
        if (!a->active) continue;
        /* Stale or externally seeded animation slots cannot revive windows. */
        if (a->target_id != i || !wm_is_visible(i)) {
            wm_cancel_animation(i);
            continue;
        }
        if (a->type == ANIM_MINIMIZE) {
            wm_minimize(i);
            continue;
        }

        u32 elapsed = now_ms - a->start_time_ms;
        int finished = 0;
        int factor = 256;

        if (elapsed >= a->duration_ms) {
            finished = 1;
            factor = 256;
        } else {
            int t = (int)((elapsed * 256u) / a->duration_ms);
            factor = ease_out_cubic(t);
        }

        int old_x = a->cur_x;
        int old_y = a->cur_y;
        int old_w = a->cur_w;
        int old_h = a->cur_h;

        int new_x = a->start_x + (((a->end_x - a->start_x) * factor) >> 8);
        int new_y = a->start_y + (((a->end_y - a->start_y) * factor) >> 8);
        int new_w = a->start_w + (((a->end_w - a->start_w) * factor) >> 8);
        int new_h = a->start_h + (((a->end_h - a->start_h) * factor) >> 8);
        /* A zero-extent frame would make the compositor divide by ww/wh and
         * triple-fault Ring 0 (spontaneous reboot). Never let a frame reach 0. */
        if (new_w < 1) new_w = 1;
        if (new_h < 1) new_h = 1;

        a->cur_x = new_x;
        a->cur_y = new_y;
        a->cur_w = new_w;
        a->cur_h = new_h;

        Window *w = &g_windows[a->target_id];
        w->x = new_x;
        w->y = new_y;
        w->width = new_w;
        w->height = new_h;

        /* Invalidate union of old and new bounding boxes */
        int min_x = old_x < new_x ? old_x : new_x;
        int min_y = old_y < new_y ? old_y : new_y;
        int max_x = (old_x + old_w > new_x + new_w) ? old_x + old_w : new_x + new_w;
        int max_y = (old_y + old_h > new_y + new_h) ? old_y + old_h : new_y + new_h;
        wm_invalidate_rect(min_x - 8, min_y - 8, (max_x - min_x) + 16, (max_y - min_y) + 16);

        if (finished) {
            a->active = 0;
            if (a->type == ANIM_MINIMIZE) {
                w->state = WINDOW_STATE_MINIMIZED;
                w->minimized = 1;
                w->visible = 0;
            } else if (a->type == ANIM_RESTORE || a->type == ANIM_OPEN) {
                w->state = WINDOW_STATE_NORMAL;
                w->minimized = 0;
                w->visible = 1;
            } else if (a->type == ANIM_MAXIMIZE) {
                w->state = WINDOW_STATE_MAXIMIZED;
                w->minimized = 0;
                w->visible = 1;
            }
        }
    }
}

void wm_toggle_maximize(int id) {
    if (!wm_is_visible(id)) return;
    Window *w = &g_windows[id];
    if (w->state == WINDOW_STATE_MAXIMIZED || w->state == WINDOW_STATE_SNAPPED)
        wm_restore(id);
    else
        wm_maximize(id);
}

void wm_snap(int id, SnapTarget target) {
    if (!wm_is_visible(id)) return;
    if (target == SNAP_NONE) {
        wm_restore(id);
        return;
    }
    Window *w = &g_windows[id];
    Rect r;
    wm_get_snap_bounds(target, &r);
    /* Decline rather than overlap neighbors or violate a window's minimum.
     * This also handles work areas too small for a half/quarter/full window. */
    if (r.w < w->min_w || r.h < w->min_h ||
        r.w > w->max_w || r.h > w->max_h) return;
    wm_cancel_animation(id);
    wm_save_normal_rect(w);
    w->state = target == SNAP_MAXIMIZE ? WINDOW_STATE_MAXIMIZED : WINDOW_STATE_SNAPPED;
    wm_set_rect(id, r);
}

void wm_maximize(int id) {
    /* Instantaneous, including repeated requests; snapping never replaces the
     * normal restore rectangle with an already snapped/maximized rectangle. */
    wm_snap(id, SNAP_MAXIMIZE);
}

void wm_restore(int id) {
    if (!wm_is_visible(id)) return;
    Window *w = &g_windows[id];
    wm_cancel_animation(id);
    if (w->state != WINDOW_STATE_MAXIMIZED && w->state != WINDOW_STATE_SNAPPED) return;
    /* Zero is a valid restore coordinate. Only invalid sizes decline restore. */
    if (w->restore_w <= 0 || w->restore_h <= 0) return;
    Rect r = {w->restore_x, w->restore_y, w->restore_w, w->restore_h};
    w->state = WINDOW_STATE_NORMAL;
    wm_set_rect(id, r);
}

void wm_begin_drag_restore(int id, int px, int py) {
    if (!wm_is_visible(id)) return;
    Window *w = &g_windows[id];
    wm_cancel_animation(id);
    if (w->state != WINDOW_STATE_MAXIMIZED && w->state != WINDOW_STATE_SNAPPED) return;
    if (w->width <= 0 || w->restore_w <= 0 || w->restore_h <= 0) return;
    /* Preserve the horizontal pointer fraction and vertical titlebar offset. */
    int offset_x = wm_clamp((long long)px - w->x, 0, w->width);
    int offset_y = wm_clamp((long long)py - w->y, 0, 33);
    /* Screen-backed dimensions fit a 32-bit pixel product; no 64-bit division
     * runtime helper is required by this freestanding kernel. */
    int anchor_x = (int)(((unsigned)offset_x * (unsigned)w->restore_w) /
                         (unsigned)w->width);
    WorkArea wa;
    wm_get_work_area(&wa);
    Rect r = {w->restore_x, w->restore_y, w->restore_w, w->restore_h};
    int max_x = wa.x + wa.w - r.w;
    int max_y = wa.y + wa.h - r.h;
    r.x = wm_clamp((long long)px - anchor_x, wa.x, max_x < wa.x ? wa.x : max_x);
    r.y = wm_clamp((long long)py - offset_y, wa.y, max_y < wa.y ? wa.y : max_y);
    w->state = WINDOW_STATE_NORMAL;
    wm_set_rect(id, r);
    wm_save_normal_rect(w);
}

/* Clamp only the moving edge, preserving the opposite edge even after a
 * large overshoot. Reject an impossible axis (e.g. fixed edge off-work-area). */
static void wm_resize_axis(int pos, int size, int leading, int delta,
                           int area_pos, int area_size, int min_size, int max_size,
                           int *out_pos, int *out_size) {
    if (size <= 0 || area_size < min_size || max_size < min_size) return;
    long long end = (long long)area_pos + area_size;
    long long fixed = leading ? (long long)pos + size : pos;
    if (fixed < area_pos || fixed > end) return;
    int available = (int)(leading ? fixed - area_pos : end - fixed);
    int limit = max_size < available ? max_size : available;
    if (limit < min_size) return;
    int result = wm_clamp((long long)size + (leading ? -(long long)delta : delta),
                          min_size, limit);
    *out_size = result;
    *out_pos = leading ? (int)(fixed - result) : pos;
}

void wm_resize_window(int id, Rect start, int edges, int delta_x, int delta_y) {
    if (!wm_is_visible(id)) return;
    Window *w = &g_windows[id];
    if (w->state != WINDOW_STATE_NORMAL || start.w <= 0 || start.h <= 0) return;
    if (!edges || (edges & ~(RESIZE_LEFT | RESIZE_RIGHT | RESIZE_TOP | RESIZE_BOTTOM)) ||
        (edges & RESIZE_LEFT && edges & RESIZE_RIGHT) ||
        (edges & RESIZE_TOP && edges & RESIZE_BOTTOM)) return;
    wm_cancel_animation(id);
    WorkArea wa;
    wm_get_work_area(&wa);
    Rect r = {w->x, w->y, w->width, w->height};
    if (edges & (RESIZE_LEFT | RESIZE_RIGHT))
        wm_resize_axis(start.x, start.w, edges & RESIZE_LEFT, delta_x,
                       wa.x, wa.w, w->min_w, w->max_w, &r.x, &r.w);
    if (edges & (RESIZE_TOP | RESIZE_BOTTOM))
        wm_resize_axis(start.y, start.h, edges & RESIZE_TOP, delta_y,
                       wa.y, wa.h, w->min_h, w->max_h, &r.y, &r.h);
    wm_set_rect(id, r);
    wm_save_normal_rect(w);
}

void wm_minimize(int id) {
    if (!wm_is_visible(id)) return;
    Window *w = &g_windows[id];
    wm_cancel_animation(id);
    g_minimized_rect[id] = (Rect){w->x, w->y, w->width, w->height};
    g_pre_minimized_state[id] = w->state;
    wm_invalidate_window(id);
    wm_release_interaction(id);
    w->state = WINDOW_STATE_MINIMIZED;
    w->minimized = 1;
    w->visible = 0;
    w->focused = 0;
    /* x/y/width/height AND the normal restore rectangle are untouched. */
    if (g_focused_window == id || !wm_is_visible(g_focused_window)) {
        wm_unfocus();
        wm_focus_frontmost();
    }
}

void wm_unminimize(int id) {
    Window *w = wm_get_window(id);
    if (!w || !w->open) return;
    if (!w->minimized && w->state != WINDOW_STATE_MINIMIZED) return;
    wm_cancel_animation(id);
    Rect r = g_minimized_rect[id];
    if (r.w <= 0 || r.h <= 0) return;
    w->state = g_pre_minimized_state[id];
    w->minimized = 0;
    w->visible = 1;
    wm_set_rect(id, r);
    wm_focus(id);
}

void wm_invalidate_all(void) {
    g_full_redraw = 1;
    g_num_dirty_rects = 0;
}

void wm_invalidate_rect(int x, int y, int w, int h) {
    if (g_full_redraw) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g_screen_w) w = g_screen_w - x;
    if (y + h > g_screen_h) h = g_screen_h - y;
    if (w <= 0 || h <= 0) return;

    /* Check for merge with existing rects */
    for (int i = 0; i < g_num_dirty_rects; i++) {
        Rect *r = &g_dirty_rects[i];
        if (x <= r->x + r->w + 8 && x + w + 8 >= r->x &&
            y <= r->y + r->h + 8 && y + h + 8 >= r->y) {
            int min_x = x < r->x ? x : r->x;
            int min_y = y < r->y ? y : r->y;
            int max_x = (x + w > r->x + r->w) ? x + w : r->x + r->w;
            int max_y = (y + h > r->y + r->h) ? y + h : r->y + r->h;
            r->x = min_x;
            r->y = min_y;
            r->w = max_x - min_x;
            r->h = max_y - min_y;
            return;
        }
    }

    if (g_num_dirty_rects < MAX_DIRTY_RECTS) {
        g_dirty_rects[g_num_dirty_rects].x = x;
        g_dirty_rects[g_num_dirty_rects].y = y;
        g_dirty_rects[g_num_dirty_rects].w = w;
        g_dirty_rects[g_num_dirty_rects].h = h;
        g_num_dirty_rects++;
    } else {
        /* Merge into first rect */
        Rect *r = &g_dirty_rects[0];
        int min_x = x < r->x ? x : r->x;
        int min_y = y < r->y ? y : r->y;
        int max_x = (x + w > r->x + r->w) ? x + w : r->x + r->w;
        int max_y = (y + h > r->y + r->h) ? y + h : r->y + r->h;
        r->x = min_x;
        r->y = min_y;
        r->w = max_x - min_x;
        r->h = max_y - min_y;
    }

    u32 total_area = 0;
    for (int i = 0; i < g_num_dirty_rects; i++) {
        total_area += (u32)g_dirty_rects[i].w * g_dirty_rects[i].h;
    }
    if (total_area > (u32)(g_screen_w * g_screen_h * 8 / 10)) {
        g_full_redraw = 1;
        g_num_dirty_rects = 0;
    }
}

void wm_invalidate_window(int id) {
    if (id >= 0 && id < NUM_APPS) {
        Window *w = &g_windows[id];
        wm_invalidate_rect(w->x - 8, w->y - 8, w->width + 16, w->height + 16);
    }
}

void wm_invalidate_titlebar(int id) {
    if (id >= 0 && id < NUM_APPS) {
        Window *w = &g_windows[id];
        wm_invalidate_rect(w->x - 4, w->y - 4, w->width + 8, 44);
    }
}

int wm_get_dirty_rects(Rect *out_rects, int max_rects) {
    if (g_full_redraw) return 0;
    int count = g_num_dirty_rects < max_rects ? g_num_dirty_rects : max_rects;
    for (int i = 0; i < count; i++) {
        out_rects[i] = g_dirty_rects[i];
    }
    return count;
}

void wm_clear_dirty(void) {
    g_full_redraw = 0;
    g_num_dirty_rects = 0;
}

int wm_is_full_redraw(void) {
    return g_full_redraw;
}

int wm_has_damage(void) {
    return g_full_redraw || (g_num_dirty_rects > 0);
}

static int g_target_fps = 120;

int wm_get_target_fps(void) {
    return g_target_fps;
}

void wm_set_target_fps(int fps) {
    g_target_fps = (fps <= 60) ? 60 : 120;
}

int wm_frame_due(u32 now_ms) {
    u32 interval = (g_target_fps <= 60) ? 16 : 8;
    return (now_ms - g_last_frame_ms >= interval);
}

void wm_frame_scheduled(u32 now_ms) {
    g_last_frame_ms = now_ms;
}

void wm_perf_frame_begin(void) { g_frame_start = wm_time_us(); }

void wm_perf_set_frame_kind(int frame_kind) { g_perf_pending_frame_kind = frame_kind; }

void wm_perf_record_input(int is_coalesced) {
    g_sec_input_events++;
    /* Current caller passes "packet moved", NOT a coalesced-event count.
     * Retain the ABI but do not fabricate a coalescing measurement. */
    (void)is_coalesced;
}

void wm_perf_record_phase_times(u32 app_update_us, u32 input_us, u32 layout_us) {
    g_perf_stats.app_update_us = app_update_us;
    g_perf_stats.input_us = input_us;
    g_perf_stats.layout_us = layout_us;
}

#ifdef POLLIK_INSTALL_MEDIA
int wm_perf_overlay_is_enabled(void) { return 0; }
int wm_perf_overlay_toggle(void) { return 0; }
#else
int wm_perf_overlay_is_enabled(void) { return g_perf_overlay_enabled; }
int wm_perf_overlay_toggle(void) { return g_perf_overlay_enabled = !g_perf_overlay_enabled; }
#endif

void wm_perf_record_client_paint(void) {
    g_sec_client_paints++;
    g_perf_stats.client_paint_count++;
}

static void perf_rates(u64 now) {
    u64 elapsed = now - g_rate_start;
    if (elapsed < 1000000) return;
    u32 ms = (u32)perf_div(elapsed, 1000);
    g_perf_stats.fps = (u32)perf_div((u64)g_sec_presents * 1000, ms);
    g_perf_stats.input_events_sec = (u32)perf_div((u64)g_sec_input_events * 1000, ms);
    g_perf_stats.coalesced_mouse_sec = (u32)perf_div((u64)g_sec_coalesced_mouse * 1000, ms);
    g_perf_stats.client_paints_sec = (u32)perf_div((u64)g_sec_client_paints * 1000, ms);
    g_perf_stats.compositor_frames_sec = (u32)perf_div((u64)g_sec_compositor_frames * 1000, ms);
    g_perf_stats.presents_sec = g_perf_stats.fps;
    g_perf_stats.pixels_composed_sec = (u32)perf_div(g_sec_pixels_composed * 1000, ms);
    g_perf_stats.pixels_presented_sec = (u32)perf_div(g_sec_pixels_presented * 1000, ms);
    g_sec_input_events = g_sec_coalesced_mouse = g_sec_client_paints = 0;
    g_sec_compositor_frames = g_sec_presents = 0;
    g_sec_pixels_composed = g_sec_pixels_presented = 0;
    g_rate_start = now;
}

static u32 perf_history(const u32 *history, u32 count, u32 *sorted) {
    u64 sum = 0;
    for (u32 i = 0; i < count; i++) {
        u32 v = history[i], j = i;
        while (j && sorted[j - 1] > v) { sorted[j] = sorted[j - 1]; j--; }
        sorted[j] = v;
        sum += v;
    }
    return count ? (u32)perf_div(sum, count) : 0;
}

u32 wm_perf_copy_frame_times(u32 after_frame, u32 count, u32 *out, u32 capacity) {
    if (!out || !capacity || !count) return 0;
    u32 end = after_frame + count;
    if (end > g_perf_stats.frame_count) end = g_perf_stats.frame_count;
    u32 first = after_frame + 1;
    u32 oldest = g_perf_stats.frame_count > PERF_HISTORY_SIZE
        ? g_perf_stats.frame_count - PERF_HISTORY_SIZE + 1 : 1;
    if (first < oldest) first = oldest;
    if (end < first) return 0;
    u32 available = end - first + 1;
    if (available > capacity) {
        first = end - capacity + 1;
        available = capacity;
    }
    for (u32 i = 0; i < available; ++i) {
        u32 frame = first + i;
        out[i] = g_frame_time_history[(frame - 1) % PERF_HISTORY_SIZE];
    }
    return available;
}

void wm_perf_snapshot(GuiPerfStats *out) {
    if (!out) return;
    perf_rates(wm_time_us());
    u32 sorted[PERF_HISTORY_SIZE], n = g_perf_stats.history_count;
    g_perf_stats.avg_frame_us = perf_history(g_frame_time_history, n, sorted);
    if (n) {
        g_perf_stats.min_frame_us = sorted[0];
        g_perf_stats.max_frame_us = sorted[n - 1];
        g_perf_stats.p95_frame_us = sorted[(n * 95 + 99) / 100 - 1];
        g_perf_stats.p99_frame_us = sorted[(n * 99 + 99) / 100 - 1];
    }
    g_perf_stats.render_fps = g_perf_stats.avg_frame_us ? 1000000 / g_perf_stats.avg_frame_us : 0;
    n = g_perf_stats.interval_count;
    g_perf_stats.avg_interval_us = perf_history(g_frame_interval_history, n, sorted);
    if (n) {
        g_perf_stats.p95_interval_us = sorted[(n * 95 + 99) / 100 - 1];
        g_perf_stats.p99_interval_us = sorted[(n * 99 + 99) / 100 - 1];
        u32 slow = (n + 99) / 100;
        u64 sum = 0;
        for (u32 i = n - slow; i < n; i++) sum += sorted[i];
        g_perf_stats.slow_1pct_interval_us = (u32)perf_div(sum, slow);
        g_perf_stats.low_1pct_fps = g_perf_stats.slow_1pct_interval_us ?
            1000000 / g_perf_stats.slow_1pct_interval_us : 0;
    }
    *out = g_perf_stats;
}

void wm_perf_frame_end(u32 layout_us, u32 paint_us, u32 present_us, int is_full, u32 pixels_presented) {
    if (!pixels_presented) {
        g_perf_pending_frame_kind = GUI_PERF_FRAME_PARTIAL;
        return;
    }
    u64 now = wm_time_us();
    u32 frame_us = (u32)(now - g_frame_start);
    perf_rates(now);
    g_perf_stats.frame_count++;
    g_sec_compositor_frames++;
    g_sec_presents++;
    g_sec_pixels_presented += pixels_presented;
    g_sec_pixels_composed += g_perf_stats.composed_pixels;
    g_perf_stats.elapsed_us = now;
    g_perf_stats.total_us = frame_us;
    if (layout_us) g_perf_stats.layout_us = layout_us;
    g_perf_stats.paint_us = paint_us;
    g_perf_stats.present_us = present_us;
    g_perf_stats.compose_us = frame_us - paint_us - present_us;
    g_perf_stats.presented_pixels = pixels_presented;
    g_perf_stats.total_time_us += frame_us;
    g_perf_stats.paint_time_us += paint_us;
    g_perf_stats.compose_time_us += g_perf_stats.compose_us;
    g_perf_stats.present_time_us += present_us;
    g_perf_stats.composed_pixels_total += g_perf_stats.composed_pixels;
    g_perf_stats.presented_pixels_total += pixels_presented;
    g_perf_stats.damage_rects_count += g_perf_stats.effective_rects;
    if (is_full) g_perf_stats.full_redraw_count++;
    else if (g_perf_pending_frame_kind == GUI_PERF_FRAME_CURSOR) g_perf_stats.cursor_frames++;
    else if (g_perf_pending_frame_kind == GUI_PERF_FRAME_DOCK) g_perf_stats.dock_frames++;
    else g_perf_stats.partial_frames++;
    g_perf_pending_frame_kind = GUI_PERF_FRAME_PARTIAL;
    if (frame_us > g_perf_stats.worst_frame_us) g_perf_stats.worst_frame_us = frame_us;
    g_frame_time_history[g_frame_time_idx] = frame_us;
    g_frame_time_idx = (g_frame_time_idx + 1) % PERF_HISTORY_SIZE;
    if (g_perf_stats.history_count < PERF_HISTORY_SIZE) g_perf_stats.history_count++;
    if (g_previous_present) {
        u64 interval = now - g_previous_present;
        g_frame_interval_history[g_interval_idx] = interval > 0xffffffffu ? 0xffffffffu : (u32)interval;
        g_interval_idx = (g_interval_idx + 1) % PERF_HISTORY_SIZE;
        if (g_perf_stats.interval_count < PERF_HISTORY_SIZE) g_perf_stats.interval_count++;
    }
    g_previous_present = now;
    g_perf_stats.window_count = 0;
    for (int i = 0; i < NUM_APPS; i++) if (wm_is_visible(i)) g_perf_stats.window_count++;
}

void wm_perf_summary(char *out, int max_len) {
    if (!out || max_len <= 0) return;
    GuiPerfStats s;
    wm_perf_snapshot(&s);
    /* Fixed labels and unsigned decimal; bounded including NUL for size 1.
     * Latest stage phase values are wall time, not exclusive CPU time. */
    const char *labels[] = {"FPS: ", " render/s: ", "\nFrame ms: ", " avg: ", " p95: ",
        " p99: ", " dirty px: ", "\nInput us: ", " app: ", " layout: ",
        " draw: ", " compose: ", " LFB: "};
    u32 values[] = {s.fps, s.render_fps, s.total_us / 1000u, s.avg_frame_us / 1000u,
        s.p95_frame_us / 1000u, s.p99_frame_us / 1000u, s.composed_pixels,
        s.input_us, s.app_update_us, s.layout_us, s.paint_us, s.compose_us, s.present_us};
    int pos = 0;
    for (int i = 0; i < 13; i++) {
        for (const char *p = labels[i]; *p && pos < max_len - 1; p++) out[pos++] = *p;
        char digits[10];
        int n = 0;
        u32 v = values[i];
        do { digits[n++] = '0' + v % 10; v /= 10; } while (v);
        while (n && pos < max_len - 1) out[pos++] = digits[--n];
    }
    out[pos] = 0;
}
