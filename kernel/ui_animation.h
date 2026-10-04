#ifndef POLLIK_UI_ANIMATION_H
#define POLLIK_UI_ANIMATION_H

#include "system.h"
#include "gui/apps.h"

typedef enum {
    WINDOW_ANIM_NONE = 0,
    WINDOW_ANIM_OPEN,
    WINDOW_ANIM_MINIMIZE,
    WINDOW_ANIM_RESTORE,
    WINDOW_ANIM_CLOSE
} WindowAnimType;

typedef struct {
    int active;
    WindowAnimType type;
    int app_id;
    u32 start_ms;
    u32 duration_ms;
    int start_x, start_y, start_w, start_h;
    int end_x, end_y, end_w, end_h;
    int cur_x, cur_y, cur_w, cur_h;
    int cur_alpha; /* 0 .. 256 */
} WindowAnim;

/* Easing curves: t in 0 .. 256, returns 0 .. 256 */
int ease_out_cubic(int t);
int ease_in_cubic(int t);
int ease_in_out_cubic(int t);

void ui_anim_init(void);
void ui_anim_start_open(int app_id, int x, int y, int w, int h);
void ui_anim_start_minimize(int app_id, int dock_cx, int dock_cy);
void ui_anim_start_restore(int app_id, int dock_cx, int dock_cy, int target_x, int target_y, int target_w, int target_h);
void ui_anim_start_close(int app_id);
void ui_anim_cancel(int app_id);
int  ui_anim_has_active(void);
const WindowAnim *ui_anim_get(int app_id);
void ui_anim_update(u32 now_ms);

/* Dock launch animation */
void ui_anim_dock_launch(int app_id);
int  ui_anim_dock_is_launching(int app_id);
int  ui_anim_dock_bounce_offset(int app_id, u32 now_ms);

#endif
