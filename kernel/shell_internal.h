#ifndef POLLIK_SHELL_INTERNAL_H
#define POLLIK_SHELL_INTERNAL_H
/* Private to desktop.c, compositor.c and input_dispatch.c. Never include in
 * clients, graphics, WM or the runtime. Desktop owns this interaction context;
 * raster targets, surface validity and PS/2 packet state remain module-private. */
#include "system.h"
#include "wm.h"
#include "ui.h"
#include "graphics.h"
#include "gui/apps.h"
#include "gui/app_host.h"
#define MINIMIZE_MS 150u
#define DRAG_THRESHOLD 4
#define TITLE_DOUBLECLICK_MS 350u
#define TITLE_DOUBLECLICK_DISTANCE 4
#define windows g_windows
#define z_order g_z_order
static inline int window_width(int id) { return g_windows[id].width; }
static inline int window_height(int id) { return g_windows[id].height; }
static inline int active_app(void) { return wm_active_app(); }
typedef struct {
    int width, height, dirty, scene_dirty, theme, animations_mode;
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
void close_app(int id);
void minimize_app(int id);
void toggle_maximize(int id);
void open_context_menu(int x, int y, int id, int file_slot);
int dock_hit(void);
void dock_draw_pill(void);
void dock_draw_content(void);
void desktop_draw_bar(void);
void desktop_draw_overlays(void);
void desktop_paint_wallpaper(u32 *buffer);
void input_dispatch_init(void);
void input_dispatch_poll(void);
void cancel_interaction(int id);
int input_pointer_x(void);
int input_pointer_y(void);
int input_cursor_kind(void);
void compositor_init(void);
void compositor_paint(int full);
void compositor_draw_cursor(int full);
void compositor_invalidate(int id);
void compositor_minimize(int id);
void compositor_cancel_minimize(int id);
int compositor_minimizing(int id);
int compositor_animate(u32 now);
#endif
