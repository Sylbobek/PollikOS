/* C7 libc completion checks: ctype, strings, conversions, sort/search,
 * assert success path, errno strings. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>
static int failures;
#define CHECK(condition, name) \
    do { if (!(condition)) { printf("[C7] FAIL: %s\n", name); ++failures; } } while (0)
static int compare_ints(const void *left, const void *right) {
    int a = *(const int *)left, b = *(const int *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}
static int mode_basic(void) {
    CHECK(isalpha('a') && isalpha('Z') && !isalpha('5'), "isalpha");
    CHECK(isdigit('7') && !isdigit('x') && isxdigit('F') && !isxdigit('g'), "isdigit/isxdigit");
    CHECK(isalnum('q') && isalnum('9') && !isalnum('-'), "isalnum");
    CHECK(isspace(' ') && isspace('\n') && isspace('\t') && !isspace('_'), "isspace");
    CHECK(isupper('A') && islower('z') && toupper('m') == 'M' && tolower('Q') == 'q', "case helpers");
    CHECK(isblank(' ') && isblank('\t') && !isblank('\n'), "isblank");
    CHECK(ispunct(';') && ispunct('+') && !ispunct('a'), "ispunct");
    CHECK(isprint('~') && !isprint(0x1f) && iscntrl(0x1f), "print/control");
    CHECK(toupper((unsigned char)200) == 200 && isalpha((unsigned char)200) == 0, "unsigned-char safe");
    const char *text = "compiler/runtime support";
    CHECK(strstr(text, "runtime") == text+9 && strstr(text, "missing") == NULL, "strstr");
    CHECK(strspn("abc123", "abc") == 3 && strcspn("abc123", "123") == 3, "strspn/strcspn");
    CHECK(strpbrk(text, "/ ") == text+8, "strpbrk");
    char copy[64];
    strcpy(copy, "a");
    strcat(copy, "b");
    strncat(copy, "cdef", 2);
    CHECK(strcmp(copy, "abcd") == 0, "strcat/strncat");
    char *duplicate = strdup(text);
    CHECK(duplicate && strcmp(duplicate, text) == 0, "strdup");
    free(duplicate);
    duplicate = strndup(text, 8);
    CHECK(duplicate && strcmp(duplicate, "compiler") == 0, "strndup");
    free(duplicate);
    CHECK(atoi("  -42") == -42 && atol("123456789") == 123456789L, "atoi/atol");
    CHECK(strtol("0x1f", NULL, 0) == 31 && strtol("077", NULL, 0) == 63, "strtol base 0");
    CHECK(strtoll("-9223372036854775808", NULL, 10) == (-9223372036854775807LL-1), "strtoll min");
    CHECK(strtoull("0xffffffffffffffff", NULL, 16) == 18446744073709551615ULL, "strtoull max");
    char *end = NULL;
    errno = 0;
    CHECK(strtoll("99999999999999999999999", &end, 10) == 9223372036854775807LL &&
          errno == ERANGE && *end == 0, "strtoll overflow");
    errno = 0;
    CHECK(strtol("junk", &end, 10) == 0 && end && *end == 'j', "strtol no digits");
    int values[16] = {9, 3, 7, 1, 8, 2, 6, 4, 5, 0, 15, 11, 13, 10, 14, 12};
    qsort(values, 16, sizeof(int), compare_ints);
    int sorted = 1;
    for (int index = 0; index < 16; ++index) if (values[index] != index) sorted = 0;
    CHECK(sorted, "qsort");
    int key = 13;
    CHECK(bsearch(&key, values, 16, sizeof(int), compare_ints) == values+13, "bsearch");
    key = 99;
    CHECK(bsearch(&key, values, 16, sizeof(int), compare_ints) == NULL, "bsearch missing");
    char formatted[64];
    CHECK(snprintf(formatted, sizeof(formatted), "%s=%d/%lx", "size", 42, 0x2ful) == 10 &&
          strcmp(formatted, "size=42/2f") == 0, "snprintf still works");
    CHECK(strcmp(strerror(ENOENT), "no such file or directory") == 0, "strerror");
    CHECK(strcmp(strerror(12345), "unknown error") == 0, "strerror fallback");
    assert(1 && "assert success path");
    return failures ? 1 : 42;
}
int main(int argc, char **argv) {
    (void)argv;
    if (argc > 1 && strcmp(argv[1], "basic") != 0) return 1;
    return mode_basic();
}
