#include "apps.h"

#include "pollikmark.h"
#include "../browser/browser.h"
#include "../ui_data.h"

static GuiAppSize sizes[APP_COUNT];
GuiAppSize gui_app_size(int id) {
    if (id >= 0 && id < APP_COUNT && sizes[id].width) return sizes[id];
    return (GuiAppSize){680, 410};
}
static void terminal_client_key(u8 code, char ch, int shift, int control) {
    if (shift && ch >= 'a' && ch <= 'z') ch -= 32;
    terminal_key(code, ch, shift, control);
}
static void notes_client_key(u8 code, char ch, int shift, int control) {
    if (shift && ch >= 'a' && ch <= 'z') ch -= 32;
    notes_key_ex(code, ch, shift, control);
}
static void notes_client_scroll(int delta) { notes_scroll(delta * 3); }
static int notes_client_drag(int x, int y, int active) { return notes_drag(x, y, active); }
static void terminal_client_scroll(int delta) { terminal_scroll(delta * 3); }
static int terminal_client_drag(int x, int y, int active) { return terminal_drag(x, y, active); }
int browser_client_drag(int x, int y, int active);
static void files_client_key(u8 code, char ch, int shift, int control) {
    (void)ch; (void)shift; (void)control;
    files_key(code);
}

/* Missing callbacks deliberately mean no-op, including close for persistent
 * Notes/Terminal state. Icon pixels and client decoration are registry-owned. */
#define ICON(id) { icons_index[id], icons_alpha[id], icons_palette[id], 72, 72 }
#define LIGHT_BODY .body_active = 0xfaf9fc, .body_inactive = 0xf4f2f7
#define MIN_SIZE .min_width = 480, .min_height = 280
static const GuiApp registry[APP_COUNT] = {
    [APP_WELCOME] = { .name = "Welcome To pollikos", .icon = ICON(APP_WELCOME), MIN_SIZE, LIGHT_BODY, .render = welcome_render, .click = welcome_click },
    [APP_FILES] = { .name = "Files", .icon = ICON(APP_FILES), MIN_SIZE, LIGHT_BODY,
        .render = files_render, .click = files_click, .scroll = files_scroll,
        .key = files_client_key, .poll = files_poll, .close = files_close },
    [APP_TERMINAL] = { .name = "Terminal", .icon = ICON(APP_TERMINAL), MIN_SIZE, .body_active = 0x202331, .body_inactive = 0x1a1c27,
        .render = terminal_render, .key = terminal_client_key, .click = terminal_click,
        .drag = terminal_client_drag, .scroll = terminal_client_scroll },
    [APP_NOTES] = { .name = "Notes", .icon = ICON(APP_NOTES), MIN_SIZE, LIGHT_BODY, .init = notes_init,
        .render = notes_render, .key = notes_client_key, .click = notes_click,
        .drag = notes_client_drag, .resize = notes_resized, .scroll = notes_client_scroll, .cursor = notes_cursor },
    [APP_SETTINGS] = { .name = "Settings", .icon = ICON(APP_SETTINGS), .min_width = 640, .min_height = 520, LIGHT_BODY,
        .render = settings_render, .click = settings_click },
    [APP_BROWSER] = { .name = "Browser", .icon = ICON(APP_BROWSER), MIN_SIZE, LIGHT_BODY, .bottom_inset = 18,
        .init = browser_client_init, .render = browser_client_render, .key = browser_client_key,
        .click = browser_handle_click, .drag = browser_client_drag, .open = browser_open, .close = browser_close,
        .resize = browser_client_resized, .scroll = browser_client_scroll,
        .cursor = browser_cursor, .poll = browser_client_poll },
    [APP_POLLIKMARK] = { .name = "PollikMark3D", .icon = {pollikmark_icon, pollikmark_alpha, pollikmark_palette, 16, 16},
        MIN_SIZE, .body_active = 0x202b40, .body_inactive = 0x202b40,
        .init = pollikmark_init, .render = pollikmark_render, .resize = pollikmark_resize,
        .open = pollikmark_open, .close = pollikmark_close, .key = pollikmark_key,
        .click = pollikmark_click, .poll = pollikmark_poll }
};

const GuiApp *gui_app_get(int id) { return id >= 0 && id < APP_COUNT ? &registry[id] : 0; }
void gui_apps_init(void) {
    for (int id = 0; id < APP_COUNT; id++) if (registry[id].init) registry[id].init();
}
void gui_app_render(int id, int width, int height, int active) {
    const GuiApp *app = gui_app_get(id);
    if (app && app->render) app->render(width, height, active);
}
void gui_app_click(int id, int x, int y) {
    const GuiApp *app = gui_app_get(id);
    if (app && app->click) app->click(x, y);
}
int gui_app_drag(int id, int x, int y, int active) {
    const GuiApp *app = gui_app_get(id);
    if (!app || !app->drag) return -1;
    return app->drag(x, y, active);
}
/* Preserve the original keyboard mapping (including the deliberately narrower
 * Shift mapping for Terminal/Notes). Hardware modifier tracking stays in host. */
static const char keys[128] = {
    [2] = '1',   [3] = '2',  [4] = '3',  [5] = '4',  [6] = '5',  [7] = '6',  [8] = '7',
    [9] = '8',   [10] = '9', [11] = '0', [12] = '-', [13] = '=', [16] = 'q', [17] = 'w',
    [18] = 'e',  [19] = 'r', [20] = 't', [21] = 'y', [22] = 'u', [23] = 'i', [24] = 'o',
    [25] = 'p',  [26] = '[', [27] = ']', [30] = 'a', [31] = 's', [32] = 'd', [33] = 'f',
    [34] = 'g',  [35] = 'h', [36] = 'j', [37] = 'k', [38] = 'l', [39] = ';', [40] = '\'',
    [43] = '\\', [44] = 'z', [45] = 'x', [46] = 'c', [47] = 'v', [48] = 'b', [49] = 'n',
    [50] = 'm',  [51] = ',', [52] = '.', [53] = '/', [57] = ' '};
void gui_app_key(int id, u8 code, int shift, int control) {
    const GuiApp *app = gui_app_get(id);
    char ch = code < sizeof(keys) ? keys[code] : 0;
    if (app && app->key) app->key(code, ch, shift, control);
}
void gui_app_scroll(int id, int delta) {
    const GuiApp *app = gui_app_get(id);
    if (app && app->scroll) app->scroll(delta);
}
int gui_app_cursor(int id, int x, int y) {
    const GuiApp *app = gui_app_get(id);
    return app && app->cursor ? app->cursor(x, y) : 0;
}
void gui_app_opened(int id) {
    const GuiApp *app = gui_app_get(id);
    if (app && app->open) app->open();
}
void gui_app_closed(int id) {
    const GuiApp *app = gui_app_get(id);
    if (app && app->close) app->close();
}
void gui_app_resized(int id, int width, int height) {
    const GuiApp *app = gui_app_get(id);
    if (!app || width <= 0 || height <= GUI_CHROME_HEIGHT) return;
    sizes[id] = (GuiAppSize){width, height};
    if (app->resize) app->resize(width, height);
}
u32 gui_apps_poll_mask(u32 active_mask) {
    u32 changed = 0;
    for (int id = 0; id < APP_COUNT; id++)
        if ((active_mask & (1u << id)) && registry[id].poll && registry[id].poll())
            changed |= 1u << id;
    return changed;
}
u32 gui_apps_poll(void) { return gui_apps_poll_mask(~0u); }
