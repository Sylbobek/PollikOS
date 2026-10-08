#include <pollikos/window.h>
#include <string.h>
#include <pollikos/process.h>
#include <pollikos/time.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdint.h>
#include "window_ui.h"
#include "icon_assets.h"
static int launch(const char *path);
#include "control_panel.h"

static int launch(const char *path) {
    const char *argv[]={path,0};
    unsigned rights=USER_CAP_DEFAULT;
    if(!strcmp(path,"/bin/settings.pol")) rights|=USER_CAP_DEVICE;
    long pid=pollikos_spawn_rights(path,argv,0,rights);
    if (pid<0) {
        puts("[desktop] application launch failed");
        return 0;
    }
    printf("[desktop] launched %s pid=%ld\n",path,pid);
    return 1;
}

static void draw(PollikCanvas *canvas,int files_x,int terminal_x,int demo_x,int browser_x,int notes_x,int calc_x,int dock_y) {
    pollik_ui_fill(canvas,0,0,(int)canvas->width,(int)canvas->height,0x111725);
    for (unsigned y=0;y<canvas->height/2;y+=3)
        pollik_ui_fill(canvas,0,(int)y,(int)canvas->width,1,0x161e30);
    pollik_ui_fill(canvas,0,0,(int)canvas->width,38,0x161c2a);
    pollik_ui_text(canvas,22,10,"PollikOS v0.0.001",0xf0f2f7);
    pollik_ui_text(canvas,(int)canvas->width/2-35,10,"Pollik OS",0xf0f2f7);
    pollik_ui_text(canvas,(int)canvas->width-150,10,"Desktop",0xa9b2c4);
    pollik_ui_text(canvas,48,104,"Your applications",0xf0f2f7);
    pollik_ui_text(canvas,48,131,"Open Files to browse .pol apps",0xa9b2c4);
    pollik_ui_text(canvas,48,154,"Use F for Files, T for Terminal, B for Browser, C for Calculator",0x8d9bb1);
    pollik_ui_fill(canvas,48,196,4,72,0x64a9fa);
    pollik_ui_text(canvas,64,212,"Applications open in separate windows",0xb4bdcf);
    pollik_ui_fill(canvas,files_x-14,dock_y-5,390,65,0x252d40);
    pollik_assets_icon(canvas,1,files_x,dock_y,42);
    pollik_ui_text(canvas,files_x-1,dock_y+46,"Files",0xe8eaf1);
    pollik_ui_fill(canvas,files_x+52,dock_y+5,1,34,0x66748c);
    pollik_assets_icon(canvas,2,terminal_x,dock_y,42);
    pollik_ui_text(canvas,terminal_x-10,dock_y+46,"Terminal",0xe8eaf1);
    pollik_assets_icon(canvas,0,demo_x,dock_y,42);
    pollik_ui_text(canvas,demo_x-7,dock_y+46,"Demo",0xe8eaf1);
    pollik_assets_icon(canvas,5,browser_x,dock_y,42);
    pollik_ui_text(canvas,browser_x-7,dock_y+46,"Browser",0xe8eaf1);
    pollik_assets_icon(canvas,3,notes_x,dock_y,42);
    pollik_ui_text(canvas,notes_x-2,dock_y+46,"Notes",0xe8eaf1);
    pollik_assets_icon(canvas,7,calc_x,dock_y,42);
    pollik_ui_text(canvas,calc_x-1,dock_y+46,"Calc",0xe8eaf1);
    pollik_ui_text(canvas,16,(int)canvas->height-18,"PollikOS .pol desktop",0x828da2);
    panel_draw(canvas);
}

int main(void) {
    unsigned width=940,height=650;
    int64_t mapped=pollikos_window_create(width,height,"PollikOS Desktop");
    if (mapped<0) {
        width=720;height=500;
        mapped=pollikos_window_create(width,height,"PollikOS Desktop");
    }
    if (mapped<0) {
        width=560;height=300;
        mapped=pollikos_window_create(width,height,"PollikOS Desktop");
    }
    if (mapped<0) { puts("[desktop] cannot create desktop window"); return 1; }
    PollikCanvas canvas={(uint32_t *)(uintptr_t)mapped,width,height};
    pollik_assets_init();
    /* Preserve existing shortcut positions when appending Calculator. */
    int files_x=(int)width/2-118, terminal_x=files_x+64, demo_x=terminal_x+64;
    int browser_x=demo_x+64, notes_x=browser_x+64, calc_x=notes_x+64;
    int dock_y=(int)height-72;
    draw(&canvas,files_x,terminal_x,demo_x,browser_x,notes_x,calc_x,dock_y);
    if (pollikos_window_present()<0) { pollik_assets_destroy();pollikos_window_destroy(); return 2; }
    puts("[desktop] ready: Files is fixed left of the Dock separator");
    (void)launch("/bin/terminal.pol");
    int running=1;
    while (running) {
        int child_status;
        while (waitpid(-1,&child_status,WNOHANG)>0) {}
        pollikos_input_event_t event;
        if (pollikos_input_read(&event)==(int64_t)sizeof(event)) {
            if (event.kind&POLLIKOS_INPUT_WINDOW_CLOSE) running=0;
            if (event.kind&POLLIKOS_INPUT_KEY_DOWN) {
                if(panel_open||panel_slide){
                    panel_key(event.key,event.modifiers);
                    draw(&canvas,files_x,terminal_x,demo_x,browser_x,notes_x,calc_x,dock_y);
                    (void)pollikos_window_present();
                }
                else if (event.key=='f' || event.key=='F' || event.key==13) (void)launch("/bin/files.pol");
                else if (event.key=='t' || event.key=='T') (void)launch("/bin/terminal.pol");
                else if (event.key=='d' || event.key=='D') (void)launch("/bin/windowdemo.pol");
                else if (event.key=='b' || event.key=='B') (void)launch("/bin/browser.pol");
                else if (event.key=='n' || event.key=='N') (void)launch("/bin/notes.pol");
                else if (event.key=='c' || event.key=='C') (void)launch("/bin/calculator.pol");
                else if (event.key==27) running=0;
            }
            if(event.kind&POLLIKOS_INPUT_MOUSE_MOVE){pollikos_window_info_t info;
                if(pollikos_window_info(&info)==0&&panel_hover_at(&canvas,event.x-info.content_x,event.y-info.content_y)){
                    draw(&canvas,files_x,terminal_x,demo_x,browser_x,notes_x,calc_x,dock_y);(void)pollikos_window_present();}}
            if((event.kind&(POLLIKOS_INPUT_MOUSE_MOVE|POLLIKOS_INPUT_MOUSE_BUTTON))&&panel_capture){
                pollikos_window_info_t info;
                if(pollikos_window_info(&info)==0)panel_drag(&canvas,event.x-info.content_x,event.buttons&POLLIKOS_MOUSE_LEFT);
                draw(&canvas,files_x,terminal_x,demo_x,browser_x,notes_x,calc_x,dock_y);
                (void)pollikos_window_present();
            }
            if ((event.kind&POLLIKOS_INPUT_MOUSE_BUTTON) &&
                (event.changed&POLLIKOS_MOUSE_LEFT) && (event.buttons&POLLIKOS_MOUSE_LEFT)) {
                pollikos_window_info_t info;
                if (pollikos_window_info(&info)==0) {
                    int x=event.x-info.content_x, y=event.y-info.content_y;
                    if (panel_pointer(&canvas,x,y,1)) {
                        draw(&canvas,files_x,terminal_x,demo_x,browser_x,notes_x,calc_x,dock_y);
                        (void)pollikos_window_present();
                    } else if (y>=dock_y-5 && y<dock_y+62) {
                        if (x>=files_x-14 && x<files_x+36) (void)launch("/bin/files.pol");
                        else if (x>=terminal_x-4 && x<terminal_x+44) (void)launch("/bin/terminal.pol");
                        else if (x>=demo_x && x<demo_x+48) (void)launch("/bin/windowdemo.pol");
                        else if (x>=browser_x && x<browser_x+48) (void)launch("/bin/browser.pol");
                        else if (x>=notes_x && x<notes_x+48) (void)launch("/bin/notes.pol");
                        else if (x>=calc_x && x<calc_x+48) (void)launch("/bin/calculator.pol");
                    }
                }
            }
        }
        if(panel_tick()){
            draw(&canvas,files_x,terminal_x,demo_x,browser_x,notes_x,calc_x,dock_y);
            (void)pollikos_window_present();
        }
        (void)pollikos_sleep_ms(10);
    }
    puts("[desktop] closing");
    pollik_assets_destroy();return pollikos_window_destroy()<0 ? 3 : 0;
}
