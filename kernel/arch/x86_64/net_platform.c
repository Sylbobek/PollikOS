#include "net_platform.h"

void serial(const char *text) { memory_log(text); }

size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length]) ++length;
    return length;
}

int memcmp(const void *left, const void *right, size_t size) {
    const unsigned char *a = left, *b = right;
    for (size_t i = 0; i < size; ++i) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

void number(char *out, u32 value) {
    char reverse[10];
    unsigned count = 0;
    do {
        reverse[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && count < sizeof(reverse));
    unsigned at = 0;
    while (count) out[at++] = reverse[--count];
    out[at] = 0;
}
