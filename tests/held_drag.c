/* Native, deterministic held-drag compositor regression. No OS image, input
 * injection, hardware I/O, application behavior, or release-triggered repair.
 * Include the real compositor to inspect its cursor-free scene/private caches;
 * graphics.c is linked unchanged. Only external shell/client services are mocks.
 */
#include "../kernel/shell_internal.h"
#include "fixtures/ui_data.h" /* Frozen sprite oracle; never linked into kernel. */
extern int printf(const char *, ...);

/* PMM/VBE addresses are intentionally 32-bit in the kernel. These unreachable
 * init functions are parsed on a 64-bit host; never call them in this test. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wint-to-pointer-cast"
#pragma clang diagnostic ignored "-Wpointer-to-int-cast"
#define framebuffer_present native_framebuffer_present
#define framebuffer_present_cursor_pair native_framebuffer_present_cursor_pair
#include "../kernel/framebuffer.c"
#undef framebuffer_present
#undef framebuffer_present_cursor_pair
#include "../kernel/compositor.c"
#pragma clang diagnostic pop

#define W 1024
#define H 768
#define N (W * H)
static u32 scene[N], background[N], reference[N], reference_lfb[N];
static u32 hardware[N], before[N], cache_backup[(APP_COUNT * 68 + 144) * 145];
static u32 native_dock_background[DOCK_CACHE_WIDTH * 145];
static u32 native_auth_scratch[2][N];
static int native_auth_allocate,native_auth_allocations;
_Static_assert(sizeof cache_backup == sizeof native_dock_background, "count-derived dock cache geometry");
static u32 clients[NUM_APPS][420 * 320];
static int px = 380, py = 220, cursor_kind, calls, paints, failures, last_full;
static u32 last_pixels;
static int reference_pass;
static int resize_calls, resize_order_errors, notified_w[NUM_APPS], notified_h[NUM_APPS];
ShellState shell;
Window g_windows[NUM_APPS];
WindowSurface g_surfaces[NUM_APPS];
int g_z_order[NUM_APPS], g_hovered_window = -1;
UiMenu g_active_menu;
UiDialog g_active_dialog;
GuiPerfStats g_perf_stats;

#define REQUIRE(e) do { if (!(e)) { printf("FAIL line %d: %s\n", __LINE__, #e); return 0; } } while (0)
void serial(const char *s) { (void)s; }
void number(char *out, u32 n) { (void)n; out[0] = 0; }
uintptr_t pmm_alloc_pages(u32 n) { (void)n; return 0; }
void pmm_free_pages(uintptr_t p, u32 n) { (void)p; (void)n; }
volatile u32 ticks;
void *kmalloc(u32 size){return native_auth_allocate&&size<=sizeof(native_auth_scratch[0])&&native_auth_allocations<2?native_auth_scratch[native_auth_allocations++]:0;}
void kfree(void *memory){(void)memory;}
int hal_cpu_has_cpuid(void){return 0;}
void hal_cpuid(unsigned leaf,unsigned subleaf,unsigned *a,unsigned *b,unsigned *c,unsigned *d){(void)leaf;(void)subleaf;*a=*b=*c=*d=0;}
void hal_port_write8(unsigned short port,unsigned char value){(void)port;(void)value;}
u64 hal_read_tsc_serialized(void){return 0;}
void hal_cpu_relax(void){}
void klog_dec(const char *a, const char *b, u32 c) { (void)a; (void)b; (void)c; }
Window *wm_get_window(int id) { return &g_windows[id]; }
int wm_active_app(void) { return 1; }
u32 wm_time_ms(void) { return 1000; }
u64 wm_time_us(void) { return 1000000; }
void wm_perf_frame_begin(void) {}
void wm_perf_set_frame_kind(int kind) { (void)kind; }
void wm_perf_record_client_paint(void) { paints++; }
int wm_perf_overlay_is_enabled(void) { return 0; }
void wm_perf_summary(char *out, int capacity) { if (capacity > 0) out[0] = 0; }
void wm_perf_frame_end(u32 a, u32 b, u32 c, int full, u32 count) {
    (void)a; (void)b; (void)c; last_full = full; last_pixels = count;
}
void wm_check_canaries(void) {}
void hal_port_write16(unsigned short port, unsigned short value) { (void)port; (void)value; }
unsigned short hal_port_read16(unsigned short port) { (void)port; return 0; }
unsigned char hal_port_read8(unsigned short port) { (void)port; return 0; }
void hal_cpu_halt_forever(void) { __builtin_trap(); }
int auth_is_active(void) { return 0; }
void auth_render(int width, int height) { (void)width; (void)height; }
int dock_get_visible_apps(int *out_apps, int max_apps) {
    int count = APP_COUNT < max_apps ? APP_COUNT : max_apps;
    for (int i = 0; i < count; i++) out_apps[i] = i;
    return count;
}
void desktop_items_draw(void) {}
ThemeColors *ui_theme(void) {
    static ThemeColors colors = {0x202020,0x303030,0x383838,0x404040,0x505050,
        0x484848,0xffffff,0xcccccc,0xaaaaaa,0x8060ff,0x9070ff,0x8060ff,
        0xffffff,0xff0000,0xcc0000,0x00ff00,0xffff00};
    return &colors;
}
int dock_is_visible(void) { return 1; }
int ui_is_dark(void) { return 0; }
void ui_anim_cancel(int id) { (void)id; }
const WindowAnim *ui_anim_get(int id) { (void)id; return 0; }
int ui_anim_dock_is_launching(int id) { (void)id; return 0; }
void ui_anim_update(u32 now) { (void)now; }
int ui_anim_has_active(void) { return 0; }
void wm_get_snap_bounds(SnapTarget target, Rect *r) { (void)target; *r = (Rect){0, 0, 512, 600}; }
void request_scene_redraw(void) { shell.scene_dirty = 1; }
int input_pointer_x(void) { return px; }
int input_pointer_y(void) { return py; }
int input_cursor_kind(void) { return cursor_kind; }
const GuiApp *gui_app_get(int id) {
    static const GuiApp app = {.name = "Test", .body_active = 0xd9e2eb, .body_inactive = 0xc1cad3};
    (void)id; return &app;
}
void gui_app_resized(int id, int w, int h) {
    resize_calls++;
    notified_w[id] = w; notified_h[id] = h;
}
void gui_app_render(int id, int w, int h, int active) {
    (void)active;
    if (notified_w[id] != w || notified_h[id] != h) resize_order_errors++;
    rect(16, 45, w - 32, h - 65, 0x8090a0);
    text(20, 50, "Cache rendered in local coordinates", 0xffffff, 1);
}
void desktop_paint_wallpaper(u32 *dst) {
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
        dst[y * W + x] = 0x8a9bac + (u32)((x + y) % 31);
}
void desktop_paint_wallpaper_mode(u32 *dst, int dark) { (void)dark; desktop_paint_wallpaper(dst); }
void desktop_paint_wallpaper_fallback(u32 *dst, int dark) { (void)dark; desktop_paint_wallpaper(dst); }
void desktop_draw_bar(void) { text(20, 7, "Scene bar", 0x202020, 1); }
void dock_draw_pill(void) {
    int width = APP_COUNT * 68 + 48;
    rounded((W - width) / 2, H - 96, width, 84, 29, 0xffffff, 205);
}
void dock_draw_content(void) {
    sprite(360, H - 88, 56, 56, cursors_index[2], cursors_alpha[2], cursors_palette[2], 32, 36);
}
void ui_draw_dialog(void) { rounded(400, 200, 340, 280, 12, 0xeeeeee, 190); }
void control_center_draw(void){
    ui_bridge_glass(UI_GLASS_CONTROL,500,60,350,450,22,0x20263a,160);
    text(520,80,"Glass overlay",0xffffff,1);
}
int control_center_active(void){return 1;}
void ui_draw_menu(UiMenu *m) { (void)m; rounded(100, 300, 250, 180, 8, 0x303040, 200); }
void ui_draw_notifications(u32 now) {
    (void)now;
    /* Intentionally intersects only some damage rectangles. */
    rounded(720, 500, 270, 180, 12, 0x443355, 160);
    text(734, 514, "Notification", 0xffffff, 1);
}
void desktop_draw_overlays(void) {
    /* Exercises scene-global overlays even while not doing a full repaint. */
    rounded(900, 80, 90, 260, 8, 0x556677, 80);
}
void framebuffer_present(const u32 *src, int x, int y, int w, int h) {
    calls++;
    address = (volatile u8 *)(reference_pass ? reference_lfb : hardware);
    screen_w = W; screen_h = H; stride = W * 4; bytes = 4;
    native_framebuffer_present(src, x, y, w, h);
}
void framebuffer_present_cursor_pair(const u32 *old_pixels, int old_x, int old_y,
                                     int old_w, int old_h, const u32 *new_pixels,
                                     int new_x, int new_y, int new_w, int new_h) {
    if (!calls) calls++;
    address = (volatile u8 *)(reference_pass ? reference_lfb : hardware);
    screen_w = W; screen_h = H; stride = W * 4; bytes = 4;
    native_framebuffer_present_cursor_pair(old_pixels, old_x, old_y, old_w, old_h,
                                           new_pixels, new_x, new_y, new_w, new_h);
}
static int equal_pixels(const u32 *a, const u32 *b, const char *label) {
    for (int i = 0; i < N; i++) if (a[i] != b[i]) {
        printf("FAIL %s at %d,%d: %08x != %08x\n", label, i % W, i / W, a[i], b[i]);
        return 0;
    }
    return 1;
}
static int compare_full(void) {
    /* Render an oracle into separate buffers, then restore the incremental
     * cache. Never replace the accumulated held-drag scene with the oracle. */
    memcpy(cache_backup, dock_background, sizeof(cache_backup));
    pixels = reference;
    set_draw_target(reference, W, H, W);
    reference_pass = 1;
    compositor_paint(1);
    reference_pass = 0;
    pixels = scene;
    set_draw_target(scene, W, H, W);
    memcpy(dock_background, cache_backup, sizeof(cache_backup));
    REQUIRE(equal_pixels(scene, reference, "cursor-free scene vs full repaint"));
    REQUIRE(equal_pixels(hardware, reference_lfb, "submitted LFB vs full repaint"));
    return 1;
}
static void setup(void) {
    dock_background=native_dock_background;
    shell = (ShellState){.width = W, .height = H, .drag_app = 1, .drag = 1,
                         .drag_moved = 1, .resizing = -1, .hover = -1};
    pixels = scene; wallpaper = background;
    graphics_init(W, H);
    set_draw_target(scene, W, H, W);
    for (int id = 0; id < NUM_APPS; id++) {
        g_z_order[id] = id;
        g_surfaces[id].pixels = clients[id];
    }
    g_windows[0] = (Window){.x = 290, .y = 150, .width = 360, .height = 250, .open = 1};
    g_windows[1] = (Window){.x = 220, .y = 250, .width = 300, .height = 220, .open = 1};
    /* Window 2 contributes a shadow where its client lies outside damage. */
    g_windows[2] = (Window){.x = 550, .y = 140, .width = 200, .height = 100, .open = 1};
    compositor_paint(1);
}
static int held_drag(void) {
    static const int positions[][2] = {
        {223,245},{228,239},{250,250},{255,251},{260,249},{263,240},
        {280,244},{300,246},{330,242},{350,250},{350,250},{350,250},
        {520,420},{550,445},{580,450},{585,450},{590,447},{600,450},
        {720,560},{800,620},{850,640},{880,650},{890,650},{870,630},
        {-30,70},{-25,65},{20,32},{40,35},{220,250}
    };
    for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); i++) {
        g_windows[1].x = positions[i][0]; g_windows[1].y = positions[i][1];
        px = g_windows[1].x + 100; py = g_windows[1].y + 17;
        int old_paints = paints, old_resizes = resize_calls;
        calls = 0;
        compositor_paint(2);
        REQUIRE(shell.drag && shell.drag_app == 1); /* Still held: no release. */
        REQUIRE(calls == 1 && !last_full);
        REQUIRE(last_pixels < N);
        REQUIRE(paints == old_paints); /* Translation must reuse clients. */
        REQUIRE(resize_calls == old_resizes && !resize_order_errors);
        REQUIRE(compare_full());
    }
    printf("PASS held-drag frame pixels identical to full-repaint oracle (29 positions)\n");
    /* Check the pre-icon dock cache after partial updates, without repairing it. */
    calls = 0;
    compositor_paint(0);
    REQUIRE(calls == 1);
    REQUIRE(compare_full());
    /* Resize repaints in target-local coordinates despite narrow scene clip. */
    int old_resizes = resize_calls;
    g_windows[1].width = 340; g_windows[1].height = 240;
    compositor_paint(2);
    REQUIRE(resize_calls == old_resizes + 1 && !resize_order_errors);
    REQUIRE(compare_full());
    /* Inactive neutral controls match the pre-followup2 chrome artwork. */
    REQUIRE((clients[0][17 * 360 + 18]&0xffffffu) == 0xb8adb5);
    REQUIRE((clients[0][17 * 360 + 36]&0xffffffu) == 0xb8b2ad);
    REQUIRE((clients[0][17 * 360 + 54]&0xffffffu) == 0xadb8b2);
    printf("PASS accumulated held-drag frames, shadow-only cull, dock cache, resize, dim controls\n");
    return 1;
}
static int cursor_presentations(void) {
    static const int points[][2] = {{500,350},{501,351},{0,0},{W-1,0},{0,H-1},{W-1,H-1},{200,200}};
    for (int kind = 0; kind <= CURSOR_RESIZE_NESW; kind++) {
        for (unsigned i = 0; i < sizeof(points) / sizeof(points[0]); i++) {
            memcpy(before, scene, sizeof(scene));
            cursor_kind = kind; px = points[i][0]; py = points[i][1];
            calls = 0;
            compositor_draw_cursor(0);
            REQUIRE(calls == 1); /* No erase-only intermediate submission. */
            REQUIRE(equal_pixels(scene, before, "scene changed by cursor"));
            REQUIRE(compare_full());
            calls = 0;
            compositor_draw_cursor(0);
            REQUIRE(calls == 0);
        }
    }
    printf("PASS cursor union presents, kind changes, corners, cursor-free scene\n");
    return 1;
}
static void primitive(int kind) {
    switch (kind) {
    case 0: rect(-5, -5, 80, 80, 0xfefefe); break;
    case 1: rounded(-5, -5, 70, 65, 16, 0xffffff, 95); break;
    case 2: rounded(-5, -5, 70, 65, 0, 0xffffff, 95); break;
    case 3: roundrect(-5, -5, 70, 65, 12, 0xffffff); break;
    case 4: roundrect_slice(-5, -5, 70, 65, 6, 0xffffff, 3, 61); break;
    case 5: text(2, 4, "Rounded\nGlyphs", 0xffffff, 1); break;
    case 6: sys_draw_letter_clipped(9, 3, 'W', 0xffffff, 3, 0, 0, 60, 60); break;
    case 7: sprite(3, 1, 32, 36, cursors_index[2], cursors_alpha[2], cursors_palette[2], 32, 36); break;
    case 8: sprite(3, 1, 56, 60, cursors_index[2], cursors_alpha[2], cursors_palette[2], 32, 36); break;
    }
}
static int scissor_primitives(void) {
    u32 a[71 * 64], b[71 * 64];
    static const GraphicsClip clips[] = {{11,9,39,31},{0,0,3,3},{55,50,64,64},{20,20,20,40},{-5,-8,12,15}};
    for (int kind = 0; kind < 9; kind++) for (unsigned c = 0; c < sizeof(clips)/sizeof(clips[0]); c++) {
        for (int i = 0; i < 71 * 64; i++) a[i] = b[i] = 0x345678;
        set_draw_target(a, 64, 64, 71); primitive(kind);
        set_draw_target(b, 64, 64, 71); graphics_set_clip(clips[c]); primitive(kind);
        for (int y = 0; y < 64; y++) for (int x = 0; x < 71; x++) {
            int inside = x < 64 && x >= clips[c].x1 && x < clips[c].x2 && y >= clips[c].y1 && y < clips[c].y2;
            REQUIRE(b[y*71+x] == (inside ? a[y*71+x] : 0x345678));
        }
    }
    set_draw_target(scene, W, H, W);
    printf("PASS all primitive scissors and padded target stride\n");
    return 1;
}
static int framebuffer_rows(void) {
    u8 out[128];
    u32 src[5 * 4];
    for (int i = 0; i < 20; i++) src[i] = 0x102030u + (u32)i * 0x030507u;
    for (int depth = 3; depth <= 4; depth++) {
        for (int i = 0; i < 128; i++) out[i] = 0xa5;
        address = out + 8; screen_w = 5; screen_h = 4; bytes = depth; stride = 5 * depth + 7;
        native_framebuffer_present(src, 1, 1, 3, 2);
        for (int i = 0; i < 128; i++) {
            int offset = i - 8, row = offset >= 0 ? offset / stride : -1;
            int col = offset >= 0 ? offset % stride : -1;
            u8 expected = 0xa5;
            if (row >= 1 && row < 3 && col >= depth && col < 4 * depth)
                expected = (u8)(src[row * 5 + col / depth] >> (8 * (col % depth)));
            REQUIRE(out[i] == expected);
        }
    }
    address = (volatile u8 *)hardware; screen_w = W; screen_h = H; bytes = 4; stride = W * 4;
    printf("PASS 24/32-bpp framebuffer channel order, padded pitch, partial rows\n");
    return 1;
}
static u64 test_pixel_hash(const u32 *buffer) {
    u64 hash = 1469598103934665603ull;
    for (int i = 0; i < N; i++) {
        hash ^= buffer[i];
        hash *= 1099511628211ull;
    }
    return hash;
}
#include "corners.h"
int main(void) {
    static u32 blur_scratch[(DOCK_GLASS_BLUR_ROWS+1+3)*DOCK_CACHE_WIDTH];
    static u32 welcome_backdrop[N];
    static u32 panel_backdrop[N];
    dock_glass_blur_rows=(u32 (*)[DOCK_CACHE_WIDTH])blur_scratch;
    dock_glass_next_blur_row=blur_scratch+DOCK_GLASS_BLUR_ROWS*DOCK_CACHE_WIDTH;
    dock_glass_vertical_sum=(int (*)[DOCK_CACHE_WIDTH])(dock_glass_next_blur_row+DOCK_CACHE_WIDTH);
    glass_backdrops[UI_GLASS_WELCOME].pixels=welcome_backdrop;
    glass_backdrops[UI_GLASS_WELCOME].capacity=N;
    glass_backdrops[UI_GLASS_CONTROL].pixels=panel_backdrop;
    glass_backdrops[UI_GLASS_CONTROL].capacity=N;
    for(int percent=20;percent<=100;percent++) {
        framebuffer_set_brightness(percent);
        for(int value=0;value<256;value++) {
            u32 color=0xff000000u|((u32)value<<16)|((u32)(255-value)<<8)|(u32)value;
            u32 actual=brightness_pixel(color);
            for(int shift=0;shift<=16;shift+=8) {
                int expected=(((color>>shift)&255)*(u32)percent+50)/100;
                int difference=(int)((actual>>shift)&255)-expected;
                if(difference < -1 || difference > 1)failures++;
            }
            if((actual>>24)!=255)failures++;
        }
    }
    framebuffer_set_brightness(100);
    if(!failures)printf("PASS brightness: 62208 channel vectors, max channel error 1, alpha preserved\n");
    setup();
    failures += !held_drag();
    failures += !cursor_presentations();
    failures += !scissor_primitives();
    failures += !framebuffer_rows();
    failures += !corner_pixels();
    failures += !corner_masks();
    if (!failures)
        printf("PIXEL_HASH scene=%016llx hardware=%016llx\n",
               (unsigned long long)test_pixel_hash(scene),
               (unsigned long long)test_pixel_hash(hardware));
    return failures != 0;
}
