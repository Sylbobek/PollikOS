/* Exercise the real shared button renderer, recording its drawing operations. */
#include "../kernel/ui.c"
extern int printf(const char *, ...);
extern void exit(int);
static u32 fill_color, text_color;
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
void compositor_invalidate_all_surfaces(void) { CHECK(0); }
void wm_invalidate_all(void) { CHECK(0); }
void compositor_invalidate_dock(void) { CHECK(0); }
void request_scene_redraw(void) { CHECK(0); }
void app_host_set_theme(int theme) { (void)theme; CHECK(0); }
void request_partial_redraw(int x,int y,int w,int h) { (void)x;(void)y;(void)w;(void)h;CHECK(0); }
int ui_bridge_screen_width(void) { CHECK(0);return 0; }
int ui_bridge_screen_height(void) { CHECK(0);return 0; }
void app_clipboard_copy(const char *s,int length) { (void)s;(void)length;CHECK(0); }
int app_clipboard_paste(char *out,int capacity) { (void)out;(void)capacity;CHECK(0);return 0; }
void ui_bridge_rect(int x, int y, int w, int h, u32 color) {
    (void)color; CHECK(x >= 20 && y >= 40 && w >= 0 && h >= 0 && x+w <= 120 && y+h <= 66);
}
void ui_bridge_roundrect_stroke(int x,int y,int w,int h,int r,int t,u32 stroke,u32 fill) {
    (void)r; (void)t; (void)stroke; fill_color=fill; ui_bridge_rect(x,y,w,h,fill);
}
void ui_bridge_roundrect(int x,int y,int w,int h,int r,u32 c) { (void)r; ui_bridge_rect(x,y,w,h,c); }
void ui_bridge_rounded(int x,int y,int w,int h,int r,u32 c,int a) { (void)r;(void)a;ui_bridge_rect(x,y,w,h,c); }
u32 ui_bridge_blend(u32 a,u32 b,int t) {
    u32 result=0; for(int s=0;s<=16;s+=8) result|=((((a>>s)&255)*(256-t)+((b>>s)&255)*t)>>8)<<s; return result;
}
void ui_bridge_text(int x,int y,const char *s,u32 c,int scale) { (void)s;(void)scale; CHECK(x>=20&&y>=40&&x<120&&y<66);text_color=c; }
int ui_bridge_text_width(const char *s,int scale) { int n=0;while(*s++)n+=6;return n*scale; }
int main(void) {
    for(int theme=0;theme<2;++theme) {
        g_current_mode=theme?THEME_DARK:THEME_LIGHT;
        for(int kind=0;kind<3;++kind) {
            int def=kind==1,danger=kind==2;
            ui_draw_button(20,40,100,26,"OK",ICON_NONE,UI_BTN_NORMAL,def,danger);
            u32 normal=fill_color;
            ui_draw_button(20,40,100,26,"OK",ICON_NONE,UI_BTN_PRESSED,def,danger);
            CHECK(fill_color!=normal);
            ui_draw_button(20,40,100,26,"OK",ICON_NONE,UI_BTN_DISABLED,def,danger);
            CHECK(fill_color==ui_theme()->surface);
            CHECK(text_color==ui_theme()->text_muted);
            ui_draw_button(20,40,100,26,"OK",ICON_NONE,UI_BTN_HOVER,def,danger);
            CHECK(fill_color!=normal);
        }
    }
    printf("PASS button states: light/dark x normal/default/danger; pressed, disabled, hover; drawing bounds (%d checks)\n",checks);
    return 0;
}
