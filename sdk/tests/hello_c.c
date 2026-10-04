/* First freestanding PollikOS C application: crt0 + libpollikc, normal main. */
#include <stdio.h>
int main(int argc, char **argv) {
    (void)argv;
    printf("Hello from PollikOS C\n");
    printf("argc=%d\n", argc);
    return 42;
}
