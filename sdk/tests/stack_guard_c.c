/* Controlled user-stack overflow. Each frame touches a volatile 512-byte
 * buffer, so the recursion reaches the guard page and faults only this
 * process. The kernel test pairs it with a healthy peer. */
#include <stdio.h>
static volatile unsigned long sink;
static volatile int never_reached;
__attribute__((noinline)) static void descend(unsigned long depth) {
    volatile unsigned char frame[512];
    frame[0] = (unsigned char)depth;
    frame[511] = (unsigned char)(depth >> 7);
    sink += frame[0] + frame[511];
    if (never_reached) return; /* volatile read: not provably infinite recursion */
    descend(depth + 1);
    sink += frame[0]; /* keeps the frame live, preventing tail-call reuse */
}
int main(void) {
    printf("[C4] stack guard: descending\n");
    descend(1);
    return 1;
}
