#include "../../include/pollikos.h"

int main(void) {
    volatile unsigned int *bad = (volatile unsigned int *)0x0;
    *bad = 0x1234;
    return 0;
}
