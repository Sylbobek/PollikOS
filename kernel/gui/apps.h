#ifndef POLLIK_GUI_APPS_H
#define POLLIK_GUI_APPS_H
#include "../system.h"
/* Stable IDs; PollikMark occupies slot 6. */
enum { APP_WELCOME, APP_FILES, APP_TERMINAL, APP_NOTES, APP_SETTINGS, APP_BROWSER, APP_POLLIKMARK, APP_CALCULATOR,
#ifdef POLLIK_INSTALL_MEDIA
    APP_COUNT = APP_CALCULATOR /* Installer keeps its seven original slots. */
#else
    APP_COUNT
#endif
};
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
    /* Optional left-button client drag; returns -1 when it did not capture,
     * otherwise 1 when visible state changed or 0 when it did not. */
    int (*drag)(int x, int y, int active);
    void (*open)(void);
    void (*close)(void);
    void (*resize)(int width, int height);
    void (*scroll)(int delta);
    int (*cursor)(int x, int y);
    int (*poll)(void);
} GuiApp;
const GuiApp *gui_app_get(int id);
void gui_apps_init(void);
void app_clipboard_copy(const char *text, int length);
int app_clipboard_paste(char *out, int capacity);
void gui_app_render(int id, int width, int height, int active);
void gui_app_click(int id, int x, int y);
int gui_app_drag(int id, int x, int y, int active);
void gui_app_key(int id, u8 code, int shift, int control);
void gui_app_scroll(int id, int delta);
int gui_app_cursor(int id, int x, int y);
void gui_app_opened(int id);
void gui_app_closed(int id);
void gui_app_resized(int id, int width, int height);
/* Poll returns an app-ID bitmask; shell gates repainting on WM visibility.
 * Only apps whose bit is set in active_mask run their poll callback, so a
 * closed window never drives background work (browser_poll, media, etc.). */
u32 gui_apps_poll(void);
u32 gui_apps_poll_mask(u32 active_mask);

void welcome_render(int width, int height, int active);
void welcome_click(int x, int y);
int welcome_is_first_boot(void);
void welcome_mark_first_boot_done(void);
void files_render(int width, int height, int active);
void files_scroll(int delta);
void files_click(int x, int y);
int files_select_at(int x, int y);
void files_open_selected(void);
void files_delete_selected(void);
void files_key(u8 code);
int files_poll(void);
int files_preview_active(void);
void files_close(void);
int files_is_media(const char *name);
void files_open_image(const char *path, const char *name);
const char *files_selected_name(void);
void files_open_path(const char *path);
void notes_init(void);
void notes_render(int width, int height, int active);
void notes_resized(int width, int height);
int notes_cursor(int x, int y);
void notes_click(int x, int y);
int notes_drag(int x, int y, int active);
void notes_key(u8 code, char ch, int control);
void notes_key_ex(u8 code, char ch, int shift, int control);
void notes_scroll(int delta);
int notes_save(void);
void notes_open(int slot);
int notes_open_path(const char *path);
int notes_changed(void);
const char *notes_text(void);
int notes_selected_file(void);
void notes_select_file(int slot);
void terminal_render(int width, int height, int active);
void terminal_key(u8 code, char ch, int shift, int control);
void terminal_click(int x, int y);
int terminal_drag(int x, int y, int active);
void terminal_scroll(int delta);
void settings_render(int width, int height, int active);
void settings_click(int x, int y);
int settings_drag(int x,int y,int active);
void settings_close(void);
void calculator_init(void);
void calculator_close(void);
void calculator_render(int width,int height,int active);
void calculator_key(u8 code,char ch,int shift,int control);
void calculator_click(int x,int y);
void browser_client_init(void);
void browser_client_render(int width, int height, int active);
void browser_client_key(u8 code, char ch, int shift, int control);
void browser_client_resized(int width, int height);
int browser_client_poll(void);
void browser_client_scroll(int delta);
#endif
