#include "../../include/pollikos.h"

int main(void) {
    /* Touch the guard page at 0xBFFFB800 below the user stack (0xBFFFC000) */
    volatile unsigned int *bad = (volatile unsigned int *)0xBFFFB800;
    *bad = 0x1234;
    return 0;
}
