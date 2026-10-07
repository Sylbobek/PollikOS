#include "app_internal.h"
#include "../../common/calc.h"
#include "../klog.h"

static CalcModel model;
void calculator_init(void) { calc_model_init(&model); }
void calculator_close(void) { memset(&model,0,sizeof(model));calculator_init(); }
static void changed(void) {
    if (model.evaluated) { serial("[CALC] "); serial(model.result); serial("\n"); }
    /* Editing changes only the readout, not the 24 cached button faces. */
    GuiAppSize s=gui_app_size(APP_CALCULATOR);
    app_host_invalidate_partial_region(APP_CALCULATOR,16,GUI_CHROME_HEIGHT+12,s.width-32,88);
}
void calculator_render(int w,int h,int active) {
    (void)active;
    int dark=ui_is_dark();
    u32 ink=dark?0xf6f5fa:0x252330, muted=dark?0xb8b5c2:0x77727e;
    rect(0,GUI_CHROME_HEIGHT,w,h-GUI_CHROME_HEIGHT,dark?0x24232b:0xf2f0f5);
    roundrect(16,GUI_CHROME_HEIGHT+12,w-32,88,12,dark?0x1b1a22:0xffffff);
    app_label(28,GUI_CHROME_HEIGHT+23,w-56,model.expression[0]?model.expression:"Enter an expression",muted,1);
    const char *result=model.result[0]?model.result:"0";
    int scale=model.evaluated||!model.expression[0]?3:2;
    int tw=sys_text_width(result,scale);
    if(tw>w-56){scale=1;tw=sys_text_width(result,scale);}
    int x=w-28-tw;if(x<28)x=28;
    app_label(x,GUI_CHROME_HEIGHT+54,w-28-x,result,ink,scale);
    for(int i=0;i<24;i++) {
        CalcRect r=calc_button_rect(w,h,GUI_CHROME_HEIGHT,i);
        int action=i%4==3;
        u32 fill=action?0xd97523:i<8?(dark?0x55515f:0xdad6e0):(dark?0x3c3945:0xffffff);
        roundrect(r.x,r.y,r.w,r.h,10,fill);
        roundrect_border(r.x,r.y,r.w,r.h,10,1,action?0xee9848:dark?0x655f71:0xe3dfe9);
        int fs=(i==1||i==4||i==22)?1:2;
        centered(r.x,r.y+(r.h-(fs==2?20:13))/2,r.w,calc_button_label(i),action?0xffffff:ink,fs);
    }
}
void calculator_key(u8 code,char ch,int shift,int control) {
    if(control) return;
    if(code==28) ch='\n';
    else if(code==14) ch='\b';
    else if(code==1) ch=27;
    else if(shift) {
        const char *from="1234567890-=", *to="!@#$%^&*()_+";
        for(int i=0;from[i];i++) if(ch==from[i]) {ch=to[i];break;}
    }
    if(ch) {calc_model_key(&model,ch);changed();}
}
void calculator_click(int x,int y) {
    GuiAppSize s=gui_app_size(APP_CALCULATOR);
    for(int i=0;i<24;i++) {
        CalcRect r=calc_button_rect(s.width,s.height,GUI_CHROME_HEIGHT,i);
        if(x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h) {calc_model_button(&model,i);changed();return;}
    }
}
