/* Polled PS/2 auxiliary-port mouse input for the x86_64 graphical console. */
#include "mouse.h"
#include "console_fb.h"
#include "../../hal.h"

static int enabled;
static uint8_t packet[4];
static unsigned packet_size=3;
static uint8_t device_id;
static unsigned packet_length;
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
    uint8_t response;
    return command(0xd4) && data(value) && wait_output(&response,1) && response==0xfa;
}
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
    config=(uint8_t)(config&~0x23);
    if (!command(0x60) || !data(config) || !auxiliary(0xf6)) return 0;
    /* IntelliMouse sample-rate unlock; standard mice fall back to 3-byte mode. */
    if (auxiliary(0xf3) && auxiliary(200) && auxiliary(0xf3) && auxiliary(100) &&
        auxiliary(0xf3) && auxiliary(80) && command(0xd4) && data(0xf2)) {
        uint8_t ack, id;
        if (wait_output(&ack,1) && ack==0xfa && wait_output(&id,1) && (id==3 || id==4)) {
            packet_size=4;
            device_id=id;
        }
    }
    if (!auxiliary(0xf4)) return 0;
    x=(int)console_fb_width()/2;
    y=(int)console_fb_height()/2;
    packet_length=0;
    buttons=0;
    event_head=event_count=0;
    enabled=1;
    console_fb_mouse_move(x,y);
    console_fb_mouse_enable();
    return 1;
}

void mouse64_byte(uint8_t value) {
    if (!enabled) return;
    if (!packet_length && !(value&0x08)) return; /* packet sync bit */
    packet[packet_length++]=value;
    if (packet_length<packet_size) return;
    packet_length=0;
    if (packet[0]&0xc0) return; /* discard overflowed deltas */
    int dx=(packet[0]&0x10)?(int)packet[1]-256:(int)packet[1];
    int dy=(packet[0]&0x20)?(int)packet[2]-256:(int)packet[2];
    int old_x=x,old_y=y;
    unsigned old_buttons=buttons;
    int wheel=0;
    if (packet_size==4) {
        unsigned nibble=packet[3]&0x0f;
        wheel=(nibble&8)?(int)nibble-16:(int)nibble;
    }
    buttons=packet[0]&7;
    if (device_id==4) buttons|=((packet[3]&0x30)>>1);
    x+=dx; y-=dy;
    if (x<0) x=0; else if ((unsigned)x>=console_fb_width()) x=(int)console_fb_width()-1;
    if (y<0) y=0; else if ((unsigned)y>=console_fb_height()) y=(int)console_fb_height()-1;
    console_fb_mouse_move(x,y);
    unsigned changed=old_buttons^buttons;
    unsigned kind=(x!=old_x || y!=old_y)?USER_INPUT_MOUSE_MOVE:0;
    if (changed) kind|=USER_INPUT_MOUSE_BUTTON;
    if (wheel) kind|=USER_INPUT_MOUSE_WHEEL;
    if (kind) queue_event(kind,changed,wheel);
}

int mouse64_pop(MouseEvent64 *event) {
    if (!enabled || !event || !event_count) return 0;
    *event=event_queue[event_head];
    event_head=(event_head+1)%(sizeof(event_queue)/sizeof(event_queue[0]));
    --event_count;
    return 1;
}
void mouse64_flush(void) { event_head=event_count=0; }
int mouse64_wheel_enabled(void) { return enabled && packet_size==4; }
