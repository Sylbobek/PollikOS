#ifndef POLLIK_UI_H
#define POLLIK_UI_H

#include "system.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

/* =========================================================================
 * 1. THEME & DESIGN SYSTEM
 * ========================================================================= */
typedef struct {
    u32 background;
    u32 surface;
    u32 surface_secondary;
    u32 surface_elevated;
    u32 border;
    u32 border_subtle;
    u32 text;
    u32 text_secondary;
    u32 text_muted;
    u32 accent;
    u32 accent_hover;
    u32 selection;
    u32 selection_text;
    u32 danger;
    u32 danger_hover;
    u32 success;
    u32 warning;
} ThemeColors;

typedef struct {
    int corner_radius;
    int corner_radius_small;
    int corner_radius_large;
    int window_title_height;
    int menu_item_height;
    int button_height;
    int padding_small;
    int padding_medium;
    int padding_large;
} ThemeMetrics;

typedef enum {
    THEME_DARK = 0,
    THEME_LIGHT = 1
} ThemeMode;

void ui_init(void);
ThemeColors *ui_theme(void);
ThemeMetrics *ui_metrics(void);
ThemeMode ui_theme_mode(void);
void ui_set_theme_mode(ThemeMode mode);
void ui_toggle_theme_mode(void);
int ui_is_dark(void);

/* =========================================================================
 * 2. SYSTEM ICONS (PROCEDURAL VECTOR ENGINE)
 * ========================================================================= */
typedef enum {
    ICON_NONE = 0,
    ICON_FOLDER,
    ICON_FILE,
    ICON_TEXT,
    ICON_APP,
    ICON_IMAGE,
    ICON_DISK,
    ICON_TRASH,
    ICON_SETTINGS,
    ICON_TERMINAL,
    ICON_INFO,
    ICON_WARNING,
    ICON_DANGER,
    ICON_CHECK,
    ICON_CHEVRON_RIGHT,
    ICON_CLOSE
} IconKind;

void ui_draw_icon(IconKind kind, int x, int y, int size, u32 accent, u32 fg);

/* =========================================================================
 * 3. REUSABLE BUTTON COMPONENT
 * ========================================================================= */
typedef enum {
    UI_BTN_NORMAL = 0,
    UI_BTN_HOVER,
    UI_BTN_PRESSED,
    UI_BTN_DISABLED
} UiButtonState;

void ui_draw_button(int x, int y, int w, int h, const char *label, IconKind icon, UiButtonState state, int is_default, int is_danger);
int ui_button_hit(int x, int y, int w, int h, int px, int py);

/* =========================================================================
 * 4. REUSABLE MENU & CONTEXT MENU SYSTEM
 * ========================================================================= */
typedef enum {
    MENU_ITEM_NORMAL = 0,
    MENU_ITEM_SEPARATOR,
    MENU_ITEM_SUBMENU
} MenuItemKind;

typedef struct {
    MenuItemKind kind;
    int id;                   /* Action ID */
    const char *label;        /* "Open", "New Folder", etc. */
    const char *shortcut;     /* "Ctrl+N", "Del", etc. (or NULL) */
    IconKind icon;
    int enabled;
} UiMenuItem;

#define MAX_MENU_ITEMS 16

typedef struct {
    int active;
    int x, y, w, h;
    int item_count;
    UiMenuItem items[MAX_MENU_ITEMS];
    int hovered_index;        /* -1 if none */
    int selected_index;       /* keyboard or mouse selected, -1 if none */
    void (*on_select)(int action_id);
} UiMenu;

extern UiMenu g_active_menu;

void ui_menu_open(UiMenu *m, int x, int y, const UiMenuItem *items, int count, void (*on_select)(int));
void ui_menu_close(UiMenu *m);
void ui_draw_menu(UiMenu *m);
int ui_menu_on_key(UiMenu *m, int key, int shift);
int ui_menu_on_mouse_move(UiMenu *m, int mx, int my);
int ui_menu_on_mouse_down(UiMenu *m, int mx, int my, int button);

/* Standard Action IDs */
enum {
    ACTION_NONE = 0,
    /* Desktop Context Menu */
    ACTION_NEW_FOLDER,
    ACTION_DISPLAY_SETTINGS,
    ACTION_DESKTOP_SETTINGS,
    ACTION_ABOUT_POLLIKOS,
    /* File Context Menu */
    ACTION_OPEN,
    ACTION_OPEN_WITH,
    ACTION_RENAME,
    ACTION_COPY,
    ACTION_CUT,
    ACTION_DELETE,
    ACTION_PROPERTIES,
    /* Window Operations */
    ACTION_MINIMIZE,
    ACTION_MAXIMIZE,
    ACTION_CLOSE_WINDOW
};

/* =========================================================================
 * 5. REUSABLE DIALOG SYSTEM
 * ========================================================================= */
typedef enum {
    DIALOG_MESSAGE = 0,
    DIALOG_CONFIRM
} UiDialogKind;

typedef struct {
    int active;
    UiDialogKind kind;
    char title[48];
    char message[128];
    char confirm_label[24];
    char cancel_label[24];
    int is_danger;
    IconKind icon;
    int focused_btn;          /* 0 = Cancel, 1 = Confirm */
    void (*on_result)(int confirmed);
} UiDialog;

extern UiDialog g_active_dialog;

void ui_dialog_message(const char *title, const char *msg, IconKind icon, void (*on_close)(int));
void ui_dialog_confirm(const char *title, const char *msg, const char *confirm_label, const char *cancel_label, int is_danger, IconKind icon, void (*on_result)(int));
void ui_dialog_close(void);
void ui_draw_dialog(void);
int ui_dialog_on_key(int key);
int ui_dialog_on_mouse_move(int mx, int my);
int ui_dialog_on_mouse_down(int mx, int my, int button);

/* =========================================================================
 * 6. NOTIFICATION TOAST SYSTEM
 * ========================================================================= */
typedef struct {
    int active;
    char title[32];
    char message[64];
    IconKind icon;
    u32 start_ms;
    u32 duration_ms;
} UiNotification;

#define MAX_NOTIFICATIONS 4
extern UiNotification g_notifications[MAX_NOTIFICATIONS];

void ui_notify(const char *title, const char *message, IconKind icon);
void ui_draw_notifications(u32 now_ms);

/* =========================================================================
 * 7. LOW-LEVEL DRAWING BRIDGE PROTOTYPES
 * ========================================================================= */
void ui_bridge_rect(int x, int y, int w, int h, u32 color);
void ui_bridge_roundrect(int x, int y, int w, int h, int r, u32 color);
void ui_bridge_text(int x, int y, const char *s, u32 color, int scale);
int  ui_bridge_text_width(const char *s, int scale);
u32  ui_bridge_blend(u32 a, u32 b, int t);
int  ui_bridge_screen_width(void);
int  ui_bridge_screen_height(void);

#endif /* POLLIK_UI_H */
