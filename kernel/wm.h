#ifndef POLLIK_WM_H
#define POLLIK_WM_H

#include "system.h"

/* Stable window slots 0..6; must match the GUI registry APP_COUNT
 * (including APP_POLLIKMARK in slot 6), asserted in desktop.c. */
#ifdef POLLIK_INSTALL_MEDIA
#define NUM_APPS 7
#else
#define NUM_APPS 8
#endif

typedef enum {
    WINDOW_STATE_NORMAL = 0,
    WINDOW_STATE_MINIMIZED,
    WINDOW_STATE_MAXIMIZED,
    WINDOW_STATE_SNAPPED
} WindowState;

#define SURFACE_CANARY      0xDEADBEEF
#define MAX_WINDOW_WIDTH    2560
#define MAX_WINDOW_HEIGHT   1600

typedef struct {
    u32 *pixels;
    int width;
    int height;
    int stride;        /* Stride in pixels */
    int stride_pixels; /* Explicit alias for clarity */
    int dirty;
    u32 canary_head;
    u32 canary_tail;
    u32 content_version;
} WindowSurface;

/* Prefix/suffix guards surround screen-capacity drawable storage. Their
 * allocation bases and capacities are private to WM; pixels excludes guards.
 * The legacy canary fields above remain for the 36-byte i386 surface ABI. */
#define SURFACE_CANARY_WORDS 2

typedef struct {
    int id;
    int owner_pid;
    int x, y;
    int width, height;
    WindowState state;
    int open;
    int minimized;
    int focused;
    int visible;
    int dirty;
    int min_w, min_h;
    int max_w, max_h;
    int restore_x, restore_y;
    int restore_w, restore_h;
    const char *title;
} Window;

typedef struct {
    int x, y, w, h;
} Rect;

typedef struct {
    int x, y, w, h;
} WorkArea;

/* Hit Testing Enum */
typedef enum {
    HIT_NONE = 0,
    HIT_CLIENT,
    HIT_TITLEBAR,
    HIT_CLOSE,
    HIT_MINIMIZE,
    HIT_MAXIMIZE,
    HIT_RESIZE_LEFT,
    HIT_RESIZE_RIGHT,
    HIT_RESIZE_TOP,
    HIT_RESIZE_BOTTOM,
    HIT_RESIZE_TOP_LEFT,
    HIT_RESIZE_TOP_RIGHT,
    HIT_RESIZE_BOTTOM_LEFT,
    HIT_RESIZE_BOTTOM_RIGHT
} HitTestResult;

/* Cursor Types Enum */
typedef enum {
    CURSOR_DEFAULT = 0,
    CURSOR_IBEAM = 1,
    CURSOR_POINTER = 2,
    CURSOR_RESIZE_H = 3,
    CURSOR_RESIZE_V = 4,
    CURSOR_RESIZE_NWSE = 5,
    CURSOR_RESIZE_NESW = 6,
    CURSOR_BUSY = 7,
    CURSOR_MOVE = 8,
    CURSOR_NOT_ALLOWED = 9
} CursorKind;

/* Snapping Enum */
typedef enum {
    SNAP_NONE = 0,
    SNAP_LEFT,
    SNAP_RIGHT,
    SNAP_MAXIMIZE,
    SNAP_TOP_LEFT,
    SNAP_TOP_RIGHT,
    SNAP_BOTTOM_LEFT,
    SNAP_BOTTOM_RIGHT
} SnapTarget;

/* Resize edge masks */
#define RESIZE_NONE   0
#define RESIZE_RIGHT  1
#define RESIZE_BOTTOM 2
#define RESIZE_LEFT   4
#define RESIZE_TOP    8

/* Window Manager global state */
extern Window g_windows[NUM_APPS];
extern WindowSurface g_surfaces[NUM_APPS];
extern int g_z_order[NUM_APPS];
extern int g_focused_window;
extern int g_hovered_window;
extern int g_dragged_window;
extern int g_resized_window;
extern int g_resize_edges;

/* Window Manager lifecycle & operations */
void wm_init(int screen_w, int screen_h);
Window *wm_get_window(int id);
WindowSurface *wm_get_surface(int id);
int wm_active_app(void);
void wm_focus(int id);
void wm_unfocus(void);
void wm_bring_to_front(int id);
void wm_open(int id);
void wm_close(int id);
void wm_minimize(int id);
void wm_unminimize(int id);
void wm_toggle_maximize(int id);
void wm_maximize(int id);
void wm_restore(int id);
void wm_check_canaries(void);
void wm_render_test_pattern(u32 *dst, int w, int h, int stride);

/* Instantaneous geometry operations; invalid/closed/minimized IDs are no-ops.
 * Snap preserves the normal restore rectangle; undersized targets are declined.
 * SNAP_NONE restores normal geometry. These operations cancel WM animations. */
void wm_snap(int id, SnapTarget target);
/* Restore maximized/snapped geometry under the pointer. The caller must read
 * the resulting Window geometry to establish its drag origin and offsets. */
void wm_begin_drag_restore(int id, int px, int py);
/* Deltas are relative to the initial drag rectangle, NOT the previous frame.
 * Normal windows only; opposite edges stay fixed. Impossible axes are unchanged. */
void wm_resize_window(int id, Rect start, int edges, int delta_x, int delta_y);
/* Side-effect-free visible-window traversal in z-order. +1 walks toward the
 * back, -1 toward the front; wraps, excludes from_id, returns -1 if none.
 * An invalid from_id starts at the front (+1) or back (-1); zero returns -1.
 * Minimized windows are excluded; Alt-Tab policy remains with the caller. */
int wm_cycle_candidate(int from_id, int direction);

/* Unified Hit Testing and Snapping */
HitTestResult wm_hit_test(int id, int px, int py);
int wm_get_resize_edges(int id, int px, int py);
void wm_get_work_area(WorkArea *out_wa);
SnapTarget wm_check_snap(int px, int py);
void wm_get_snap_bounds(SnapTarget target, Rect *out_rect);

/* Invalidation & Damage Tracking V2 */
void wm_invalidate_rect(int x, int y, int w, int h);
void wm_invalidate_window(int id);
void wm_invalidate_titlebar(int id);
void wm_invalidate_all(void);
int wm_get_dirty_rects(Rect *out_rects, int max_rects);
void wm_clear_dirty(void);
int wm_is_full_redraw(void);
int wm_has_damage(void);

/* Time-based Animations */
typedef enum {
    ANIM_NONE = 0,
    ANIM_OPEN,
    ANIM_MINIMIZE,
    ANIM_RESTORE,
    ANIM_MAXIMIZE
} AnimType;

typedef struct {
    int active;
    AnimType type;
    int target_id;
    u32 start_time_ms;
    u32 duration_ms;
    int start_x, start_y, start_w, start_h;
    int end_x, end_y, end_w, end_h;
    int cur_x, cur_y, cur_w, cur_h;
} Animation;

extern Animation g_animations[NUM_APPS];
int wm_has_active_animations(void);
void wm_start_animation(int id, AnimType type, int start_x, int start_y, int start_w, int start_h,
                        int end_x, int end_y, int end_w, int end_h, u32 duration_ms);
void wm_update_animations(u32 now_ms);

/* Frame Scheduler (Target 60/120 FPS) */
int wm_frame_due(u32 now_ms);
void wm_frame_scheduled(u32 now_ms);
int wm_get_target_fps(void);
void wm_set_target_fps(int fps);

/* High-resolution Timing & Profiler */
void wm_timer_init(void);
u32 wm_time_ms(void);
/* Monotonic since timer initialization; PIT fallback reports coarse resolution. */
u64 wm_time_us(void);

typedef struct {
    u32 frame_count;
    u32 fps;
    u32 avg_frame_us;
    u32 p95_frame_us;
    u32 p99_frame_us;
    u32 worst_frame_us;
    u32 input_events_sec;
    u32 coalesced_mouse_sec;
    u32 client_paints_sec;
    u32 compositor_frames_sec;
    u32 presents_sec;
    u32 pixels_composed_sec;
    u32 pixels_presented_sec;
    u32 full_redraw_count;
    u32 damage_rects_count;
    u32 layout_us;
    u32 paint_us;
    u32 present_us;
    u32 input_us;
    u32 compose_us;
    u32 window_count;
    /* ABI v2: original 21 u32 fields above retain offsets. All durations are
     * elapsed wall time (including preemption), NOT exclusive CPU time.
     * History = last 128 completed frames; nearest-rank percentiles.
     * Intervals = consecutive present completions, including idle gaps. */
    u32 total_us, min_frame_us, max_frame_us, history_count;
    u32 interval_count, avg_interval_us, p95_interval_us, p99_interval_us;
    u32 low_1pct_fps, slow_1pct_interval_us, render_fps;
    u32 clock_source, clock_resolution_us, tsc_khz; /* 0=PIT, 1=calibrated TSC */
    /* Effective scene/cursor clip count and area; includes clip overdraw, not
     * per-layer blend operations. app_update_us, input_us and layout_us are
     * latest phase wall times measured by desktop_poll. Coalesced mouse/sec
     * remains zero: caller only reports motion packets. */
    u32 effective_rects, composed_pixels, presented_pixels;
    u32 cursor_frames, dock_frames, client_paint_count;
    u32 app_update_us;
    /* u64 tail; i386 alignment 4. Cumulative counters enable stage deltas. */
    u64 elapsed_us, total_time_us, paint_time_us, compose_time_us, present_time_us;
    u64 composed_pixels_total, presented_pixels_total;
    /* Appended to preserve every existing field offset. */
    u32 partial_frames;
} GuiPerfStats;

/* Single GUI-owner thread; copies stats, never exposes mutable storage to apps.
 * Snapshot computes history/rates, including zero FPS after idle. */
void wm_perf_snapshot(GuiPerfStats *out);
u32 wm_perf_copy_frame_times(u32 after_frame, u32 count, u32 *out, u32 capacity);
extern GuiPerfStats g_perf_stats;
void wm_perf_frame_begin(void);
void wm_perf_set_frame_kind(int frame_kind);
enum { GUI_PERF_FRAME_FULL = 1, GUI_PERF_FRAME_PARTIAL = 2,
       GUI_PERF_FRAME_CURSOR = 3, GUI_PERF_FRAME_DOCK = 4 };
void wm_perf_frame_end(u32 layout_us, u32 paint_us, u32 present_us, int is_full, u32 pixels_presented);
void wm_perf_record_input(int is_coalesced);
void wm_perf_record_phase_times(u32 app_update_us, u32 input_us, u32 layout_us);
int wm_perf_overlay_is_enabled(void);
int wm_perf_overlay_toggle(void);
void wm_perf_record_client_paint(void);
void wm_perf_summary(char *out, int max_len);

#endif
