#include <pollikos/window.h>
#include <pollikos/process.h>
#include <pollikos/time.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdint.h>
#include "window_ui.h"

static int launch(const char *path) {
    const char *argv[]={path,0};
    long pid=pollikos_spawn(path,argv,0);
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
    pollik_ui_text(canvas,22,10,"PollikOS x86-64",0xf0f2f7);
    pollik_ui_text(canvas,(int)canvas->width-150,10,"Desktop",0xa9b2c4);
    pollik_ui_text(canvas,48,104,"Your applications",0xf0f2f7);
    pollik_ui_text(canvas,48,131,"Open Files to browse .pol apps",0xa9b2c4);
    pollik_ui_text(canvas,48,154,"Use F for Files, T for Terminal, B for Browser, C for Calculator",0x8d9bb1);
    pollik_ui_fill(canvas,48,196,4,72,0x64a9fa);
    pollik_ui_text(canvas,64,212,"Applications open in separate windows",0xb4bdcf);
    pollik_ui_fill(canvas,files_x-14,dock_y-5,390,65,0x252d40);
    pollik_ui_fill(canvas,files_x,dock_y,42,42,0x35415b);
    pollik_ui_icon(canvas,files_x+8,dock_y+7,0x70c9ff,1);
    pollik_ui_text(canvas,files_x-1,dock_y+46,"Files",0xe8eaf1);
    pollik_ui_fill(canvas,files_x+52,dock_y+5,1,34,0x66748c);
    pollik_ui_fill(canvas,terminal_x+9,dock_y+7,25,24,0x4a5873);
    pollik_ui_fill(canvas,terminal_x+13,dock_y+11,17,2,0x82d2bb);
    pollik_ui_fill(canvas,terminal_x+13,dock_y+17,9,2,0xb6c0d2);
    pollik_ui_text(canvas,terminal_x-10,dock_y+46,"Terminal",0xe8eaf1);
    pollik_ui_fill(canvas,demo_x,dock_y,42,42,0x35415b);
    pollik_ui_icon(canvas,demo_x+8,dock_y+7,0x80d5bd,0);
    pollik_ui_text(canvas,demo_x-7,dock_y+46,"Demo",0xe8eaf1);
    pollik_ui_fill(canvas,browser_x,dock_y,42,42,0x35415b);
    pollik_ui_fill(canvas,browser_x+8,dock_y+9,26,22,0x78a9ed);
    pollik_ui_fill(canvas,browser_x+11,dock_y+12,20,2,0xdce9ff);
    pollik_ui_fill(canvas,browser_x+11,dock_y+17,13,2,0xdce9ff);
    pollik_ui_text(canvas,browser_x-7,dock_y+46,"Browser",0xe8eaf1);
    pollik_ui_fill(canvas,notes_x,dock_y,42,42,0x35415b);
    pollik_ui_fill(canvas,notes_x+11,dock_y+7,20,28,0xf2f4f8);
    pollik_ui_fill(canvas,notes_x+15,dock_y+14,12,2,0x6482ad);
    pollik_ui_fill(canvas,notes_x+15,dock_y+20,12,2,0x6482ad);
    pollik_ui_fill(canvas,notes_x+15,dock_y+26,9,2,0x6482ad);
    pollik_ui_text(canvas,notes_x-2,dock_y+46,"Notes",0xe8eaf1);
    pollik_ui_fill(canvas,calc_x,dock_y,42,42,0x7968d8);
    pollik_ui_text(canvas,calc_x+11,dock_y+10,"+",0xffffff);
    pollik_ui_text(canvas,calc_x-1,dock_y+46,"Calc",0xe8eaf1);
    pollik_ui_text(canvas,16,(int)canvas->height-18,"PollikOS .pol desktop",0x828da2);
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
    int files_x=(int)width/2-182, terminal_x=files_x+64, demo_x=terminal_x+64;
    int browser_x=demo_x+64, notes_x=browser_x+64, calc_x=notes_x+64;
    int dock_y=(int)height-72;
    draw(&canvas,files_x,terminal_x,demo_x,browser_x,notes_x,calc_x,dock_y);
    if (pollikos_window_present()<0) { pollikos_window_destroy(); return 2; }
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
                if (event.key=='f' || event.key=='F' || event.key==13) (void)launch("/bin/files.pol");
                else if (event.key=='t' || event.key=='T') (void)launch("/bin/terminal.pol");
                else if (event.key=='d' || event.key=='D') (void)launch("/bin/windowdemo.pol");
                else if (event.key=='b' || event.key=='B') (void)launch("/bin/browser.pol");
                else if (event.key=='n' || event.key=='N') (void)launch("/bin/notes.pol");
                else if (event.key=='c' || event.key=='C') (void)launch("/bin/calculator.pol");
                else if (event.key==27) running=0;
            }
            if ((event.kind&POLLIKOS_INPUT_MOUSE_BUTTON) &&
                (event.changed&POLLIKOS_MOUSE_LEFT) && (event.buttons&POLLIKOS_MOUSE_LEFT)) {
                pollikos_window_info_t info;
                if (pollikos_window_info(&info)==0) {
                    int x=event.x-info.content_x, y=event.y-info.content_y;
                    if (y>=dock_y-5 && y<dock_y+62) {
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
        (void)pollikos_sleep_ms(10);
    }
    puts("[desktop] closing");
    return pollikos_window_destroy()<0 ? 3 : 0;
}
