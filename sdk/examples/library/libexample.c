#include "libexample.h"
int example_add(int left, int right) { return left + right; }
unsigned example_hash(const char *text) {
    unsigned hash = 2166136261u;
    while (*text) {
        hash ^= (unsigned char)*text++;
        hash *= 16777619u;
    }
    return hash;
}
