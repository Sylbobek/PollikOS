#include "app_internal.h"
#include "../../common/calc.h"
#include "../klog.h"

static CalcModel model;
void calculator_init(void) { calc_model_init(&model); }
static void changed(void) {
    if (model.evaluated) { serial("[CALC] "); serial(model.result); serial("\n"); }
    app_host_invalidate_partial(APP_CALCULATOR);
}
void calculator_render(int w,int h,int active) {
    (void)active;
    int dark=ui_is_dark();
    u32 ink=dark?0xf1f2fa:0x202331, muted=dark?0xaeb4c8:0x656b7c;
    rect(0,GUI_CHROME_HEIGHT,w,h-GUI_CHROME_HEIGHT,dark?0x171b28:0xfaf9fc);
    roundrect(16,GUI_CHROME_HEIGHT+12,w-32,88,12,dark?0x23293a:0xeeedf5);
    app_label(28,GUI_CHROME_HEIGHT+23,w-56,model.expression[0]?model.expression:"Enter an expression",muted,0);
    app_label(28,GUI_CHROME_HEIGHT+56,w-56,model.result[0]?model.result:"0",ink,1);
    for(int i=0;i<24;i++) {
        CalcRect r=calc_button_rect(w,h,GUI_CHROME_HEIGHT,i);
        int action=i%4==3 || i==0;
        u32 fill=i==23?app_host_accent_color():action?(dark?0x30384c:0xe1dfed):(dark?0x242a39:0xf0eff6);
        roundrect(r.x,r.y,r.w,r.h,8,fill);
        centered(r.x,r.y+(r.h-20)/2,r.w,calc_button_label(i),i==23?0xffffff:ink,0);
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
