/* Essential freestanding string routines; only what current libc and tests
 * need. Bounded variants exist for internal use. */
#include <string.h>
#include <stdlib.h>
size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length]) ++length;
    return length;
}
size_t strnlen(const char *text, size_t maximum) {
    size_t length = 0;
    while (length < maximum && text[length]) ++length;
    return length;
}
int strcmp(const char *left, const char *right) {
    while (*left && *left == *right) { ++left; ++right; }
    return (unsigned char)*left - (unsigned char)*right;
}
int strncmp(const char *left, const char *right, size_t count) {
    while (count && *left && *left == *right) { ++left; ++right; --count; }
    return count ? (unsigned char)*left - (unsigned char)*right : 0;
}
char *strcpy(char *destination, const char *source) {
    char *result = destination;
    while ((*destination++ = *source++) != 0) {}
    return result;
}
char *strncpy(char *destination, const char *source, size_t count) {
    char *result = destination;
    while (count && *source) { *destination++ = *source++; --count; }
    while (count--) *destination++ = 0;
    return result;
}
char *strchr(const char *text, int character) {
    char target = (char)character;
    for (;;) {
        if (*text == target) return (char *)text;
        if (!*text) return NULL;
        ++text;
    }
}
char *strrchr(const char *text, int character) {
    const char *found = NULL;
    char target = (char)character;
    for (;;) {
        if (*text == target) found = text;
        if (!*text) return (char *)found;
        ++text;
    }
}
char *strcat(char *destination, const char *source) {
    char *result = destination;
    while (*destination) ++destination;
    while ((*destination++ = *source++) != 0) {}
    return result;
}
char *strncat(char *destination, const char *source, size_t count) {
    char *result = destination;
    while (*destination) ++destination;
    while (count && *source) { *destination++ = *source++; --count; }
    *destination = 0;
    return result;
}
char *strstr(const char *text, const char *needle) {
    if (!*needle) return (char *)text;
    for (; *text; ++text) {
        size_t index = 0;
        while (needle[index] && text[index] == needle[index]) ++index;
        if (!needle[index]) return (char *)text;
    }
    return NULL;
}
char *strpbrk(const char *text, const char *accept) {
    for (; *text; ++text)
        for (const char *candidate = accept; *candidate; ++candidate)
            if (*text == *candidate) return (char *)text;
    return NULL;
}
size_t strspn(const char *text, const char *accept) {
    size_t length = 0;
    while (text[length]) {
        const char *candidate = accept;
        while (*candidate && *candidate != text[length]) ++candidate;
        if (!*candidate) break;
        ++length;
    }
    return length;
}
size_t strcspn(const char *text, const char *reject) {
    size_t length = 0;
    while (text[length]) {
        const char *candidate = reject;
        while (*candidate && *candidate != text[length]) ++candidate;
        if (*candidate) break;
        ++length;
    }
    return length;
}
char *strndup(const char *text, size_t maximum) {
    size_t length = strnlen(text, maximum);
    char *copy = malloc(length+1);
    if (!copy) return NULL;
    memcpy(copy, text, length);
    copy[length] = 0;
    return copy;
}
char *strdup(const char *text) {
    size_t length = strlen(text);
    char *copy = malloc(length+1);
    if (!copy) return NULL;
    memcpy(copy, text, length+1);
    return copy;
}
