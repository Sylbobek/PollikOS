#include <pollikos/window.h>
#include <pollikos/time.h>
#include <stdint.h>
#include <stdio.h>
#include "window_ui.h"
#include "../../common/calc.h"

static CalcModel model;
static void draw(PollikCanvas *c) {
    pollik_ui_fill(c,0,0,(int)c->width,(int)c->height,0x171b28);
    pollik_ui_fill(c,16,12,(int)c->width-32,88,0x23293a);
    pollik_ui_text(c,28,23,model.expression[0]?model.expression:"Enter an expression",0xaeb4c8);
    pollik_ui_text(c,28,56,model.result[0]?model.result:"0",0xf1f2fa);
    for(int i=0;i<24;i++) {
        CalcRect r=calc_button_rect((int)c->width,(int)c->height,0,i);
        pollik_ui_fill(c,r.x,r.y,r.w,r.h,i==23?0x7968d8:i%4==3?0x30384c:0x242a39);
        pollik_ui_text(c,r.x+10,r.y+(r.h-20)/2,calc_button_label(i),0xf1f2fa);
    }
}
int main(void) {
    int64_t mapped=pollikos_window_create(360,466,"Calculator (x86-64)");
    if(mapped<0) {puts("[calculator] cannot create window");return 1;}
    PollikCanvas c={(uint32_t *)(uintptr_t)mapped,360,466};
    calc_model_init(&model);draw(&c);
    if(pollikos_window_present()<0) {pollikos_window_destroy();return 2;}
    puts("[calculator] ready: native x86-64");
    for(;;) {
        pollikos_input_event_t e;
        if(pollikos_input_read(&e)==(int64_t)sizeof e) {
            if(e.kind&POLLIKOS_INPUT_WINDOW_CLOSE) break;
            int changed=0;
            if(e.kind&POLLIKOS_INPUT_KEY_DOWN) {
                char key=e.key==13?'\n':e.key==127?'\b':(char)e.key;
                calc_model_key(&model,key);changed=1;
            }
            if((e.kind&POLLIKOS_INPUT_MOUSE_BUTTON)&&(e.changed&POLLIKOS_MOUSE_LEFT)&&(e.buttons&POLLIKOS_MOUSE_LEFT)) {
                pollikos_window_info_t info;
                if(pollikos_window_info(&info)==0) {
                    int x=e.x-info.content_x,y=e.y-info.content_y;
                    for(int i=0;i<24;i++) {
                        CalcRect r=calc_button_rect(360,466,0,i);
                        if(x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h) {calc_model_button(&model,i);changed=1;break;}
                    }
                }
            }
            if(changed) {
                if(model.evaluated) printf("[calculator] result=%s\n",model.result);
                draw(&c);
                if(pollikos_window_present()<0) {pollikos_window_destroy();return 3;}
            }
        }
        (void)pollikos_sleep_ms(10);
    }
    return pollikos_window_destroy()<0?4:0;
}
