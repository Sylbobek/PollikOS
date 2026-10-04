/* Userspace assertion failure: report and terminate this process only. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
void __pollikos_assert_fail(const char *expression, const char *file, int line) {
    fprintf(stderr, "assertion failed: %s (%s:%d)\n", expression, file, line);
    abort();
}
