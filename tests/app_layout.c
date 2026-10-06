/* Native geometry regression: REAL five simple apps and editor, mock host and
 * glyph metrics. Includes implementations to inspect private layout state.
 * Not a pixel/font raster or WM propagation test. No OS boot, disk or network. */
#include "../kernel/gui/welcome.c"
#include "../kernel/gui/files.c"
#include "../kernel/gui/notes.c"
#include "../kernel/gui/settings.c"
#include "../kernel/gui/terminal.c"
#include "../kernel/gui/app_edit.c"
#include "../kernel/gui/calculator.c"
extern int printf(const char *, ...);
extern void exit(int);
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL %d: %s (app %d, %dx%d)\n", __LINE__, #x, current, sw, sh); exit(1); } } while (0)
static int checks, current, sw, sh, drawing, paints, saves, opened, theme, animations = 1, pings, stops;
static int terminal_invalidations;
static GuiAppSize test_sizes[APP_COUNT];
static int terminal_glyph_count, terminal_first_x, terminal_second_x, terminal_first_y, terminal_rounded_paints;
static int terminal_input_glyph_count, terminal_input_first_x, terminal_input_first_y;
static int terminal_input_last_y, terminal_input_first_row_glyphs;
static void (*dialog_input_callback)(const char *);
File files[FS_FILES];
int fs_ready, net_ready;
const char *fs_status = "Saved to PollikFS";
const char *net_status = "CONNECTED - ETHERNET";
volatile u32 ticks;
static int vfs_dir_pos, vfs_file_index;
GuiAppSize gui_app_size(int id) { return test_sizes[id]; }
static void bounds(int x,int y,int w,int h) {
    if (!drawing) return;
    ++paints;
    if(w<0||h<0||x<0||y<34||x+w>sw||y+h>sh)
        printf("RAW bounds: tab=%d x=%d y=%d w=%d h=%d window=%dx%d\n",g_settings_tab,x,y,w,h,sw,sh);
    CHECK(w >= 0 && h >= 0 && x >= 0 && y >= 34 && x + w <= sw && y + h <= sh);
}
int sys_get_glyph_advance(u8 c,int scale) { CHECK(scale>=1&&scale<=5); return (c == 'W' ? 8 : c == 'i' ? 3 : 6) * scale; }
int sys_text_width(const char *s,int scale) {
    int w = 0; while (*s) w += sys_get_glyph_advance(*s++,scale); return w;
}
static int font_h(int scale) { return scale == 1 ? 13 : scale == 2 ? 20 : scale == 3 ? 28 : 36; }
void ui_bridge_rect(int x,int y,int w,int h,u32 c) { (void)c; bounds(x,y,w,h); }
void ui_bridge_roundrect(int x,int y,int w,int h,int r,u32 c) { (void)r; if(drawing&&current==APP_TERMINAL)++terminal_rounded_paints; ui_bridge_rect(x,y,w,h,c); }
void ui_bridge_roundrect_border(int x,int y,int w,int h,int r,int t,u32 c) { (void)r; (void)t; if(drawing&&current==APP_TERMINAL)++terminal_rounded_paints; ui_bridge_rect(x,y,w,h,c); }
void app_draw_letter(int x,int y,u8 c,u32 color,int scale) {
    (void)color;
    if (drawing && current == APP_TERMINAL) {
        if (terminal_glyph_count == 0) { terminal_first_x=x; terminal_first_y=y; }
        else if (terminal_glyph_count == 1) terminal_second_x=x;
        terminal_glyph_count++;
        if (color == TERM_INPUT_TEXT_COLOR) {
            if (terminal_input_glyph_count == 0) {
                terminal_input_first_x=x; terminal_input_first_y=y;
            }
            if (y == terminal_input_first_y) terminal_input_first_row_glyphs++;
            terminal_input_last_y=y;
            terminal_input_glyph_count++;
        }
    }
    bounds(x,y,sys_get_glyph_advance(c,scale),font_h(scale));
}
void ui_bridge_text(int x,int y,const char *s,u32 c,int scale) {
    (void)c; bounds(x,y,sys_text_width(s,scale),font_h(scale));
}
void app_draw_centered(int x,int y,int w,const char *s,u32 c,int scale) {
    int tw = sys_text_width(s,scale); CHECK(tw <= w); ui_bridge_text(x+(w-tw)/2,y,s,c,scale);
}
void app_draw_mono(int x,int y,const char *s,u32 c) {
    (void)c;
    bounds(x,y,len(s)*12,16);
}
void app_host_invalidate(int id) { if (id == APP_TERMINAL) ++terminal_invalidations; }
void app_host_invalidate_partial(int id) { if (id == APP_TERMINAL) ++terminal_invalidations; }
void app_host_invalidate_partial_region(int id,int x,int y,int w,int h) {
    (void)id;(void)x;(void)y;(void)w;(void)h;
}
void ui_notify(const char *title,const char *message,IconKind icon) {
    (void)title;(void)message;(void)icon;
}
void ui_dialog_input(const char *title,const char *message,const char *initial,IconKind icon,void (*callback)(const char *)) {
    (void)title;(void)message;(void)initial;(void)icon;dialog_input_callback=callback;
}
void ui_dialog_message(const char *title,const char *message,IconKind icon,void (*callback)(int)) {
    (void)title;(void)message;(void)icon;(void)callback;
}
int process_spawn_elf_path(const char *path) { (void)path; return -1; }
void app_host_open(int id) { opened = id; }
void app_host_close(int id) { (void)id; }
int app_host_theme(void) { return theme; }
void app_host_set_theme(int n) { theme = n; }
int app_host_animations(void) { return animations; }
void app_host_set_animations(int n) { animations = n; }
void app_host_stop_minimize(void) { ++stops; }
int ui_is_dark(void) { return theme == 0; }
int app_host_accent(void) { return 0; }
void app_host_set_accent(int i) { (void)i; }
u32 app_host_accent_color(void) { return 0x2563eb; }
u32 app_host_accent_hover(void) { return 0x3b82f6; }
int app_host_dock_zoom(void) { return 1; }
void app_host_set_dock_zoom(int n) { (void)n; }
int app_host_target_fps(void) { return 60; }
void app_host_set_target_fps(int n) { (void)n; }
int app_host_sound_muted(void) { return 0; }
void app_host_set_sound_muted(int n) { (void)n; }
u32 app_host_sound_freq(void) { return 880; }
void app_host_set_sound_freq(u32 n) { (void)n; }
int app_host_wallpaper_count(void) { return 0; }
const char *app_host_wallpaper_name(int index) { (void)index; return ""; }
const char *app_host_selected_wallpaper(void) { return ""; }
int app_host_set_wallpaper(int index) { (void)index; return 0; }
int app_host_pointer_acceleration(void) { return 1; }
void app_host_set_pointer_acceleration(int enabled) { (void)enabled; }
static int cursor_size=100,setting_saves,volume=50;
int app_host_cursor_size(void) { return cursor_size; }
void app_host_set_cursor_size(int percent) { cursor_size=percent;setting_saves++; }
void app_host_preview_cursor_size(int percent) { cursor_size=percent; }
void app_host_save_settings(void) { setting_saves++; }
int audio_is_available(void) { return 0; }
int audio_output(void){return 0;}
int audio_select_output(int n){return n==0;}
u8 audio_get_volume(void) { return (u8)volume; }
void audio_set_volume(u8 v) { volume=v; }
void audio_play_sound(SoundEffect s) { (void)s; }
void audio_play_tone(u32 freq, u32 duration) { (void)freq; (void)duration; }
int audio_play_wav_file(const char *p) { (void)p; return 0; }
int process_get_count(void) { return 1; }
u32 pollikfs_free_blocks(void) { return 30000; }
NetworkInterface *net_manager_get_ethernet_iface(void) { return 0; }
int hal_cpu_has_cpuid(void) { return 0; }
void hal_cpuid(unsigned int l, unsigned int sl, unsigned int *a, unsigned int *b, unsigned int *c, unsigned int *d) { (void)l; (void)sl; *a = *b = *c = *d = 0; }
int framebuffer_width(void) { return sw; }
int framebuffer_height(void) { return sh; }
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
u32 pmm_get_total_memory(void) { return 8 * 1024 * 1024; }
u32 pmm_get_free_memory(void) { return 4 * 1024 * 1024; }
int pci_scan_bus(PciDevice *p,int n) { (void)p;(void)n; return 0; }
const char *pci_class_name(u8 c,u8 s) { (void)c;(void)s; return "pci"; }
void speaker_beep(u32 f,u32 d) { (void)f;(void)d; }
void rtc_format_time(char *s,int cap) { (void)cap; copy(s,"00:00"); }
void rtc_get_time(RtcTime *t) { memset(t,0,sizeof(*t)); }
void auth_lock(void) { }
void desktop_items_scan(void) { }
int trash_empty(void) { return 1; }
int trash_restore_item(const char *name) { (void)name; return 0; }
int trash_move_item(const char *path) { (void)path; return 0; }
int trash_delete_permanent(const char *name) { (void)name; return 0; }
void *kmalloc(u32 n) { (void)n; return 0; }
void kfree(void *p) { (void)p; }
void media_free(void *p) { (void)p; }
MediaGif *media_gif_open(const u8 *p,u32 n) { (void)p;(void)n;return 0; }
void media_gif_close(MediaGif *p) { (void)p; }
int media_gif_width(MediaGif *p) { (void)p;return 0; }
int media_gif_height(MediaGif *p) { (void)p;return 0; }
int media_gif_animating(MediaGif *p) { (void)p;return 0; }
const u8 *media_gif_canvas(MediaGif *p,int a) { (void)p;(void)a;return 0; }
int media_gif_due(MediaGif *p) { (void)p;return 0; }
MediaClip *media_clip_open(const u8 *p,u32 n) { (void)p;(void)n;return 0; }
void media_clip_close(MediaClip *p) { (void)p; }
int media_clip_width(MediaClip *p) { (void)p;return 0; }
int media_clip_height(MediaClip *p) { (void)p;return 0; }
int media_clip_animating(MediaClip *p) { (void)p;return 0; }
const u8 *media_clip_canvas(MediaClip *p,int a) { (void)p;(void)a;return 0; }
int media_clip_due(MediaClip *p) { (void)p;return 0; }
u8 *media_decode(const u8 *p,u32 n,int *w,int *h) { (void)p;(void)n;(void)w;(void)h;return 0; }
void ui_bridge_blit_rgba(int x,int y,int w,int h,const u8 *p,int sw,int sh) { (void)x;(void)y;(void)w;(void)h;(void)p;(void)sw;(void)sh; }
int vfs_open(const char *path,int flags) {
    if (eq(path,"/home") && flags == O_RDONLY) { vfs_dir_pos=0; return 1; }
    if ((flags & O_CREAT) && path[0]=='/' && path[1]=='h' && path[2]=='o' && path[3]=='m' && path[4]=='e' && path[5]=='/') return 3;
    if (path[0]=='/' && path[1]=='h' && path[2]=='o' && path[3]=='m' && path[4]=='e' && path[5]=='/') {
        for (int i=0;i<FS_FILES;i++) if (eq(path+6,files[i].name)) { vfs_file_index=i; return (flags & O_WRONLY) ? 3 : 2; }
    }
    return -1;
}
int vfs_close(int fd) { (void)fd; return 0; }
int vfs_read(int fd,void *out,u32 cap) {
    if (fd != 2) return -1;
    u32 n=files[vfs_file_index].length; if (n>cap) n=cap;
    for (u32 i=0;i<n;i++) ((char *)out)[i]=files[vfs_file_index].data[i];
    return (int)n;
}
int vfs_write(int fd,const void *data,u32 n) { (void)fd;(void)data;return (int)n; }
int vfs_stat(const char *path,vfs_stat_t *st) {
    if (path[0]=='/' && path[1]=='h' && path[2]=='o' && path[3]=='m' && path[4]=='e' && path[5]=='/')
        for (int i=0;i<FS_FILES;i++) if (eq(path+6,files[i].name)) { st->type=VFS_FILE;st->size=files[i].length;return 0; }
    return -1;
}
int vfs_readdir(int fd,vfs_dirent_t *out) {
    if (fd!=1 || vfs_dir_pos>=FS_FILES) return 0;
    out->type=VFS_FILE;out->inode=(u32)(vfs_dir_pos+1);copy(out->name,files[vfs_dir_pos++].name);return 1;
}
int vfs_mkdir(const char *p) { (void)p;return -1; }
int vfs_unlink(const char *p) { (void)p;return -1; }
int vfs_rmdir(const char *p) { (void)p;return -1; }
int vfs_rename(const char *a,const char *b) { (void)a;(void)b;return -1; }

static void paint_apps(int w,int h) {
    sw=w; sh=h;
    for (int id=0;id<APP_COUNT;id++) test_sizes[id]=(GuiAppSize){w,h};
    notes_resized(w,h);
    void (*const renders[])(int,int,int)={welcome_render,files_render,terminal_render,notes_render,settings_render};
    for (current=0;current<5;current++) {
        /* Settings' registry minimum keeps its fixed content inside the window. */
        if (current == APP_SETTINGS && (w < 640 || h < 410)) continue;
        paints=0; terminal_glyph_count=terminal_rounded_paints=0;
        drawing=1; renders[current](w,h,1); drawing=0; CHECK(paints>0);
        if (current == APP_TERMINAL) {
            CHECK(terminal_glyph_count >= 6);
            CHECK(terminal_second_x - terminal_first_x == sys_get_glyph_advance('P', 1));
            CHECK(terminal_first_y >= 48);
            CHECK(terminal_rounded_paints == 0);
        }
    }
}
static void interactions(int w,int h) {
    paint_apps(w,h);
    current=APP_FILES;
    AppRect list=files_list(w,h);
    files_scroll(-1000); CHECK(files_select_at(list.x+1,list.y+1)==0);
    CHECK(files_select_at(list.x-1,list.y+1)==-1);
    CHECK(files_select_at(list.x+list.w,list.y+1)==-1);
    CHECK(files_select_at(list.x+1,list.y+28)==-1); /* row gap */
    files_scroll(1000);
    int rows=list.h/31;
    if (rows > 0) {
        int last_visible_row=rows<FS_FILES ? rows-1 : FS_FILES-1;
        CHECK(files_select_at(list.x+1,list.y+last_visible_row*31+1)==FS_FILES-1);
        CHECK(files_select_at(list.x+1,list.y+rows*31+1)==-1);
        opened=-1;
        files_click(list.x+1,list.y+last_visible_row*31+1); CHECK(opened==-1);
        files_click(list.x+1,list.y+last_visible_row*31+1); CHECK(opened==APP_NOTES && notes_selected_file()==-1);
    } else CHECK(files_select_at(list.x+1,list.y+1)==-1);
    current=APP_SETTINGS;
    SettingsLayout l=settings_calc_layout(w,h);
    int cx=l.content.x;
    settings_click(l.tabs[0].x+1,l.tabs[0].y+1);
    settings_click(cx+25,119); CHECK(theme==1);
    settings_click(cx+139,119); CHECK(theme==0);
    settings_click(l.tabs[1].x+1,l.tabs[1].y+1);
    settings_click(cx+153,135); CHECK(!animations && stops>0);
    settings_click(cx+15,135); CHECK(animations==1);
    AppRect slider=slider_rect(l,1);int saved=setting_saves;
    CHECK(settings_drag(slider.x,slider.y,1)>=0 && cursor_size==100);
    CHECK(settings_drag(slider.x+slider.w+500,slider.y,1)==1 && cursor_size==200);
    CHECK(setting_saves==saved);
    settings_drag(0,0,0);CHECK(setting_saves==saved+1);
    settings_click(l.tabs[3].x+1,l.tabs[3].y+1);slider=slider_rect(l,0);saved=setting_saves;
    CHECK(settings_drag(slider.x-1,slider.y,1)==-1);
    CHECK(settings_drag(slider.x,slider.y,1)==1 && volume==0);
    CHECK(settings_drag(slider.x+slider.w+500,0,1)==1 && volume==100);
    CHECK(setting_saves==saved);settings_close();CHECK(setting_saves==saved+1);
    CHECK(settings_drag(slider.x+slider.w/2,slider.y,1)>=0 && volume>=49 && volume<=51);
    settings_drag(0,0,0);CHECK(setting_saves==saved+2);
    int before=pings;
    settings_click(l.tabs[4].x+1,l.tabs[4].y+1);
    settings_click(cx+165,310); CHECK(pings==before);
    settings_click(cx+15,311); CHECK(pings==before+1);
    current=APP_NOTES;
    notes_vfs_path[0]=0; AppRect save=note_save_rect(); before=saves;
    notes_click(save.x+save.w,save.y); notes_click(save.x,save.y+save.h); notes_click(w+100,h+100);
    CHECK(saves==before);
    notes_click(save.x+1,save.y+1); CHECK(saves==before+1);
    int note_bottom=82+note_rows()*20+20;
    CHECK(notes_cursor(14,82)==1 && notes_cursor(13,82)==0);
    CHECK(notes_cursor(w-15,82)==1 && notes_cursor(w-14,82)==0);
    CHECK(notes_cursor(24,note_bottom-1)==1 && notes_cursor(24,note_bottom)==0);
}
int main(void) {
    for (int i=0;i<FS_FILES;i++) { copy(files[i].name,"long-file-name-12345.txt"); copy(files[i].data,"hello"); files[i].length=5; }
    notes_init();
    notes_key_ex(30,0,0,1); /* Ctrl+A selects the note. */
    notes_key_ex(46,0,0,1); /* Ctrl+C copies selected text. */
    char note_copy[16];
    CHECK(app_clipboard_paste(note_copy,sizeof(note_copy))==5 && eq(note_copy,"hello"));
    notes_key_ex(71,0,0,0); /* Home, then insert at the caret. */
    notes_key_ex(0,'X',0,0);
    CHECK(eq(notes_text(),"Xhello"));
    notes_click(64,91);
    CHECK(notes_drag(84,91,1)==1 && note_has_selection());
    notes_drag(84,91,0);
    notes_key_ex(46,0,0,1);
    CHECK(app_clipboard_paste(note_copy,sizeof(note_copy))==2 && eq(note_copy,"Xh"));
    notes_key_ex(30,0,0,1); notes_key_ex(47,0,0,1);
    CHECK(eq(notes_text(),"Xh"));
    notes_click(note_save_as_rect().x+4,note_save_as_rect().y+4);
    CHECK(dialog_input_callback != 0);
    dialog_input_callback("/home/draft.txt");
    CHECK(!notes_changed() && eq(note_name,"draft.txt") && eq(notes_vfs_path,"/home/draft.txt"));
    for (int i=0;i<180;i++) terminal_key(30,'W',0,0);
    CHECK(cmdlen==TERM_COMMAND_LENGTH-1); /* Display resizing must not change command semantics. */
    cmdlen=cmdcursor=0; command[0]=0; transcript[0]=0;
    for (int i=0;i<180;i++) terminal_key(30,'W',0,0);
    current=APP_TERMINAL;
    terminal_input_glyph_count=terminal_input_first_row_glyphs=0;
    sw=480; sh=800;
    test_sizes[APP_TERMINAL]=(GuiAppSize){480,800};
    drawing=1; terminal_render(480,800,1); drawing=0;
    int prompt_width=sys_text_width("pollik:/home/Desktop$ ",1);
    int input_columns=(480-24-18-prompt_width)/sys_get_glyph_advance('W',1);
    CHECK(terminal_input_glyph_count==cmdlen);
    CHECK(terminal_input_first_x==18+prompt_width);
    CHECK(terminal_input_first_row_glyphs==input_columns);
    CHECK(terminal_input_last_y>terminal_input_first_y);
    cmdlen=cmdcursor=0; command[0]=0;
    terminal_key(0,'h',0,0); terminal_key(0,'e',0,0); terminal_key(0,'l',0,0); terminal_key(0,'p',0,0);
    terminal_invalidations=0;
    terminal_key(28,0,0,0);
    CHECK(cmdlen==0 && history_count==1 && eq(history[0],"help"));
    CHECK(terminal_invalidations==1);
    CHECK(output[0]=='F' && output[1]=='I' && output[2]=='L' && output[3]=='E' && output[4]=='S');
    test_sizes[APP_TERMINAL]=(GuiAppSize){1200,800};
    terminal_click(18,68);
    int files_width = 0;
    for (const char *p = "FILES:"; *p; p++) files_width += sys_get_glyph_advance((u8)*p, 1);
    terminal_drag(18 + files_width,68,1);
    terminal_drag(18 + files_width,68,0);
    terminal_key(46,0,0,1); /* Ctrl+C copies selected output. */
    char copied[16];
    CHECK(app_clipboard_paste(copied,sizeof(copied))==6 && eq(copied,"FILES:"));
    terminal_key(47,0,0,1); /* Ctrl+V inserts the shared clipboard at the prompt. */
    CHECK(eq(command,"FILES:") && cmdcursor==6);
    terminal_key(75,0,1,0); /* Shift+Left selects the preceding input character. */
    terminal_key(46,0,0,1);
    CHECK(app_clipboard_paste(copied,sizeof(copied))==1 && eq(copied,":"));
    for(int i=0;i<24;i++) append_transcript("scroll test line\n");
    terminal_scroll(-3); CHECK(scroll_lines==3);
    terminal_scroll(3); CHECK(scroll_lines==0);
    cmdlen=cmdcursor=0; command[0]=0; terminal_clear_selection();
    static const int sizes[][2]={{480,280},{640,410},{640,480},{800,600},{680,410},{1200,800},
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
    int final_note_bottom=82+note_rows()*20+20;
    CHECK(notes_cursor(665,final_note_bottom-1) && !notes_cursor(666,final_note_bottom-1) &&
          !notes_cursor(665,final_note_bottom));
    calculator_init();
    current=APP_CALCULATOR;
    static const int calculator_sizes[][2]={{320,440},{360,500},{800,650}};
    for(unsigned i=0;i<3;i++) {
        sw=calculator_sizes[i][0];sh=calculator_sizes[i][1];
        test_sizes[APP_CALCULATOR]=(GuiAppSize){sw,sh};drawing=1;
        calculator_render(sw,sh,1);drawing=0;
        calculator_key(1,0,0,0);
        const int buttons[]={16,19,17,23}; /* 1 + 2 = */
        for(unsigned j=0;j<4;j++) {
            CalcRect r=calc_button_rect(sw,sh,34,buttons[j]);
            calculator_click(r.x+r.w/2,r.y+r.h/2);
        }
        CHECK(model.evaluated&&eq(model.result,"3"));
        calculator_key(1,0,0,0);calculator_key(3,'2',0,0);
        calculator_key(13,'=',1,0);calculator_key(4,'3',0,0);
        calculator_key(28,0,0,0);CHECK(model.evaluated&&eq(model.result,"5"));
        calculator_key(1,0,0,0);calculator_click(-1,-1);CHECK(!model.expression[0]);
    }
    current=APP_SETTINGS;sw=640;sh=520;test_sizes[APP_SETTINGS]=(GuiAppSize){sw,sh};
    for(int mode=0;mode<2;mode++)for(int tab=0;tab<TAB_COUNT;tab++) {
        theme=mode;g_settings_tab=tab;g_sound_played=1;drawing=1;
        settings_render(sw,sh,1);drawing=0;
    }
    printf("PASS: real app geometry, hit bounds, file scrolling, Notes wrapping/cursor, Terminal prompt wrapping, Calculator (%d checks)\n",checks);
    return 0;
}
