/* Native geometry regression: REAL five simple apps and editor, mock host and
 * glyph metrics. Includes implementations to inspect private layout state.
 * Not a pixel/font raster or WM propagation test. No OS boot, disk or network. */
#include "../kernel/gui/welcome.c"
#include "../kernel/gui/files.c"
#include "../kernel/gui/notes.c"
#include "../kernel/gui/settings.c"
#include "../kernel/gui/terminal.c"
#include "../kernel/gui/app_edit.c"
extern int printf(const char *, ...);
extern void exit(int);
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL %d: %s (app %d, %dx%d)\n", __LINE__, #x, current, sw, sh); exit(1); } } while (0)
static int checks, current, sw, sh, drawing, paints, saves, opened, theme, animations = 1, pings, stops;
static GuiAppSize test_sizes[APP_COUNT];
static int terminal_visible_columns, terminal_prompt_y;
File files[FS_FILES];
int fs_ready, net_ready;
const char *fs_status = "Saved to PollikFS";
const char *net_status = "CONNECTED - ETHERNET";
volatile u32 ticks;
GuiAppSize gui_app_size(int id) { return test_sizes[id]; }
static void bounds(int x,int y,int w,int h) {
    if (!drawing) return;
    ++paints;
    CHECK(w >= 0 && h >= 0 && x >= 0 && y >= 34 && x + w <= sw && y + h <= sh);
}
int sys_get_glyph_advance(u8 c,int scale) { return (c == 'W' ? 8 : c == 'i' ? 3 : 6) * scale; }
int sys_text_width(const char *s,int scale) {
    int w = 0; while (*s) w += sys_get_glyph_advance(*s++,scale); return w;
}
static int font_h(int scale) { return scale == 1 ? 13 : scale == 2 ? 20 : scale == 3 ? 28 : 36; }
void ui_bridge_rect(int x,int y,int w,int h,u32 c) { (void)c; bounds(x,y,w,h); }
void ui_bridge_roundrect(int x,int y,int w,int h,int r,u32 c) { (void)r; ui_bridge_rect(x,y,w,h,c); }
void app_draw_letter(int x,int y,u8 c,u32 color,int scale) { (void)color; bounds(x,y,sys_get_glyph_advance(c,scale),font_h(scale)); }
void ui_bridge_text(int x,int y,const char *s,u32 c,int scale) {
    (void)c; bounds(x,y,sys_text_width(s,scale),font_h(scale));
}
void app_draw_centered(int x,int y,int w,const char *s,u32 c,int scale) {
    int tw = sys_text_width(s,scale); CHECK(tw <= w); ui_bridge_text(x+(w-tw)/2,y,s,c,scale);
}
void app_draw_mono(int x,int y,const char *s,u32 c) {
    if (drawing && current == APP_TERMINAL && c == 0xf4f3fa) {
        terminal_visible_columns = len(s); terminal_prompt_y = y;
    }
    bounds(x,y,len(s)*12,16);
}
void app_host_invalidate(int id) { (void)id; }
void app_host_open(int id) { opened = id; }
int app_host_theme(void) { return theme; }
void app_host_set_theme(int n) { theme = n; }
int app_host_animations(void) { return animations; }
void app_host_set_animations(int n) { animations = n; }
void app_host_stop_minimize(void) { ++stops; }
void app_host_power(int reboot) { (void)reboot; CHECK(0); }
void app_host_perf_summary(char *s,int cap) { (void)cap; copy(s,"perf"); }
void app_host_service_loading(void) { CHECK(0); }
void serial(const char *s) { (void)s; }
void number(char *s,u32 n) { char tmp[12]; int k=0; do { tmp[k++]='0'+n%10; n/=10; } while(n); while(k) *s++=tmp[--k]; *s=0; }
int fs_save(int id,const char *name,const char *data,u32 n) { (void)id;(void)name;(void)data;(void)n; ++saves; return 1; }
int fs_remove(int id) { (void)id; return 1; }
int fs_find(const char *s) { (void)s; return -1; }
void net_ping(void) { ++pings; }
void net_info(char *s) { copy(s,"eth0 (ETHERNET) / LINK UP\nIP 255.255.255.255  MASK 255.255.255.255\nGATEWAY 255.255.255.255  DNS 255.255.255.255\nMAC ff:ff:ff:ff:ff:ff\nSTATUS: CONNECTED - ETHERNET\nTX 4294967295 RX 4294967295"); }
void framebuffer_info(char *s) { copy(s,"framebuffer"); }
void process_list(char *s) { copy(s,"processes"); }
int process_action(int p,int a) { (void)p;(void)a; return 0; }
void process_fault_test(void) { CHECK(0); }
int http_get_public_ip(char *s,int cap) { (void)s;(void)cap; CHECK(0); return 0; }
u32 pmm_get_free_pages_count(void) { return 1; }
u32 pmm_get_total_pages_count(void) { return 2; }
int pci_scan_bus(PciDevice *p,int n) { (void)p;(void)n; return 0; }
const char *pci_class_name(u8 c,u8 s) { (void)c;(void)s; return "pci"; }
void speaker_beep(u32 f,u32 d) { (void)f;(void)d; }
void rtc_format_time(char *s,int cap) { (void)cap; copy(s,"00:00"); }

static void paint_apps(int w,int h) {
    sw=w; sh=h;
    for (int id=0;id<APP_COUNT;id++) test_sizes[id]=(GuiAppSize){w,h};
    notes_resized(w,h);
    void (*const renders[])(int,int,int)={welcome_render,files_render,terminal_render,notes_render,settings_render};
    for (current=0;current<5;current++) {
        paints=0; drawing=1; renders[current](w,h,1); drawing=0; CHECK(paints>0);
        if (current == APP_TERMINAL) {
            int cols = (w - 68) / 12;
            CHECK(terminal_visible_columns == (cmdlen >= cols ? cols - 1 : cmdlen));
            CHECK(terminal_prompt_y == h - 90);
        }
    }
}
static void interactions(int w,int h) {
    paint_apps(w,h);
    current=APP_FILES;
    AppRect list=files_list(w,h);
    files_scroll(-1000); CHECK(files_select_at(list.x+1,108)==0);
    CHECK(files_select_at(list.x-1,108)==-1);
    CHECK(files_select_at(list.x+list.w,108)==-1);
    CHECK(files_select_at(list.x+1,135)==-1); /* row gap */
    files_scroll(1000);
    int rows=list.h/31;
    CHECK(files_select_at(list.x+1,108+(rows-1)*31)==7);
    CHECK(files_select_at(list.x+1,108+rows*31)==-1);
    files_click(list.x+1,108+(rows-1)*31); CHECK(opened==APP_NOTES && notes_selected_file()==7);
    current=APP_SETTINGS;
    SettingsLayout l=settings_layout(w,h);
    settings_click(l.theme[1].x+1,l.theme[1].y+1); CHECK(theme==1);
    settings_click(l.theme[0].x+1,l.theme[0].y+1); CHECK(theme==0);
    settings_click(l.animation[1].x+1,l.animation[1].y+1); CHECK(!animations && stops>0);
    settings_click(l.animation[0].x+1,l.animation[0].y+1); CHECK(animations==1);
    int before=pings;
    settings_click(l.test.x+l.test.w,l.test.y); CHECK(pings==before);
    settings_click(l.test.x+1,l.test.y+1); CHECK(pings==before+1);
    current=APP_NOTES;
    AppRect save=note_save_rect(); before=saves;
    notes_click(save.x+save.w,save.y); notes_click(save.x,save.y+save.h); notes_click(w+100,h+100);
    CHECK(saves==before);
    notes_click(save.x+1,save.y+1); CHECK(saves==before+1);
    CHECK(notes_cursor(24,81)==1 && notes_cursor(23,81)==0);
    CHECK(notes_cursor(w-34,81)==1 && notes_cursor(w-33,81)==0);
    CHECK(notes_cursor(24,81+note_rows()*20+9)==1);
    CHECK(notes_cursor(24,81+note_rows()*20+10)==0);
}
int main(void) {
    for (int i=0;i<FS_FILES;i++) { copy(files[i].name,"long-file-name-12345.txt"); copy(files[i].data,"hello"); files[i].length=5; }
    notes_init();
    for (int i=0;i<60;i++) terminal_key(30,'W',0);
    CHECK(cmdlen==48); /* Display resizing must not change command semantics. */
    static const int sizes[][2]={{480,280},{640,480},{800,600},{680,410},{1200,800},
        {1008,626},{1904,938},{480,800},{1200,280},{600,360},
        {480,280},{800,600},{640,480},{480,280}};
    for (unsigned i=0;i<sizeof sizes/sizeof sizes[0];i++) interactions(sizes[i][0],sizes[i][1]);
    current=APP_NOTES;
    for (int i=0;i<900;i++) note[i]= i%91==0 ? '\n' : i%2 ? 'W' : 'i';
    note[900]=0; note_len=900;
    notes_resized(480,280); note_follow(); CHECK(note_scroll==note_max_scroll() && note_scroll>0);
    int narrow_rows=note_walk(0), narrow_visible=note_rows();
    int narrow_files=files_list(480,280).h;
    notes_resized(1200,800); CHECK(note_walk(0)<narrow_rows && note_scroll<=note_max_scroll());
    CHECK(note_rows()>narrow_visible && files_list(800,600).h>narrow_files);
    notes_scroll(2147483647); CHECK(note_scroll==note_max_scroll());
    notes_scroll(-2147483647-1); CHECK(note_scroll==0);
    notes_key(30,'x',0); CHECK(note_scroll==note_max_scroll());
    for (unsigned i=0;i<sizeof sizes/sizeof sizes[0];i++) paint_apps(sizes[i][0],sizes[i][1]);
    notes_resized(680,410);
    CHECK(notes_cursor(646,350) && !notes_cursor(647,350) && !notes_cursor(646,351));
    printf("PASS: real app geometry, hit bounds, file scrolling, Notes wrapping/cursor, Terminal limit (%d checks)\n",checks);
    return 0;
}
