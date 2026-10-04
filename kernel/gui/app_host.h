#ifndef POLLIK_APP_HOST_H
#define POLLIK_APP_HOST_H
#include "../system.h"

/* Ring0 GUI host contract, not a syscall ABI. Drawing always targets the
 * compositor-selected surface and uses window-local coordinates (titlebar
 * included, 34px chrome). render/resize dimensions and registry minima are
 * FULL WINDOW sizes, NOT client sizes. Host calls gui_app_resized(id,w,h)
 * before render on geometry changes, including maximize/restore/snap.
 * Common radius-12 exterior coverage belongs to the compositor; clients paint
 * opaque interiors (including Browser's bottom bar). Clients cannot select
 * targets or access compositor/WM globals. */
void ui_bridge_rect(int x, int y, int w, int h, u32 color);
void ui_bridge_roundrect(int x, int y, int w, int h, int r, u32 color);
void ui_bridge_roundrect_border(int x, int y, int w, int h, int r, int t, u32 color);
void ui_bridge_roundrect_stroke(int x, int y, int w, int h, int r, int t, u32 stroke, u32 fill);
void ui_bridge_rounded(int x, int y, int w, int h, int r, u32 color, int opacity);
void ui_bridge_blit_rgba(int x, int y, int w, int h, const u8 *rgba, int sw, int sh);
void ui_bridge_text(int x, int y, const char *s, u32 color, int scale);
void app_draw_centered(int x, int y, int w, const char *s, u32 color, int scale);
void app_draw_mono(int x, int y, const char *s, u32 color);
void app_draw_letter(int x, int y, u8 c, u32 color, int scale);

/* Browser-load scope only: bounded PS/2 drain, pointer/WM and presentation.
 * No app polling, commands, client input, menu/dialog callbacks or network I/O. */
void app_host_service_loading(void);
void app_host_open(int id);
void app_host_close(int id);
/* Invalidate a client and request a scene redraw; -1 requests only the scene. */
void app_host_invalidate(int id);
/* Invalidate a visible client and repaint only its window bounds. */
void app_host_invalidate_partial(int id);
/* Repaint only a changed client-local rectangle. */
void app_host_invalidate_partial_region(int id, int x, int y, int w, int h);
int app_host_theme(void);
void app_host_set_theme(int value);
int ui_is_dark(void);
int app_host_accent(void);
void app_host_set_accent(int index);
u32 app_host_accent_color(void);
u32 app_host_accent_hover(void);
int app_host_animations(void);
void app_host_set_animations(int enabled);
int app_host_dock_zoom(void);
void app_host_set_dock_zoom(int enabled);
int app_host_target_fps(void);
void app_host_set_target_fps(int fps);
int app_host_sound_muted(void);
void app_host_set_sound_muted(int muted);
u32 app_host_sound_freq(void);
void app_host_set_sound_freq(u32 freq);
void app_host_save_settings(void);
int app_host_wallpaper_count(void);
const char *app_host_wallpaper_name(int index);
const char *app_host_selected_wallpaper(void);
int app_host_set_wallpaper(int index);
int app_host_pointer_acceleration(void);
void app_host_set_pointer_acceleration(int enabled);
int app_host_cursor_size(void);
void app_host_set_cursor_size(int percent);
void app_host_stop_minimize(void);
void app_host_power(int reboot);
void app_host_perf_summary(char *out, int capacity);
/* Versioned value view, independent of the private WM layout. Cumulative
 * durations include preemption; frame count is compositor completions. */
typedef struct {
    u32 version, frames, presents, clock_resolution_us;
    u64 paint_us, compose_us, present_us, total_us;
} AppPerfView;
u64 app_host_time_us(void);
void app_host_metrics(AppPerfView *out);
u32 app_host_copy_frame_times(u32 after_frame, u32 count, u32 *out, u32 capacity);
u32 app_host_free_bytes(void);
void *app_host_alloc(u32 bytes);
void app_host_free(void *p, u32 bytes);
/* RGB888/u32 source; clips to current target and scissor, never scales. */
void app_host_blit(int x, int y, const u32 *src, int w, int h, int stride, u32 capacity);
#endif
