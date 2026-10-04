#include <pollikos/window.h>
#include <pollikos/time.h>
#include <stdio.h>
#include <stdint.h>

int main(void) {
    const unsigned width=360, height=220;
    int64_t mapped=pollikos_window_create(width,height,"PollikOS App Window");
    if (mapped<0) { printf("[windowdemo] create failed: %ld\n",(long)mapped); return 1; }
    uint32_t *pixels=(uint32_t *)(uintptr_t)mapped;
    for (unsigned y=0;y<height;y++) for (unsigned x=0;x<width;x++) {
        unsigned r=24+(x*48)/width, g=32+(y*60)/height, b=62+(x*36)/width;
        pixels[y*width+x]=(r<<16)|(g<<8)|b;
    }
    if (pollikos_window_present()<0) { pollikos_window_destroy(); return 2; }
    puts("[windowdemo] ready: type, drag titlebar, wheel, or click close");
    for (;;) {
        pollikos_input_event_t event;
        int64_t result=pollikos_input_read(&event);
        if (result==(int64_t)sizeof(event)) {
            if (event.kind&(POLLIKOS_INPUT_MOUSE_BUTTON|POLLIKOS_INPUT_MOUSE_WHEEL|
                            POLLIKOS_INPUT_WINDOW_CLOSE))
                printf("[windowdemo] event=%u changed=%u buttons=%u wheel=%d at=%d,%d\n",
                       event.kind,event.changed,event.buttons,event.wheel,event.x,event.y);
            if (event.kind&(POLLIKOS_INPUT_KEY_DOWN|POLLIKOS_INPUT_KEY_UP))
                printf("[windowdemo] key=%u mods=%u down=%u\n",event.key,event.modifiers,
                       !!(event.kind&POLLIKOS_INPUT_KEY_DOWN));
            if ((event.kind&POLLIKOS_INPUT_KEY_DOWN) && event.key==27) {
                puts("[windowdemo] escape received");
                break;
            }
            if (event.kind&POLLIKOS_INPUT_WINDOW_CLOSE) {
                puts("[windowdemo] close event received");
                break;
            }
            if (event.kind&POLLIKOS_INPUT_MOUSE_WHEEL)
                printf("[windowdemo] wheel=%d at=%d,%d\n",event.wheel,event.x,event.y);
            if ((event.kind&POLLIKOS_INPUT_MOUSE_BUTTON) && event.changed)
                printf("[windowdemo] buttons=%u at=%d,%d\n",event.buttons,event.x,event.y);
        }
        (void)pollikos_sleep_ms(10);
    }
    return pollikos_window_destroy()<0 ? 3 : 0;
}
