#include "app_internal.h"
#include "../account.h"

static char clipboard[1024];
static int clipboard_len;

void app_clipboard_copy(const char *text, int length) {
    if (!text || length < 0) length = 0;
    if(!length) account_wipe(clipboard,sizeof(clipboard));
    if (length > (int)sizeof(clipboard) - 1) length = (int)sizeof(clipboard) - 1;
    for (int i = 0; i < length; i++) clipboard[i] = text[i];
    clipboard[length] = 0;
    clipboard_len = length;
}

int app_clipboard_paste(char *out, int capacity) {
    if (!out || capacity <= 0) return 0;
    int count = clipboard_len;
    if (count >= capacity) count = capacity - 1;
    for (int i = 0; i < count; i++) out[i] = clipboard[i];
    out[count] = 0;
    return count;
}

int app_edit_key(char *buf, int *n, int max, u8 code, char ch, int control) {
    if (control && code == 46) {
        app_clipboard_copy(buf, *n);
        return 0;
    }
    if (control && code == 47) {
        char pasted[sizeof(clipboard)];
        int count = app_clipboard_paste(pasted, sizeof(pasted));
        for (int i = 0; i < count && *n < max; i++) {
            buf[(*n)++] = pasted[i];
            buf[*n] = 0;
        }
        return 1;
    }
    if (code == 14) {
        if (*n) buf[--*n] = 0;
        return 1;
    }
    if (code == 28) {
        if (*n < max) buf[(*n)++] = '\n';
        buf[*n] = 0;
        return 1;
    }
    if (ch && *n < max) {
        buf[(*n)++] = ch;
        buf[*n] = 0;
        return 1;
    }
    return 0;
}
