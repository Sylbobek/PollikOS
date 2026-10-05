#include "../kernel/ui_animation.c"
extern int printf(const char *, ...);
extern void exit(int);
static u32 clock_ms;
static Window test_window;
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
u32 wm_time_ms(void) { return clock_ms; }
Window *wm_get_window(int id) { (void)id;return &test_window; }
void wm_invalidate_rect(int x,int y,int w,int h) { (void)x;(void)y;CHECK(w>0&&h>0); }
void wm_minimize(int id) { (void)id;test_window.minimized=1; }
static void same_visual(WindowAnim before) {
    const WindowAnim *after=ui_anim_get(2);
    CHECK(after&&after->cur_x==before.cur_x&&after->cur_y==before.cur_y);
    CHECK(after->cur_w==before.cur_w&&after->cur_h==before.cur_h);
    CHECK(after->cur_alpha==before.cur_alpha);
}
int main(void) {
    test_window.x=100;test_window.y=80;test_window.width=600;test_window.height=400;
    test_window.open=test_window.visible=1;
    ui_anim_init();ui_anim_start_open(2,100,80,600,400);
    WindowAnim before=*ui_anim_get(2);
    ui_anim_update(clock_ms);same_visual(before);
    for(int i=0;i<100;++i) {
        clock_ms+=40;ui_anim_update(clock_ms);before=*ui_anim_get(2);
        ui_anim_start_close(2);same_visual(before);
        before=*ui_anim_get(2);ui_anim_update(clock_ms);same_visual(before);
        clock_ms+=30;ui_anim_update(clock_ms);before=*ui_anim_get(2);
        ui_anim_start_open(2,100,80,600,400);same_visual(before);
        before=*ui_anim_get(2);ui_anim_update(clock_ms);same_visual(before);
    }
    clock_ms+=400;ui_anim_update(clock_ms);CHECK(!ui_anim_get(2));CHECK(test_window.open);
    ui_anim_start_minimize(2,400,700);clock_ms+=60;ui_anim_update(clock_ms);
    before=*ui_anim_get(2);ui_anim_start_restore(2,400,700,100,80,600,400);same_visual(before);
    clock_ms+=400;ui_anim_update(clock_ms);CHECK(!test_window.minimized);
    ui_anim_start_close(2);before=*ui_anim_get(2);clock_ms+=20;
    ui_anim_start_close(2);same_visual(before);CHECK(ui_anim_get(2)->start_ms==before.start_ms);
    clock_ms+=400;ui_anim_update(clock_ms);CHECK(!test_window.open&&!ui_anim_get(2));
    printf("PASS animation continuity: 100 open/close reversals, alpha at t=0, minimize/restore reversal, duplicate close, final state (%d checks)\n",checks);
    return 0;
}
