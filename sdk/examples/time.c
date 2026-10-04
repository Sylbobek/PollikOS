/* Monotonic time and blocking sleep example. No calendar time exists. */
#include <stdio.h>
#include <pollikos/time.h>
int main(void) {
    long ticks = pollikos_clock_ticks();
    long before = pollikos_monotonic_ms();
    if (ticks < 0 || before < 0) return 1;
    if (sleep_ms(20) != 0) return 2;
    long after = pollikos_monotonic_ms();
    if (after < before + 20) return 3;
    printf("[sdk] time: ticks=%ld before=%ld after=%ld\n", ticks, before, after);
    return 0;
}
