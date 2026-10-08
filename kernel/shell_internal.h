#ifndef POLLIK_SHELL_INTERNAL_H
#define POLLIK_SHELL_INTERNAL_H
/* Private to desktop.c, compositor.c and input_dispatch.c. Never include in
 * clients, graphics, WM or the runtime. Desktop owns this interaction context;
 * raster targets, surface validity and PS/2 packet state remain module-private. */
#include "system.h"
#include "wm.h"

#define MINIMIZE_MS 150u
#define DRAG_THRESHOLD 4
#define TITLE_DOUBLECLICK_MS 350u
#define TITLE_DOUBLECLICK_DISTANCE 4
enum { DOCK_FILES_GAP = 24 };
#define windows g_windows
#define z_order g_z_order
static inline int window_width(int id) { return g_windows[id].width; }
static inline int window_height(int id) { return g_windows[id].height; }
static inline int active_app(void) { return wm_active_app(); }
typedef struct {
    int width, height, dirty, scene_dirty, theme, animations_mode, dock_zoom;
    int partial; /* pending repaint is a region, not the whole scene */
    int drag_app, drag, dx, dy, drag_moved, drag_start_mx, drag_start_my;
    int drag_old_x, drag_old_y, drag_old_w, drag_old_h;
    int resizing, resize_edges, resize_start_x, resize_start_y;
    int resize_start_w, resize_start_h, resize_start_mx, resize_start_my;
    u32 last_title_click_ms;
    int last_title_click_id, last_title_click_x, last_title_click_y;
    SnapTarget snap_preview;
    int context_app, hovered_btn, alttab_open, alttab_selected;
    int hover, hover_level[NUM_APPS];
} ShellState;
extern ShellState shell;
void request_scene_redraw(void);
void focus_app(int id);
void open_app(int id);
void dock_activate_app(int id);
void close_app(int id);
void minimize_app(int id);
void toggle_maximize(int id);
#define CONTEXT_DESKTOP_ITEM -2
void open_context_menu(int x, int y, int id, int file_slot);
void desktop_prompt_rename_selected(void);
void desktop_prompt_delete_selected(void);
int dock_hit(void);
int dock_is_visible(void);
int dock_is_pinned(int app_id);
void dock_set_pinned(int app_id, int pinned);
int dock_get_visible_apps(int *out_apps, int max_apps);
void open_dock_context_menu(int screen_x, int screen_y, int app_id);
int input_ctrl_held(void);
void dock_get_app_center(int id, int *cx, int *cy);
void dock_draw_pill(void);
void dock_draw_content(void);
void draw_app_vector_icon(int id, int x, int y, int size);
void desktop_draw_bar(void);
int desktop_bar_handle_click(int mx, int my);
int desktop_bar_handle_pointer(int mx, int my);
void desktop_draw_overlays(void);
void desktop_paint_wallpaper(u32 *buffer);
void desktop_paint_wallpaper_mode(u32 *buffer, int dark);
void desktop_paint_wallpaper_fallback(u32 *buffer, int dark);
void desktop_prepare_theme_wallpapers(u32 *dark_buffer, u16 *light_buffer,
                                     int light_width, int light_height);
void input_dispatch_init(void);
void input_dispatch_poll(void);
void cancel_interaction(int id);
int input_pointer_x(void);
int input_pointer_y(void);
int input_cursor_kind(void);
void input_set_pointer_acceleration(int enabled);
int input_get_pointer_acceleration(void);
void compositor_init(void);
void compositor_prepare_wallpapers(void);
void compositor_capture_admin_background(void);
void compositor_wallpaper_changed(void);
void compositor_paint(int full);
void compositor_draw_cursor(int full);
int compositor_cursor_size(void);
void compositor_set_cursor_size(int percent);
void compositor_invalidate(int id);
void compositor_invalidate_all_surfaces(void);
/* Repaint only the given window region (used for in-window animation so the
 * whole desktop is not recomposited every frame). */
void compositor_invalidate_animated(int id);
void compositor_invalidate_region(int x, int y, int w, int h);
void compositor_invalidate_client_region(int id, int x, int y, int w, int h);
void request_partial_redraw(int x, int y, int w, int h);
void compositor_invalidate_dock(void);
void compositor_minimize(int id);
void compositor_cancel_minimize(int id);
int compositor_minimizing(int id);
int compositor_animate(u32 now);
#endif
