#include "../kernel/control_center.c"
extern int printf(const char *,...);
static int errors,saves,volume=85,bright=100,device,invalidations,dialogs,opened;
static int power=-1,logouts;
static unsigned saved_pins;
static int admin_requested=-1;
unsigned app_host_launcher_pins(void){return saved_pins;}
int app_host_launcher_set_pins(unsigned pins){saved_pins=pins;return 1;}
void auth_run_admin(int id){admin_requested=id;}
void ui_icon_draw(int id,int x,int y,int size){(void)id;(void)x;(void)y;(void)size;}
int text_width(const char *s,int scale){int n=0;while(*s++)n+=scale==1?6:10;return n;}
void ui_bridge_rect(int x,int y,int w,int h,u32 c){(void)x;(void)y;(void)w;(void)h;(void)c;}
void app_host_power(int reboot){power=reboot;}
void auth_logout(void){logouts++;}
const char *auth_username(void){return "Test user";}
const GuiApp *gui_app_get(int id){
    static const GuiApp apps[APP_COUNT]={{.name="Welcome"},{.name="Files"},{.name="Terminal"},{.name="Notes"},
        {.name="Settings"},{.name="Browser"},{.name="PollikMark3D"},{.name="Calculator"}};
    return id>=0&&id<APP_COUNT?&apps[id]:0;
}
char gui_key_character(u8 code){return code==30?'a':code==33?'f':code==23?'i':code==38?'l':code==18?'e':code==31?'s':code==44?'z':0;}
static u32 clock_ms;
static int networking=1;
int net_manager_enabled(void){return networking;}
void net_manager_set_enabled(int enabled){networking=enabled;}
static GraphicsClip clip={0,0,1024,768};
static ThemeColors theme={.surface=0xffffff,.surface_elevated=0xf2eff6,.accent=0x2563eb,.text=0x202331};
#define CHECK(x) do{if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);errors++;}}while(0)
int ui_bridge_screen_width(void){return 1024;}int ui_bridge_screen_height(void){return 768;}
ThemeColors *ui_theme(void){return &theme;}
u32 wm_time_ms(void){return clock_ms;}
int ease_in_out_cubic(int t){if(t>=256)return 256;if(t<128)return (t*t*t)>>14;int inverse=256-t;return 256-((inverse*inverse*inverse)>>14);}
void request_partial_redraw(int x,int y,int w,int h){CHECK(x>=0&&y==32&&w>0&&h>0);}
void app_host_save_settings(void){saves++;}void app_host_invalidate(int id){CHECK(id==-1);invalidations++;}
void app_host_open(int id){opened=id;}
u8 audio_get_volume(void){return (u8)volume;}void audio_set_volume(u8 v){volume=v;}
int audio_is_available(void){return 0;}int audio_output(void){return device;}
int audio_select_output(int id){if(id!=0)return 0;device=id;return 1;}
int framebuffer_brightness(void){return bright;}void framebuffer_set_brightness(int b){bright=b<20?20:b>100?100:b;}
NetworkInterface *net_manager_get_active_iface(void){return 0;}
void number(char *out,u32 n){char b[16];int i=0;do{b[i++]=(char)('0'+n%10);n/=10;}while(n);int j=0;while(i)out[j++]=b[--i];out[j]=0;}
GraphicsClip graphics_get_clip(void){return clip;}void graphics_set_clip(GraphicsClip c){clip=c;}
static void rectangle(int x,int y,int w,int h){(void)y;CHECK(x>=0&&x+w<=1024&&w>=0&&h>=0&&clip.y1>=32);}
void ui_bridge_roundrect(int x,int y,int w,int h,int r,u32 c){(void)r;(void)c;rectangle(x,y,w,h);}
void ui_bridge_roundrect_stroke(int x,int y,int w,int h,int r,int t,u32 c,u32 f){(void)t;(void)f;ui_bridge_roundrect(x,y,w,h,r,c);}
void ui_bridge_rounded(int x,int y,int w,int h,int r,u32 c,int a){(void)a;ui_bridge_roundrect(x,y,w,h,r,c);}
void ui_bridge_text(int x,int y,const char *s,u32 c,int scale){(void)y;(void)c;(void)s;CHECK(x>=0&&x<1024&&(scale==1||scale==2));}
void ui_dialog_message(const char *a,const char *b,IconKind c,void (*callback)(int)){(void)a;(void)b;(void)c;(void)callback;dialogs++;}
static void advance(unsigned ms){clock_ms+=ms;control_center_poll(clock_ms);}
static void settle(void){advance(DURATION);advance(DETAIL_DURATION);}
int main(void){
    AppSearchInput edit;app_search_reset(&edit);app_search_edit(&edit,'a',0,0);app_search_edit(&edit,'b',0,0);
    app_search_edit(&edit,APP_SEARCH_LEFT,1,0);CHECK(edit.anchor==2&&edit.cursor==1);
    app_search_edit(&edit,'x',0,0);CHECK(edit.text[0]=='a'&&edit.text[1]=='x'&&!edit.text[2]);
    app_search_edit(&edit,'a',0,1);app_search_edit(&edit,127,0,0);CHECK(!edit.text[0]&&edit.cursor==0);
    control_center_toggle();CHECK(cc_shown&&cc_animating&&cc_position==-HEIGHT);
    advance(100);int previous=cc_position;CHECK(previous>-HEIGHT&&previous<0);
    control_center_draw();CHECK(clip.y1==0); /* Caller scissor restored. */
    control_center_toggle();CHECK(cc_position==previous&&!cc_opening);
    advance(50);previous=cc_position;CHECK(previous<0);
    control_center_toggle();CHECK(cc_position==previous&&cc_opening);
    settle();CHECK(cc_position==0&&!cc_animating);
    control_center_draw();int x=left(),y=top();
    control_center_click(x+30,y+70);CHECK(saves==0&&device==0); /* No fake radio toggles. */
    control_center_click(x+30,y+390);CHECK(volume==0);
    control_center_pointer(x+WIDTH+50,y+390,1);CHECK(volume==100&&saves==0);
    control_center_pointer(x+WIDTH/2,y+390,1);CHECK(volume>=49&&volume<=51&&saves==0);
    control_center_pointer(0,0,0);CHECK(saves==1&&cc_capture==-1);
    control_center_click(x+30,y+305);CHECK(bright==20&&saves==1);
    control_center_pointer(x+WIDTH+50,0,1);CHECK(bright==100&&saves==1&&invalidations==2);
    control_center_pointer(0,0,0);CHECK(saves==2);
    control_center_click(x+360,y+360);CHECK(cc_list==3);
    settle();CHECK(cc_detail_kind==3&&cc_detail_slide==256);
    control_center_draw();CHECK(clip.y1==0);
    control_center_click(detail_left()+30,y+detail_top(3)+58+38+10);CHECK(device==0&&saves==2); /* AC97 absent. */
    control_center_click(x+160,y+70);CHECK(cc_list==1);
    advance(DETAIL_DURATION/2);CHECK(cc_detail_kind==3&&cc_detail_slide>0&&cc_detail_slide<256);
    control_center_draw();CHECK(clip.y1==0);
    advance(DETAIL_DURATION/2);CHECK(cc_detail_kind==1&&cc_detail_slide==0);
    advance(DETAIL_DURATION);CHECK(cc_detail_slide==256);
    control_center_click(x+345,y+70);CHECK(cc_list==2);
    settle();CHECK(cc_detail_kind==2&&cc_detail_slide==256);
    control_center_click(x+30,y+140);CHECK(!networking);
    control_center_click(x+30,y+140);CHECK(networking);
    CHECK(control_center_key(1));settle();CHECK(cc_shown&&!cc_detail_kind&&!cc_list);
    CHECK(control_center_key(1));settle();CHECK(!cc_shown&&saves==2);
    control_center_toggle();settle();
    control_center_click(search_left()+20,top()+60);CHECK(cc_search_focus);
    const u8 files[]={33,23,38,18,31};for(unsigned i=0;i<sizeof(files);i++)control_center_key(files[i]);
    CHECK(app_search_matches("Files",cc_query)&&!app_search_matches("Terminal",cc_query));
    control_center_key(28);CHECK(opened==APP_FILES&&!cc_shown&&cc_query[0]==0);
    control_center_toggle();settle();
    control_center_click(search_left()+20,top()+60);control_center_key(44);control_center_key(44);
    opened=-1;control_center_key(28);CHECK(opened==-1&&cc_shown);control_center_key(14);control_center_key(14);
    CHECK(app_at(0)==-1);control_center_key_ex(30,0,1);control_center_key(33);control_center_key(23);
    control_center_click(search_left()+SEARCH_WIDTH-16,top()+124);CHECK(cc_context==APP_FILES);
    control_center_click(search_left()+20,top()+198);CHECK(saved_pins==(1u<<APP_FILES));
    control_center_close();control_center_toggle();settle();
    CHECK(app_at(0)==APP_FILES);control_center_click(search_left()+SEARCH_WIDTH-16,top()+124);
    control_center_click(search_left()+20,top()+164);CHECK(admin_requested==APP_FILES&&!cc_shown);
    for(int action=0;action<3;action++){
        control_center_toggle();settle();
        control_center_click(left()+30,top()+444);CHECK(cc_list==4);
        advance(DETAIL_DURATION/2);CHECK(cc_detail_slide>0&&cc_detail_slide<256);
        control_center_draw();CHECK(clip.y1==0);
        advance(DETAIL_DURATION/2);CHECK(cc_detail_kind==4&&cc_detail_slide==256);
        CHECK(detail_left()>=left()+WIDTH+DETAIL_GAP);
        control_center_click(detail_left()+30,top()+detail_top(4)+70+action*38);CHECK(!cc_shown);
        if(action<2)CHECK(power==action);else CHECK(logouts==1);
    }
    printf("%s Control Center: search edit/selection, saved pins, admin request, animation/scissor, sliders, profile actions\n",errors?"FAIL":"PASS");
    return !!errors;
}
