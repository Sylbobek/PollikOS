/* Polled PS/2 auxiliary-port mouse input for the x86_64 graphical console. */
#include "mouse.h"
#include "console_fb.h"
#include "../../hal.h"
#include "../../../common/ps2_pointer.h"
#include "scheduler.h"

static int enabled;
static Ps2Pointer ps2_pointer;
static int x, y;
static unsigned buttons;
static MouseEvent64 event_queue[64];
static unsigned event_head, event_count;
static uint64_t event_sequence;

static uint8_t in8(uint16_t port) {
    return hal_port_read8(port);
}
static void out8(uint16_t port, uint8_t value) {
    hal_port_write8(port, value);
}
static int wait_input(void) {
    for (unsigned i=0; i<100000; ++i) if (!(in8(0x64)&2)) return 1;
    return 0;
}
static int wait_output(uint8_t *value, int auxiliary) {
    for (unsigned i=0; i<100000; ++i) {
        uint8_t status=in8(0x64);
        if (status&1) {
            uint8_t byte=in8(0x60);
            if (!!(status&0x20)==!!auxiliary) { *value=byte; return 1; }
        }
    }
    return 0;
}
static int command(uint8_t value) {
    if (!wait_input()) return 0;
    out8(0x64,value);
    return 1;
}
static int data(uint8_t value) {
    if (!wait_input()) return 0;
    out8(0x60,value);
    return 1;
}
static int auxiliary(uint8_t value) {
    for(unsigned retry=0;retry<3;retry++){
        uint8_t response;if(!command(0xd4)||!data(value)||!wait_output(&response,1))return 0;
        if(response==0xfa)return 1;if(response!=0xfe)return 0;
    }return 0;
}
static int auxiliary_read(void){uint8_t byte;return wait_output(&byte,1)?byte:-1;}
static void queue_event(unsigned kind, unsigned changed, int wheel) {
    MouseEvent64 event={USER_INPUT_EVENT_VERSION,USER_INPUT_EVENT_SIZE,kind,buttons,
                        changed,0,0,x,y,wheel,++event_sequence};
    if (event_count==sizeof(event_queue)/sizeof(event_queue[0])) {
        event_head=(event_head+1)%(sizeof(event_queue)/sizeof(event_queue[0]));
        --event_count;
    }
    event_queue[(event_head+event_count)%(sizeof(event_queue)/sizeof(event_queue[0]))]=event;
    ++event_count;
}

int mouse64_init(void) {
    uint8_t config;
    if (!command(0xa8) || !command(0x20) || !wait_output(&config,0)) return 0;
    /* Keep both PIC IRQs disabled: tty64_poll() services the controller on PIT. */
    config=(uint8_t)((config|0x40)&~0x33);
    if(!command(0x60)||!data(config)||!ps2_pointer_configure(&ps2_pointer,auxiliary,auxiliary_read))return 0;
    x=(int)console_fb_width()/2;
    y=(int)console_fb_height()/2;
    buttons=0;
    event_head=event_count=0;
    enabled=1;
    console_fb_mouse_move(x,y);
    console_fb_mouse_enable();
    return 1;
}

static void pointer_motion(const Ps2Motion *motion){
    int old_x=x,old_y=y;
    unsigned old_buttons=buttons;
    int wheel=motion->wheel;buttons=motion->buttons;
    x+=motion->dx;y+=motion->dy;
    if (x<0) x=0; else if ((unsigned)x>=console_fb_width()) x=(int)console_fb_width()-1;
    if (y<0) y=0; else if ((unsigned)y>=console_fb_height()) y=(int)console_fb_height()-1;
    console_fb_mouse_move(x,y);
    unsigned changed=old_buttons^buttons;
    unsigned kind=(x!=old_x || y!=old_y)?USER_INPUT_MOUSE_MOVE:0;
    if (changed) kind|=USER_INPUT_MOUSE_BUTTON;
    if (wheel) kind|=USER_INPUT_MOUSE_WHEEL;
    if (kind) queue_event(kind,changed,wheel);
}
void mouse64_byte(uint8_t value){
    if(!enabled)return;Ps2Motion motion;
    if(!ps2_pointer_feed(&ps2_pointer,value,(unsigned)(scheduler64_ticks()*10),&motion))return;
    if(motion.tap){Ps2Motion down=motion;down.buttons|=1;pointer_motion(&down);motion.dx=motion.dy=motion.wheel=0;}
    pointer_motion(&motion);
}

int mouse64_pop(MouseEvent64 *event) {
    if (!enabled || !event || !event_count) return 0;
    *event=event_queue[event_head];
    event_head=(event_head+1)%(sizeof(event_queue)/sizeof(event_queue[0]));
    --event_count;
    return 1;
}
void mouse64_flush(void) { event_head=event_count=0; }
int mouse64_wheel_enabled(void) { return enabled && (ps2_pointer.size==4||ps2_pointer.multifinger); }
