#include <pollikos/window.h>
#include <pollikos/process.h>
#include <pollikos/session.h>
#include <pollikos/system.h>
#include <pollikos/devices.h>
#include <pollikos/time.h>
#include <stdio.h>
#include <string.h>
#include "window_ui.h"
static PollikCanvas canvas;
static int confirm_reset;
static char status[100]="Ready";
static void button(int x,int y,int w,const char *text,uint32_t color) {
    pollik_ui_fill(&canvas,x,y,w,36,color);pollik_ui_text(&canvas,x+12,y+10,text,0xffffff);
}
static void draw(void) {
    pollik_ui_fill(&canvas,0,0,720,480,0x111725);
    pollik_ui_fill(&canvas,0,0,720,52,0x202939);pollik_ui_text(&canvas,24,18,"Settings / System",0xf0f3fa);
    PollikSystemInfo info;
    if(pollikos_system_info(&info)==0) {
        char text[128];snprintf(text,sizeof(text),"PollikOS x86_64 | user: %s | %s",info.username,info.rights&USER_CAP_ADMIN?"Administrator":"User");pollik_ui_text(&canvas,24,82,text,0xc8d4e8);
        snprintf(text,sizeof(text),"RAM: %lu MiB | PollikFS free: %lu KiB | %s",(unsigned long)(info.ram_bytes/1048576),(unsigned long)info.fs_free_blocks*info.fs_block_size/1024,info.fs_readonly?"read-only":"read-write");pollik_ui_text(&canvas,24,112,text,0xa5b2c9);
    }
    pollik_ui_text(&canvas,24,166,"Factory reset",0xf1f3fa);
    pollik_ui_text(&canvas,24,204,"Removes your account, /home files, /tmp and preferences.",0xc8d4e8);
    pollik_ui_text(&canvas,24,230,"PollikOS programs and libraries stay installed.",0xa5b2c9);
    pollik_ui_text(&canvas,24,256,"The system will restart and show first-run account setup.",0xa5b2c9);
    button(24,300,220,"Reset system...",0xb84150);
    if(confirm_reset) {
        pollik_ui_fill(&canvas,18,152,684,254,0x253047);
        pollik_ui_text(&canvas,36,176,"Factory reset - personal data will be deleted",0xffffff);
        pollik_ui_text(&canvas,36,216,"Files and preferences in /home and /tmp cannot be undone.",0xf0bdc1);
        pollik_ui_text(&canvas,36,246,"Your account and password record will be removed.",0xc8d4e8);
        pollik_ui_text(&canvas,36,276,"Confirm, then enter the administrator password.",0xc8d4e8);
        button(36,336,140,"Cancel",0x45556c);button(196,336,230,"Erase and restart",0xb84150);
    }
    pollik_ui_text(&canvas,24,442,status,0x99abc4);pollikos_window_present();
}
static int start_reset(void) {
    long rights=pollikos_session_control(USER_SESSION_RIGHTS);
    if(rights<0)return 0;
    if(!(rights&USER_CAP_ADMIN)) {
        if(pollikos_session_elevate("/bin/settings.pol")<0)return 0;
        const char *args[]={"settings","--factory-reset",NULL};
        if(pollikos_spawn_rights("/bin/settings.pol",args,NULL,USER_CAP_ADMIN_ALL)<0)return 0;
        return 1;
    }
    return pollikos_session_control(USER_SESSION_FACTORY_RESET)==0;
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--factory-reset")) {
        long rights=pollikos_session_control(USER_SESSION_RIGHTS);
        if(rights<0 || !(rights&USER_CAP_ADMIN)){puts("[settings] reset requires authenticated administrator rights");return 1;}
        puts("[settings] confirmed factory reset requested");
        if(!start_reset()){puts("[settings] reset request refused; data preserved");return 1;}
        for(;;)pollikos_sleep_ms(1000);
    }
    long mapped=pollikos_window_create(720,480,"Settings");if(mapped<0)return 1;
    canvas=(PollikCanvas){(uint32_t *)(uintptr_t)mapped,720,480};draw();puts("[settings] ready: factory reset requires confirmation and administrator password");
    int running=1;
    while(running) {
        pollikos_input_event_t event;
        if(pollikos_input_read(&event)==(long)sizeof(event)) {
            if(event.kind&POLLIKOS_INPUT_WINDOW_CLOSE)running=0;
            if((event.kind&POLLIKOS_INPUT_KEY_DOWN) && event.key==27){if(confirm_reset)confirm_reset=0;else running=0;draw();}
            if((event.kind&POLLIKOS_INPUT_MOUSE_BUTTON) && (event.changed&POLLIKOS_MOUSE_LEFT) && (event.buttons&POLLIKOS_MOUSE_LEFT)) {
                pollikos_window_info_t window;if(pollikos_window_info(&window)<0)continue;
                int x=event.x-window.content_x,y=event.y-window.content_y;
                if(!confirm_reset && x>=24 && x<244 && y>=300 && y<336){confirm_reset=1;draw();}
                else if(confirm_reset && y>=336 && y<372 && x>=36 && x<176){confirm_reset=0;draw();}
                else if(confirm_reset && y>=336 && y<372 && x>=196 && x<426) {
                    if(start_reset())running=0;else{strcpy(status,"Reset cancelled or refused; your data is preserved.");confirm_reset=0;draw();}
                }
            }
        }
        pollikos_sleep_ms(10);
    }
    pollikos_window_destroy();return 0;
}
