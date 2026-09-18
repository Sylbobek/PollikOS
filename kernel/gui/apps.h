#ifndef POLLIK_GUI_APPS_H
#define POLLIK_GUI_APPS_H
#include "../system.h"
/* Stable IDs; PollikMark occupies slot 6. */
enum { APP_WELCOME, APP_FILES, APP_TERMINAL, APP_NOTES, APP_SETTINGS, APP_BROWSER, APP_POLLIKMARK, APP_COUNT };
enum { GUI_CHROME_HEIGHT = 34 };
typedef struct { int width, height; } GuiAppSize;
/* Last notified FULL WINDOW size; defaults to 680x410 before first resize. */
GuiAppSize gui_app_size(int id);
typedef struct {
    const u8 *indices, *alpha;
    const u32 *palette;
    int width, height;
} AppIcon;
typedef struct {
    const char *name;
    AppIcon icon;
    u32 body_active, body_inactive;
    int bottom_inset;
    int min_width, min_height; /* FULL WINDOW, including 34px chrome. */
    void (*init)(void);
    /* Local origin includes chrome; host notifies resize before painting.
     * Neither callback may service input/network or allocate per frame. */
    void (*render)(int width, int height, int active);
    void (*key)(u8 code, char ch, int shift, int control);
    void (*click)(int x, int y);
    void (*open)(void);
    void (*close)(void);
    void (*resize)(int width, int height);
    void (*scroll)(int delta);
    int (*cursor)(int x, int y);
    int (*poll)(void);
} GuiApp;
const GuiApp *gui_app_get(int id);
void gui_apps_init(void);
void gui_app_render(int id, int width, int height, int active);
void gui_app_click(int id, int x, int y);
void gui_app_key(int id, u8 code, int shift, int control);
void gui_app_scroll(int id, int delta);
int gui_app_cursor(int id, int x, int y);
void gui_app_opened(int id);
void gui_app_closed(int id);
void gui_app_resized(int id, int width, int height);
/* Poll returns an app-ID bitmask; shell gates repainting on WM visibility. */
u32 gui_apps_poll(void);

void welcome_render(int width, int height, int active);
void files_render(int width, int height, int active);
void files_scroll(int delta);
void files_click(int x, int y);
int files_select_at(int x, int y);
void files_open_selected(void);
void files_delete_selected(void);
const char *files_selected_name(void);
void notes_init(void);
void notes_render(int width, int height, int active);
void notes_resized(int width, int height);
int notes_cursor(int x, int y);
void notes_click(int x, int y);
void notes_key(u8 code, char ch, int control);
void notes_scroll(int delta);
int notes_save(void);
void notes_open(int slot);
int notes_changed(void);
const char *notes_text(void);
int notes_selected_file(void);
void notes_select_file(int slot);
void terminal_render(int width, int height, int active);
void terminal_key(u8 code, char ch, int control);
void settings_render(int width, int height, int active);
void settings_click(int x, int y);
void browser_client_init(void);
void browser_client_render(int width, int height, int active);
void browser_client_key(u8 code, char ch, int shift, int control);
void browser_client_resized(int width, int height);
int browser_client_poll(void);
void browser_client_scroll(int delta);
#endif
