#include <pollikos/window.h>
#include <pollikos/process.h>
#include <pollikos/time.h>
#include <pollikos/fs.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "window_ui.h"

#define ENTRY_LIMIT 96
#define SIDE_WIDTH 148
#define ROW_HEIGHT 40
typedef struct { char name[USER_DIRENT_NAME_CAPACITY]; uint32_t type; } FileEntry;
static FileEntry entries[ENTRY_LIMIT];
static unsigned entry_count, selected, top_row;
static int applications_only=1;
static char current_path[USER_PATH_MAX];
static char status_text[96]="Double click or press Enter to open";
static uint64_t last_click_tick;
static int last_click_row=-1;

static int is_pol(const char *name) {
    size_t length=strlen(name);
    return length>=4 && strcmp(name+length-4,".pol")==0;
}
static void load_entries(void) {
    entry_count=selected=top_row=0;
    if (!getcwd(current_path,sizeof(current_path))) strcpy(current_path,"/");
    DIR *directory=opendir(current_path);
    if (!directory) { snprintf(status_text,sizeof(status_text),"Cannot open %s",current_path); return; }
    struct dirent *item;
    while ((item=readdir(directory))!=0) {
        if (!strcmp(item->d_name,".") || !strcmp(item->d_name,"..")) continue;
        int folder=item->d_type==POLLIKOS_TYPE_DIRECTORY;
        if (applications_only && !folder && !is_pol(item->d_name)) continue;
        if (entry_count==ENTRY_LIMIT) break;
        FileEntry *entry=&entries[entry_count++];
        snprintf(entry->name,sizeof(entry->name),"%s",item->d_name);
        entry->type=folder?POLLIKOS_TYPE_DIRECTORY:POLLIKOS_TYPE_REGULAR;
    }
    closedir(directory);
    for (unsigned i=1;i<entry_count;i++) {
        FileEntry value=entries[i];
        unsigned at=i;
        while (at && strcmp(entries[at-1].name,value.name)>0) {
            entries[at]=entries[at-1];
            --at;
        }
        entries[at]=value;
    }
    if (!entry_count) snprintf(status_text,sizeof(status_text),"No files in %s",current_path);
    else snprintf(status_text,sizeof(status_text),"%u items   Enter opens  .pol apps",entry_count);
}

static void launch_selected(void) {
    if (selected>=entry_count) return;
    FileEntry *entry=&entries[selected];
    if (entry->type==POLLIKOS_TYPE_DIRECTORY) {
        if (chdir(entry->name)==0) load_entries();
        else snprintf(status_text,sizeof(status_text),"Cannot open folder: %s",entry->name);
        return;
    }
    if (!is_pol(entry->name)) {
        snprintf(status_text,sizeof(status_text),"Only .pol files are launchable");
        return;
    }
    char path[USER_PATH_MAX];
    int root=!strcmp(current_path,"/");
    snprintf(path,sizeof(path),root?"/%s":"%s/%s",root?entry->name:current_path,
             root?"":entry->name);
    const char *argv[]={path,0};
    long pid=pollikos_spawn(path,argv,0);
    if (pid<0) snprintf(status_text,sizeof(status_text),"Cannot launch %s (%ld)",entry->name,pid);
    else {
        snprintf(status_text,sizeof(status_text),"Opened %s  PID %ld",entry->name,pid);
        printf("[files] launched %s pid=%ld\n",path,pid);
    }
}

static void select_view(int apps) {
    applications_only=apps;
    if (chdir(apps?"/bin":"/home")!=0) chdir("/");
    load_entries();
}

static void draw(PollikCanvas *canvas) {
    int width=(int)canvas->width,height=(int)canvas->height;
    pollik_ui_fill(canvas,0,0,width,height,0x101521);
    pollik_ui_fill(canvas,0,0,SIDE_WIDTH,height,0x171d2c);
    pollik_ui_text(canvas,18,18,"PollikOS",0xf0f2f7);
    pollik_ui_text(canvas,18,51,"PLACES",0x808ba1);
    pollik_ui_fill(canvas,8,74,SIDE_WIDTH-16,35,applications_only?0x202b40:0x202b40);
    pollik_ui_text(canvas,28,84,"Home",applications_only?0xb6c0d2:0x8fc2ff);
    pollik_ui_fill(canvas,8,116,SIDE_WIDTH-16,35,applications_only?0x263a57:0x202b40);
    pollik_ui_text(canvas,28,126,"Applications",applications_only?0x9bc8ff:0xb6c0d2);
    pollik_ui_fill(canvas,SIDE_WIDTH,0,width-SIDE_WIDTH,58,0x151b29);
    pollik_ui_text(canvas,SIDE_WIDTH+22,12,applications_only?"Applications":"Home",0xf0f2f7);
    pollik_ui_text(canvas,SIDE_WIDTH+22,34,current_path,0x9ba7bb);
    pollik_ui_fill(canvas,SIDE_WIDTH,58,width-SIDE_WIDTH,1,0x2a3345);
    int row_y=70, footer=height-35;
    int visible=(footer-row_y)/ROW_HEIGHT;
    if (selected<top_row) top_row=(unsigned)selected;
    if (selected>=top_row+(unsigned)visible) top_row=(unsigned)selected-(unsigned)visible+1;
    for (int row=0;row<visible;row++) {
        unsigned index=top_row+(unsigned)row;
        if (index>=entry_count) break;
        int y=row_y+row*ROW_HEIGHT;
        if (index==selected) pollik_ui_fill(canvas,SIDE_WIDTH+9,y,width-SIDE_WIDTH-18,ROW_HEIGHT-2,0x25334a);
        pollik_ui_icon(canvas,SIDE_WIDTH+22,y+6,0x76c7ff,entries[index].type==POLLIKOS_TYPE_DIRECTORY);
        pollik_ui_text(canvas,SIDE_WIDTH+62,y+11,entries[index].name,0xdce2ed);
        if (entries[index].type==POLLIKOS_TYPE_DIRECTORY)
            pollik_ui_text(canvas,width-76,y+11,"Folder",0x8490a5);
        else if (is_pol(entries[index].name))
            pollik_ui_text(canvas,width-76,y+11,"POL",0x82d2bb);
    }
    pollik_ui_fill(canvas,SIDE_WIDTH,footer,width-SIDE_WIDTH,height-footer,0x171e2b);
    pollik_ui_text(canvas,SIDE_WIDTH+18,footer+10,status_text,0x9eabc0);
}

int main(void) {
    unsigned width=650,height=400;
    int64_t mapped=pollikos_window_create(width,height,"Files");
    if (mapped<0) { width=560;height=300; mapped=pollikos_window_create(width,height,"Files"); }
    if (mapped<0) { puts("[files] cannot create window"); return 1; }
    PollikCanvas canvas={(uint32_t *)(uintptr_t)mapped,width,height};
    if (chdir("/bin")!=0) chdir("/");
    load_entries();
    draw(&canvas);
    if (pollikos_window_present()<0) { pollikos_window_destroy(); return 2; }
    puts("[files] ready: enter opens a selected .pol app");
    int running=1;
    while (running) {
        int child_status;
        while (waitpid(-1,&child_status,WNOHANG)>0) {}
        pollikos_input_event_t event;
        if (pollikos_input_read(&event)==(int64_t)sizeof(event)) {
            int changed=0;
            if (event.kind&POLLIKOS_INPUT_WINDOW_CLOSE) running=0;
            if (event.kind&POLLIKOS_INPUT_KEY_DOWN) {
                if (event.key==POLLIKOS_KEY_UP && selected) { --selected; changed=1; }
                else if (event.key==POLLIKOS_KEY_DOWN && selected+1<entry_count) { ++selected; changed=1; }
                else if (event.key==13) { launch_selected(); changed=1; }
                else if (event.key==8) { if (chdir("..") == 0) load_entries(); changed=1; }
                else if (event.key==27) running=0;
            }
            if ((event.kind&POLLIKOS_INPUT_MOUSE_WHEEL) && event.wheel) {
                if (event.wheel>0 && selected) --selected;
                else if (event.wheel<0 && selected+1<entry_count) ++selected;
                changed=1;
            }
            if ((event.kind&POLLIKOS_INPUT_MOUSE_BUTTON) &&
                (event.changed&POLLIKOS_MOUSE_LEFT) && (event.buttons&POLLIKOS_MOUSE_LEFT)) {
                pollikos_window_info_t info;
                if (pollikos_window_info(&info)==0) {
                    int x=event.x-info.content_x, y=event.y-info.content_y;
                    if (x<SIDE_WIDTH && y>=74 && y<109) { select_view(0); changed=1; }
                    else if (x<SIDE_WIDTH && y>=116 && y<151) { select_view(1); changed=1; }
                    else if (x>=SIDE_WIDTH && y>=70 && y<(int)height-35) {
                        int visible=(int)(height-35-70)/ROW_HEIGHT;
                        int row=(y-70)/ROW_HEIGHT;
                        unsigned index=top_row+(unsigned)row;
                        if (row>=0 && row<visible && index<entry_count) {
                            long now=pollikos_monotonic_ms();
                            if (last_click_row==(int)index && now-(long)last_click_tick<450) {
                                selected=index; launch_selected();
                            } else selected=index;
                            last_click_row=(int)index; last_click_tick=(uint64_t)now;
                            changed=1;
                        }
                    }
                }
            }
            if (changed) draw(&canvas);
        }
        (void)pollikos_sleep_ms(10);
    }
    puts("[files] closing");
    return pollikos_window_destroy()<0 ? 3 : 0;
}
