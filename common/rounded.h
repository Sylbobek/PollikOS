#ifndef POLLIK_ROUNDED_H
#define POLLIK_ROUNDED_H
/* Platform-independent 4x4 corner coverage, scaled to 0..64. */
static inline int pollik_corner_coverage(int radius, int x, int y) {
    if (x < 0 || y < 0) return 0;
    if (radius <= 0 || x >= radius || y >= radius) return 64;
    int coverage = 0;
    for (int sy = 1; sy < 8; sy += 2) for (int sx = 1; sx < 8; sx += 2) {
        int dx = 8*(radius-x)-sx, dy = 8*(radius-y)-sy;
        if (dx*dx + dy*dy <= 64*radius*radius) coverage += 4;
    }
    return coverage;
}
#endif
