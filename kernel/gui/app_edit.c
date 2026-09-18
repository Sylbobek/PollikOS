#include "app_internal.h"

static char clipboard[256];
static int clipboard_len;

int app_edit_key(char *buf, int *n, int max, u8 code, char ch, int control) {
    if (control && code == 46) {
        int copylen = *n;
        if (copylen > 255) copylen = 255;
        for (int i = 0; i < copylen; i++) clipboard[i] = buf[i];
        clipboard[copylen] = 0;
        clipboard_len = copylen;
        return 0;
    }
    if (control && code == 47) {
        for (int i = 0; i < clipboard_len && *n < max; i++) {
            buf[(*n)++] = clipboard[i];
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
