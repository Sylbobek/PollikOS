/* Startup ABI v1 verification from ordinary C: argc, argv and environ. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    printf("argc=%d\n", argc);
    for (int index = 0; index < argc; ++index) printf("argv[%d]=%s\n", index, argv[index]);
    for (char **entry = environ; entry && *entry; ++entry) printf("env=%s\n", *entry);
    if (argc != 3) return 1;
    if (strcmp(argv[0], "argv_c") != 0) return 2;
    if (strcmp(argv[1], "first") != 0 || strcmp(argv[2], "second") != 0) return 3;
    char *test = getenv("TEST");
    if (!test || strcmp(test, "pollikos") != 0) return 4;
    if (getenv("POLLIKOS_MISSING")) return 5;
    printf("[C3] argv/envp OK\n");
    return 42;
}
