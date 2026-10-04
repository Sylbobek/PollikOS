/* Forces a >128 KiB static ELF to exercise the raised executable size cap.
 * volatile keeps the initialized array in the file image at -O2. */
#include <stdio.h>
static volatile unsigned char blob[300*1024] = {1};
int main(void) {
    unsigned long ones = 0;
    for (unsigned long index = 0; index < sizeof(blob); index += 4096)
        if (blob[index]) ++ones;
    if (blob[0] != 1 || blob[1] != 0 || ones != 1) return 1;
    printf("[c7] big ELF loaded: %lu KiB blob\n", (unsigned long)(sizeof(blob)/1024));
    return 42;
}
