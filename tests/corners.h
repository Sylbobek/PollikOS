/* Independent 4x4 sample oracle: no production coverage/blend calls. Included
 * by held_drag.c to inspect the real compositor's cache and cursor-free scene. */
static int expected_coverage(int r, int x, int y) {
    if (x >= r || y >= r) return 64;
    int hits = 0;
    for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++) {
        int dx = 8*x + 2*sx + 1 - 8*r;
        int dy = 8*y + 2*sy + 1 - 8*r;
        if (dx*dx + dy*dy <= 64*r*r) hits++;
    }
    return hits * 4;
}
static u32 expected_mix(u32 bg, u32 fg, int alpha) {
    u32 result = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        u32 a = (bg >> shift) & 255, b = (fg >> shift) & 255;
        result |= ((a * (256-alpha) + b * alpha) / 256) << shift;
    }
    return result;
}
static int corner_pixels(void) {
    const u32 bg = 0x19b6e3;
    static const int positions[][2] = {{40,50},{-5,50},{W-75,50},{40,-5},{40,H-75}};
    static const GraphicsClip clips[] = {{0,0,W,H},{43,52,134,144}};
    int partial = 0, opaque_black = 0, transparent = 0;
    Window *w = &g_windows[1];
    /* Rounded, without the normal-window shadow contaminating the AA oracle.
     * Maximized square geometry is tested separately below. */
    w->width = 100; w->height = 100; w->state = WINDOW_STATE_SNAPPED;
    REQUIRE(render_window_to_surface(1, 1, 0));
    /* Real cache, not preblended black or guard colors, in ALL four corners. */
    for (int y = 0; y < 12; y++) for (int x = 0; x < 12; x++) {
        REQUIRE(clients[1][y*100+x] == 0xf2eff6);
        REQUIRE(clients[1][y*100+99-x] == 0xf2eff6);
        REQUIRE(clients[1][(99-y)*100+x] == 0xd9e2eb);
        REQUIRE(clients[1][(99-y)*100+99-x] == 0xd9e2eb);
    }
    /* Include truly black pixels at partial/full coverage in top/bottom rows. */
    for (int y = 0; y < 100; y++) for (int x = 0; x < 100; x++)
        clients[1][y*100+x] = y < 50 ? 0xf2eff6 : 0;
    for (unsigned pos = 0; pos < sizeof(positions)/sizeof(positions[0]); pos++)
    for (unsigned ci = 0; ci < sizeof(clips)/sizeof(clips[0]); ci++) {
        w->x = positions[pos][0]; w->y = positions[pos][1];
        for (int i = 0; i < N; i++) scene[i] = bg;
        set_draw_target(scene, W, H, W); graphics_set_clip(clips[ci]);
        int old_paints = paints, old_resizes = resize_calls;
        compose_window_surface(1, 1);
        REQUIRE(paints == old_paints && resize_calls == old_resizes);
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            int lx = x-w->x, ly = y-w->y;
            u32 expected = bg;
            if (lx >= 0 && lx < 100 && ly >= 0 && ly < 100 &&
                x >= clips[ci].x1 && x < clips[ci].x2 && y >= clips[ci].y1 && y < clips[ci].y2) {
                int cx = lx < 50 ? lx : 99-lx, cy = ly < 50 ? ly : 99-ly;
                int cov = expected_coverage(12, cx, cy);
                u32 fg = ly < 50 ? 0xf2eff6 : 0;
                expected = expected_mix(bg, fg, cov*4);
                partial += cov > 0 && cov < 64;
                transparent += !cov;
                opaque_black += cov == 64 && !fg;
            }
            REQUIRE(scene[y*W+x] == expected);
            REQUIRE((scene[y*W+x] & 0xffffff) != (SURFACE_CANARY & 0xffffff));
        }
    }
    REQUIRE(partial > 0 && transparent > 0 && opaque_black > 0);
    w->state = WINDOW_STATE_MAXIMIZED;
    w->x = 40; w->y = 50;
    for (int i = 0; i < N; i++) scene[i] = bg;
    set_draw_target(scene, W, H, W);
    compose_window_surface(1, 1);
    for (int y = 0; y < 100; y++) for (int x = 0; x < 100; x++)
        REQUIRE(scene[(y+50)*W+x+40] == (y < 50 ? 0xf2eff6 : 0));
    /* Focus/content-only repaints must not notify geometry again. */
    int old_resizes = resize_calls;
    REQUIRE(render_window_to_surface(1, 0, 0));
    compositor_invalidate(1);
    REQUIRE(render_window_to_surface(1, 0, 0));
    REQUIRE(resize_calls == old_resizes && !resize_order_errors);
    printf("PASS independent four-corner RGB/AA, no canary RGB, opaque black, all screen edges/scissors, resize-before-render\n");
    return 1;
}
static int corner_masks(void) {
    u32 target[107*104];
    static const int radii[] = {3,4,6,12,14,16,18,29};
    static const GraphicsClip clips[] = {{0,0,100,104},{8,9,87,95}};
    for (int r = 1; r <= 29; r++) for (int y = 0; y < r; y++) for (int x = 0; x < r; x++)
        REQUIRE(graphics_corner_coverage(r,x,y) == expected_coverage(r,x,y));
    for (unsigned ri = 0; ri < sizeof(radii)/sizeof(radii[0]); ri++)
    for (unsigned ci = 0; ci < sizeof(clips)/sizeof(clips[0]); ci++) {
        int r = radii[ri];
        for (int i = 0; i < 107*104; i++) target[i] = 0xe2b419;
        set_draw_target(target,100,104,107); graphics_set_clip(clips[ci]);
        rounded(3,4,94,96,r,0x181024,95);
        for (int y = 0; y < 104; y++) for (int x = 0; x < 107; x++) {
            u32 expected = 0xe2b419;
            if (x >= 3 && x < 97 && y >= 4 && y < 100 &&
                x >= clips[ci].x1 && x < clips[ci].x2 && y >= clips[ci].y1 && y < clips[ci].y2) {
                int cx = x < 50 ? x-3 : 96-x, cy = y < 52 ? y-4 : 99-y;
                int coverage = expected_coverage(r,cx,cy);
                expected = expected_mix(expected,0x181024,95*coverage/64);
            }
            REQUIRE(target[y*107+x] == expected);
        }
    }
    set_draw_target(scene,W,H,W);
    printf("PASS independent LUT radii 1..29, rounded shadow/dock colors (18/29), clipping and padded stride\n");
    return 1;
}
