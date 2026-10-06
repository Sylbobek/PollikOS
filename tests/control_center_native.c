#include "../kernel/control_center.c"
extern int printf(const char *,...);
static int errors,saves,volume=85,bright=100,device,invalidations,dialogs,opened;
static u32 clock_ms;
static GraphicsClip clip={0,0,1024,768};
static ThemeColors theme={.surface=0xffffff,.surface_elevated=0xf2eff6,.accent=0x2563eb,.text=0x202331};
#define CHECK(x) do{if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);errors++;}}while(0)
int ui_bridge_screen_width(void){return 1024;}int ui_bridge_screen_height(void){return 768;}
ThemeColors *ui_theme(void){return &theme;}
u32 wm_time_ms(void){return clock_ms;}
int ease_out_cubic(int t){if(t>=256)return 256;int inverse=256-t;return 256-((inverse*inverse*inverse)>>16);}
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
void ui_bridge_text(int x,int y,const char *s,u32 c,int scale){(void)y;(void)c;CHECK(x>=0&&scale==2);int width=0;while(*s++)width+=10;CHECK(x+width<=left()+WIDTH);}
void ui_dialog_message(const char *a,const char *b,IconKind c,void (*callback)(int)){(void)a;(void)b;(void)c;(void)callback;dialogs++;}
int main(void){
    control_center_toggle();CHECK(cc_shown&&cc_animating&&cc_position==-HEIGHT);
    control_center_poll(100);int previous=cc_position;CHECK(previous>-HEIGHT&&previous<0);
    control_center_draw();CHECK(clip.y1==0); /* Caller scissor restored. */
    clock_ms=100;control_center_toggle();CHECK(cc_position==previous&&!cc_opening);
    control_center_poll(150);previous=cc_position;CHECK(previous<0);
    clock_ms=150;control_center_toggle();CHECK(cc_position==previous&&cc_opening);
    control_center_poll(350);CHECK(cc_position==0&&!cc_animating);
    control_center_draw();int x=left(),y=top();
    control_center_click(x+30,y+70);CHECK(saves==0&&device==0); /* No fake radio toggles. */
    control_center_click(x+30,y+390);CHECK(volume==0);
    control_center_pointer(x+WIDTH+50,y+390,1);CHECK(volume==100&&saves==0);
    control_center_pointer(x+WIDTH/2,y+390,1);CHECK(volume>=49&&volume<=51&&saves==0);
    control_center_pointer(0,0,0);CHECK(saves==1&&cc_capture==-1);
    control_center_click(x+30,y+305);CHECK(bright==20&&saves==1);
    control_center_pointer(x+WIDTH+50,0,1);CHECK(bright==100&&saves==1&&invalidations==2);
    control_center_pointer(0,0,0);CHECK(saves==2);
    control_center_click(x+260,y+470);CHECK(device==0&&saves==2); /* AC97 absent. */
    CHECK(control_center_key(1));control_center_poll(550);CHECK(!cc_shown&&saves==2);
    clock_ms=550;control_center_toggle();control_center_poll(750);
    control_center_click(x+260,top()+504);CHECK(dialogs==1&&!cc_shown);
    clock_ms=750;control_center_toggle();control_center_poll(950);
    control_center_click(x+30,top()+504);CHECK(opened==APP_SETTINGS&&!cc_shown);
    printf("%s Control Center: slide/reversal/scissor, sliders/clamps/deferred saves, unavailable hardware, Settings/About\n",errors?"FAIL":"PASS");
    return !!errors;
}
