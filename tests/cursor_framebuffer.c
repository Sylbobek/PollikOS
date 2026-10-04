/* Real save-under compositor, with the existing native framebuffer harness. */
#define main held_drag_suite_main
#include "held_drag.c"
#undef main

static int sizes_and_background(void) {
    static const int sizes[] = {100,125,150,200,150,100};
    static const int points[][2] = {{0,0},{W-1,0},{0,H-1},{W-1,H-1},{500,350},{400,H-60}};
    for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        memcpy(before, scene, sizeof scene);
        compositor_set_cursor_size(sizes[s]);
        REQUIRE(compositor_cursor_size() == sizes[s]);
        REQUIRE(equal_pixels(scene, before, "size switch modified scene"));
        REQUIRE(compare_full());
        for (int kind = 0; kind < 10; kind++) {
            cursor_kind = kind;
            for (unsigned p = 0; p < sizeof points / sizeof points[0]; p++) {
                px = points[p][0]; py = points[p][1];
                compositor_draw_cursor(1);
                REQUIRE(compare_full());
            }
        }
        /* Stationary cursor while a cached window moves beneath it. */
        cursor_kind = CURSOR_DEFAULT; px = 380; py = 300;
        compositor_draw_cursor(1);
        for (int x = 230; x < 530; x += 37) {
            g_windows[1].x = x;
            compositor_paint(2);
            REQUIRE(compare_full());
        }
    }
    compositor_set_cursor_size(137);
    REQUIRE(compositor_cursor_size() == 100);
    printf("PASS cursor framebuffer: all 10 kinds, four scales, four corners, dock, stationary cursor/window motion, scale erasure; pixel-identical full-repaint oracle\n");
    return 1;
}
int main(void) {
    setup();
    return !(cursor_presentations() && sizes_and_background());
}
