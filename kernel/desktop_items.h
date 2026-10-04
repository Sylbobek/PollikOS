#ifndef POLLIK_DESKTOP_ITEMS_H
#define POLLIK_DESKTOP_ITEMS_H

#include "system.h"
#include "gui/apps.h"

#define DESKTOP_MAX_ITEMS 64

typedef enum {
    ITEM_APP = 0,
    ITEM_DIR,
    ITEM_TXT,
    ITEM_FILE,
    ITEM_TRASH
} DesktopItemType;

typedef struct {
    DesktopItemType type;
    char name[64];
    char path[128];
    int app_id; /* For ITEM_APP, -1 otherwise */
    int col, row;
    int x, y, w, h;
    int selected;
    u32 size;
    u32 modified;
} DesktopItem;

void desktop_items_init(void);
void desktop_items_scan(void);
void desktop_items_draw(void);

int  desktop_items_hit_test(int mx, int my);
void desktop_items_select(int idx);
int  desktop_items_get_selected(void);
const DesktopItem *desktop_items_get(int idx);
int  desktop_items_count(void);

/* Multi-selection */
void desktop_items_clear_selection(void);
void desktop_items_select_single(int idx);
void desktop_items_toggle_select(int idx);
int  desktop_items_is_selected(int idx);
int  desktop_items_selected_count(void);
int  desktop_items_get_first_selected(void);
int  desktop_items_move_selected_to_trash(void);

void desktop_items_open(int idx);
int  desktop_items_create_folder(void);
int  desktop_items_create_text_file(void);
int  desktop_items_rename(int idx, const char *new_name);
int  desktop_items_move_to_trash(int idx);

/* Drag & Drop */
void desktop_items_drag_start(int idx, int mx, int my);
void desktop_items_drag_move(int mx, int my);
void desktop_items_drag_end(int mx, int my);
int  desktop_items_is_dragging(void);
int  desktop_items_drag_moved(void);
int  desktop_items_trash_hit(int mx, int my);
int  desktop_items_is_trash_hovered(void);

/* Marquee Selection */
void desktop_items_marquee_start(int mx, int my);
void desktop_items_marquee_update(int mx, int my, int ctrl_held);
void desktop_items_marquee_end(void);
int  desktop_items_marquee_is_active(void);
void desktop_items_marquee_get_rect(int *rx, int *ry, int *rw, int *rh);

#endif

