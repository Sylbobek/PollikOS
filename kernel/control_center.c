#include "control_center.h"
#include "gui/app_host.h"
#include "gui/apps.h"
#include "ui.h"
#include "graphics.h"
#include "audio.h"
#include "net/net_manager.h"
#include "wm.h"
#include "ui_animation.h"
void request_partial_redraw(int x,int y,int w,int h);
/* Independent overlay state; no change to window, menu or syscall layouts. */
static int cc_shown,cc_opening,cc_animating,cc_position=-520,cc_start_position=-520;
static int cc_capture=-1,cc_settings_changed;
static u32 cc_started;
enum { WIDTH=400,HEIGHT=530,DURATION=200 };
static int left(void){return (ui_bridge_screen_width()-WIDTH)/2;}
static int top(void){return 38+cc_position;}
static void damage(void){request_partial_redraw(left()-6,32,WIDTH+12,HEIGHT+44);}
int control_center_active(void){return cc_shown;}
static void commit(void){if(cc_settings_changed)app_host_save_settings();cc_settings_changed=0;cc_capture=-1;}
void control_center_close(void){if(cc_shown){damage();commit();cc_shown=cc_animating=cc_opening=0;cc_position=-HEIGHT;}}
void control_center_toggle(void){
    if(!cc_shown){cc_shown=1;cc_position=-HEIGHT;cc_opening=0;}
    commit();cc_start_position=cc_position;cc_opening=!cc_opening;cc_animating=1;cc_started=wm_time_ms();damage();
}
int control_center_poll(unsigned now){
    if(!cc_shown||!cc_animating)return 0;
    u32 elapsed=now-cc_started;int t=elapsed>=DURATION?256:(int)(elapsed*256/DURATION);
    int goal=cc_opening?0:-HEIGHT;
    cc_position=cc_start_position+(goal-cc_start_position)*ease_out_cubic(t)/256;
    damage();
    if(t==256){cc_animating=0;if(!cc_opening){cc_shown=0;commit();}}
    return 1;
}
static void label(int x,int y,const char *s,u32 color){ui_bridge_text(left()+x,top()+y,s,color,2);}
static void card(int y,int h){ThemeColors *t=ui_theme();ui_bridge_roundrect(left()+16,top()+y,WIDTH-32,h,12,t->surface);}
static void slider(int y,int value,int minimum){
    ThemeColors *t=ui_theme();int x=left()+30,w=WIDTH-60;
    int offset=(w-1)*(value-minimum)/(100-minimum);
    ui_bridge_roundrect(x,top()+y+10,w,6,3,t->border);
    ui_bridge_roundrect(x,top()+y+10,offset+1,6,3,t->accent);
    ui_bridge_roundrect(x+offset-8,top()+y+4,16,18,8,t->text);
}
void control_center_draw(void){
    if(!cc_shown)return;
    GraphicsClip old=graphics_get_clip(),clip=old;if(clip.y1<32)clip.y1=32;graphics_set_clip(clip);
    ThemeColors *t=ui_theme();int x=left(),y=top();
    ui_bridge_rounded(x-3,y+3,WIDTH+6,HEIGHT+6,18,0x080610,64);
    ui_bridge_roundrect_stroke(x,y,WIDTH,HEIGHT,16,1,t->border,t->surface_elevated);
    label(20,14,"Control Center",t->text);
    card(52,112);label(30,62,"Wi-Fi",t->text);label(210,62,"Bluetooth",t->text);
    label(30,92,"No adapter",t->text_secondary);label(210,92,"No adapter",t->text_secondary);
    label(30,130,"Airplane mode",t->text_secondary);label(220,130,"No radios",t->text_muted);
    card(176,62);label(30,187,"Network",t->text);
    NetworkInterface *iface=net_manager_get_active_iface();
    label(166,187,iface&&iface->link_up?"Ethernet":"Disconnected",t->text_secondary);
    card(250,76);label(30,260,"Brightness",t->text);char b[16];number(b,(u32)framebuffer_brightness());
    label(312,260,b,t->text_secondary);slider(292,framebuffer_brightness(),20);
    card(338,76);label(30,348,"Volume",t->text);number(b,audio_get_volume());label(312,348,b,t->text_secondary);slider(380,audio_get_volume(),0);
    card(426,60);label(30,435,"Sound output",t->text);
    for(int i=0;i<2;i++) {
        u32 color=audio_output()==i?t->accent:t->surface_secondary;
        if(i&&!audio_is_available())color=t->surface_secondary;
        ui_bridge_roundrect(x+20+i*184,y+459,176,24,7,color);
        label(30+i*184,460,i?"AC'97":"PC Speaker",i&&!audio_is_available()?t->text_muted:audio_output()==i?0xffffff:t->text);
    }
    label(30,498,"Settings",t->accent);label(238,498,"About",t->accent);
    graphics_set_clip(old);
}
static int update_slider(int x){
    int distance=x-(left()+30),w=WIDTH-60;if(distance<0)distance=0;if(distance>=w)distance=w-1;
    int value=(distance*100+(w-1)/2)/(w-1),changed=0;
    if(cc_capture==0){changed=value!=audio_get_volume();if(changed)audio_set_volume((u8)value);}
    else {value=20+value*80/100;changed=value!=framebuffer_brightness();if(changed){framebuffer_set_brightness(value);app_host_invalidate(-1);}}
    if(changed){cc_settings_changed=1;damage();}return changed;
}
int control_center_pointer(int x,int y,int down){
    (void)y;if(!cc_shown)return 0;
    if(!down){commit();return 1;}
    if(cc_capture>=0)update_slider(x);
    return 1;
}
int control_center_click(int x,int y){
    if(!cc_shown)return 0;
    int lx=x-left(),ly=y-top();
    if(lx<0||lx>=WIDTH||ly<0||ly>=HEIGHT){control_center_toggle();return 1;}
    if(cc_animating)return 1;
    if(lx>=22&&lx<=WIDTH-22) {
        if(ly>=286&&ly<320){cc_capture=1;update_slider(x);return 1;}
        if(ly>=374&&ly<408){cc_capture=0;update_slider(x);return 1;}
    }
    if(ly>=459&&ly<485) {
        int device=lx>=204;if(audio_select_output(device)){app_host_save_settings();damage();}return 1;
    }
    if(ly>=492) {
        int about=lx>=220;control_center_close();
        if(about)ui_dialog_message("About PollikOS","PollikOS\nDesktop: i386\nPollikFS v2",ICON_INFO,0);
        else app_host_open(APP_SETTINGS);
    }
    return 1;
}
int control_center_key(unsigned char code){
    if(!cc_shown)return 0;if(code==1&&cc_opening)control_center_toggle();return 1;
}
