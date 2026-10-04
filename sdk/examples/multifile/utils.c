#include "utils.h"
unsigned long example_checksum(const char *text) {
    unsigned long sum = 0;
    while (*text) sum += (unsigned char)*text++;
    return sum;
}
int example_clamp(int value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}
