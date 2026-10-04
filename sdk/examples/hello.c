/* PollikOS SDK hello example. Build:
 *   sdk/tools/pollikcc.ps1 sdk/examples/hello.c -o hello.pol */
#include <stdio.h>
int main(int argc, char **argv) {
    (void)argv;
    printf("Hello from PollikOS C!\n");
    printf("argc = %d\n", argc);
    return 0;
}
