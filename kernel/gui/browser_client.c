#include "apps.h"
#include "app_host.h"
#include "../browser/browser.h"

void browser_client_init(void) {
    browser_init(); g_browser.open = 1;
    g_browser.x = 0; g_browser.y = 0;
}
void browser_client_render(int width, int height, int active) {
    if (g_browser.w != width || g_browser.h != height) browser_client_resized(width, height);
    browser_render(active);
}
void browser_client_key(u8 code, char ch, int shift, int control) {
    if (control && code == 38) {
        browser_focus_address();
        return;
    }
    if (shift) {
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        else if (ch == '1') ch = '!';
        else if (ch == '2') ch = '@';
        else if (ch == '3') ch = '#';
        else if (ch == '4') ch = '$';
        else if (ch == '5') ch = '%';
        else if (ch == '6') ch = '^';
        else if (ch == '7') ch = '&';
        else if (ch == '8') ch = '*';
        else if (ch == '9') ch = '(';
        else if (ch == '0') ch = ')';
        else if (ch == '-') ch = '_';
        else if (ch == '=') ch = '+';
        else if (ch == ';') ch = ':';
        else if (ch == '\'') ch = '"';
        else if (ch == '/') ch = '?';
        else if (ch == '.') ch = '>';
        else if (ch == ',') ch = '<';
    }
    browser_handle_key(code, ch);
}
void browser_client_resized(int width, int height) {
    /* Geometry only: safe on the loader's cooperative presentation stack. */
    g_browser.x = 0; g_browser.y = 0;
    g_browser.w = width; g_browser.h = height;
    g_browser.layout_dirty = 1;
}
int browser_client_poll(void) {
    browser_poll();
    return g_browser.layout_dirty;
}
void browser_client_scroll(int delta) {
    browser_handle_scroll(delta * 40);
    app_host_invalidate(APP_BROWSER);
}
