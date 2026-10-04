/* Controlled invalid pointer dereference; the kernel must contain the fault
 * and report it as a normal page fault. */
#include <stdio.h>
#include <stdint.h>
int main(void) {
    volatile uintptr_t address = 0; /* classic NULL, not optimized away */
    printf("[C4] null fault: dereferencing\n");
    *(volatile uint64_t *)address = 1;
    return 1;
}
