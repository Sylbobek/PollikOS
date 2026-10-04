/* Static library SDK example. Build the archive and link it:
 *   sdk/tools/pollikcc.ps1 -c sdk/examples/library/libexample.c -o libexample.o
 *   llvm-ar rcs libexample.a libexample.o
 *   sdk/tools/pollikcc.ps1 sdk/examples/library/main.c -L. -lexample -o library.elf */
#include <stdio.h>
#include "libexample.h"
int main(void) {
    if (example_add(40, 2) != 42) return 1;
    if (example_hash("pollikos") == 0) return 2;
    printf("[sdk] library: add=%d hash=%08x\n", example_add(40, 2), example_hash("pollikos"));
    return 0;
}
