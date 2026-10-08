#include <pollikos/window.h>
#include <pollikos/process.h>
#include <pollikos/time.h>
#include <pollikos/fs.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "window_ui.h"

#define SIDE_WIDTH 148
#define ROW_HEIGHT 40
typedef struct { char name[USER_DIRENT_NAME_CAPACITY],label[USER_DIRENT_NAME_CAPACITY]; uint32_t type; } FileEntry;
static FileEntry *entries;
static unsigned entry_capacity;
static unsigned entry_count, selected, top_row;
static int applications_only=1;
static char current_path[USER_PATH_MAX];
static char status_text[96]="Double click or press Enter to open";
static uint64_t last_click_tick;
static int last_click_row=-1;
static char clipboard[USER_PATH_MAX],last_trash[USER_PATH_MAX];
static int clipboard_move;
static int name_prompt;
static char entered_name[USER_DIRENT_NAME_CAPACITY];
static unsigned entered_length;
typedef struct { uint32_t magic; char original[USER_PATH_MAX]; } TrashInfo;
#define TRASH_MAGIC 0x31485254u
static int ends_with(const char *name,const char *suffix) {
    size_t a=strlen(name),b=strlen(suffix);return a>=b && !strcmp(name+a-b,suffix);
}
static int joined_path(const char *directory,const char *name,char out[USER_PATH_MAX]) {
    int n=snprintf(out,USER_PATH_MAX,!strcmp(directory,"/")?"/%s":"%s/%s",
                   !strcmp(directory,"/")?name:directory,!strcmp(directory,"/")?"":name);
    if(n<0 || n>=USER_PATH_MAX){errno=ENAMETOOLONG;return -1;}return 0;
}
static int selected_path(char out[USER_PATH_MAX]) {
    if(selected>=entry_count){errno=ENOENT;return -1;}
    return joined_path(current_path,entries[selected].name,out);
}
static int trash_information(const char *item,TrashInfo *info,char metadata[USER_PATH_MAX]) {
    if(!ends_with(item,".item")){errno=EINVAL;return -1;}
    size_t n=strlen(item)-5;if(n>=USER_PATH_MAX){errno=ENAMETOOLONG;return -1;}
    memcpy(metadata,item,n);metadata[n]=0;
    int fd=open(metadata,O_RDONLY);if(fd<0)return -1;
    struct stat st;int valid=fstat(fd,&st)==0 && st.st_size==sizeof(*info);
    if(valid)valid=read(fd,info,sizeof(*info))==sizeof(*info);
    if(close(fd)<0)valid=0;
    if(!valid || info->magic!=TRASH_MAGIC || info->original[0]!='/' || info->original[USER_PATH_MAX-1]) {errno=EIO;return -1;}
    return 0;
}
static int trash_path(const char *source,char item[USER_PATH_MAX]) {
    if(mkdir("/home/Trash",0)<0 && errno!=EEXIST)return -1;
    struct stat st;if(stat("/home/Trash",&st)<0)return -1;
    if(!S_ISDIR(st.st_mode)){errno=ENOTDIR;return -1;}
    TrashInfo info={.magic=TRASH_MAGIC};
    if(strlen(source)>=sizeof(info.original)){errno=ENAMETOOLONG;return -1;}
    strcpy(info.original,source);
    char metadata[USER_PATH_MAX]="/home/Trash/t-XXXXXX";
    int fd=mkstemp(metadata);if(fd<0)return -1;
    int ok=write(fd,&info,sizeof(info))==sizeof(info);if(close(fd)<0)ok=0;
    if(!ok){unlink(metadata);errno=EIO;return -1;}
    snprintf(item,USER_PATH_MAX,"%s.item",metadata);
    /* Retain metadata on a reported rename error: commit may have reached
     * the redo log before an I/O error. Recovery can then still restore it. */
    return rename(source,item);
}
static int restore_path(const char *item) {
    TrashInfo info;char metadata[USER_PATH_MAX];
    if(trash_information(item,&info,metadata)<0)return -1;
    if(rename(item,info.original)<0)return -1; /* never overwrite an existing original */
    if(unlink(metadata)<0)printf("[files] restored; metadata cleanup failed (%d)\n",errno);
    return 0;
}
static int copy_file(const char *source,const char *destination) {
    int input=open(source,O_RDONLY);if(input<0){if(errno==EISDIR)errno=ENOTSUP;return -1;}
    struct stat original,target;
    if(fstat(input,&original)<0){close(input);return -1;}
    if(!S_ISREG(original.st_mode)){close(input);errno=ENOTSUP;return -1;}
    if(stat(destination,&target)==0){close(input);errno=EEXIST;return -1;}
    if(errno!=ENOENT){close(input);return -1;}
    char temporary[USER_PATH_MAX];int length=snprintf(temporary,sizeof(temporary),"%s.XXXXXX",destination);
    if(length<0 || (size_t)length>=sizeof(temporary)){close(input);errno=ENAMETOOLONG;return -1;}
    int output=mkstemp(temporary);if(output<0){close(input);return -1;}
    char block[4096];unsigned long copied=0;int error=0;
    while(!error) {
        ssize_t n=read(input,block,sizeof(block));
        if(n<0){error=errno;break;}if(!n)break;
        ssize_t done=0;while(done<n){ssize_t w=write(output,block+done,(size_t)(n-done));if(w<=0){error=errno?errno:EIO;break;}done+=w;}
        copied+=(unsigned long)done;
    }
    if(!error && (copied!=original.st_size || fstat(input,&target)<0 || target.st_size!=original.st_size))error=EIO;
    if(close(input)<0 && !error)error=errno;
    if(close(output)<0 && !error)error=errno;
    if(error){unlink(temporary);errno=error;return -1;}
    if(rename(temporary,destination)<0)return -1;
    return 0;
}
static void operation_error(void) {
    const char *message=errno==ENOSPC?"Disk full":errno==EACCES?"Access denied":
        errno==EEXIST?"Name already exists":errno==ENAMETOOLONG?"Path is too long":
        errno==ENOTSUP?"Copy files; use Cut for folders":"Operation failed; check source and destination";
    snprintf(status_text,sizeof(status_text),"%s",message);
    printf("[files] operation error %d\n",errno);
}

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
    int listing_error=0;
    while ((item=readdir(directory))!=0) {
        if (!strcmp(item->d_name,".") || !strcmp(item->d_name,"..")) continue;
        int folder=item->d_type==POLLIKOS_TYPE_DIRECTORY;
        if (applications_only && !folder && !is_pol(item->d_name)) continue;
        if(!strcmp(current_path,"/home/Trash") && !ends_with(item->d_name,".item"))continue;
        if(entry_count==entry_capacity) {
            unsigned capacity=entry_capacity?entry_capacity*2:32;
            if(capacity<entry_capacity || (size_t)capacity>SIZE_MAX/sizeof(*entries)){listing_error=1;break;}
            FileEntry *grown=realloc(entries,(size_t)capacity*sizeof(*entries));
            if(!grown){listing_error=1;break;}entries=grown;entry_capacity=capacity;
        }
        FileEntry *entry=&entries[entry_count++];
        snprintf(entry->name,sizeof(entry->name),"%s",item->d_name);
        strcpy(entry->label,entry->name);
        entry->type=folder?POLLIKOS_TYPE_DIRECTORY:POLLIKOS_TYPE_REGULAR;
        if(!strcmp(current_path,"/home/Trash")) {
            char path[USER_PATH_MAX],metadata[USER_PATH_MAX];TrashInfo info;
            if(joined_path(current_path,entry->name,path)==0 && trash_information(path,&info,metadata)==0) {
                const char *name=info.original;for(const char *p=name;*p;p++)if(*p=='/')name=p+1;
                snprintf(entry->label,sizeof(entry->label),"%s",name);
            }
        }
    }
    listing_error|=directory->error;if(closedir(directory)<0)listing_error=1;
    for (unsigned i=1;i<entry_count;i++) {
        FileEntry value=entries[i];
        unsigned at=i;
        while (at && strcmp(entries[at-1].label,value.label)>0) {
            entries[at]=entries[at-1];
            --at;
        }
        entries[at]=value;
    }
    if(listing_error)strcpy(status_text,"Listing incomplete; refresh to retry");
    else if (!entry_count) snprintf(status_text,sizeof(status_text),"No files in %s",current_path);
    else snprintf(status_text,sizeof(status_text),"%u items",entry_count);
}

static void launch_selected(void) {
    if (selected>=entry_count) return;
    FileEntry *entry=&entries[selected];
    if (entry->type==POLLIKOS_TYPE_DIRECTORY) {
        if (chdir(entry->name)==0) load_entries();
        else snprintf(status_text,sizeof(status_text),"Cannot open folder: %s",entry->name);
        return;
    }
    size_t name_length=strlen(entry->name);
    int audio_file=name_length>=4&&(!strcmp(entry->name+name_length-4,".wav")||!strcmp(entry->name+name_length-4,".mp3"));
    int video_file=name_length>=4&&!strcmp(entry->name+name_length-4,".mp4");
    int html_file=ends_with(entry->name,".html")||ends_with(entry->name,".htm");
    int text_file=ends_with(entry->name,".txt")||ends_with(entry->name,".c")||ends_with(entry->name,".h")||ends_with(entry->name,".md");
    if (!is_pol(entry->name)&&!audio_file&&!video_file&&!html_file&&!text_file) {
        snprintf(status_text,sizeof(status_text),"No application for this file type");
        return;
    }
    char path[USER_PATH_MAX];
    if(selected_path(path)<0){operation_error();return;}
    const char *application=video_file?"/bin/video.pol":audio_file?"/bin/media.pol":
        html_file?"/bin/browser.pol":text_file?"/bin/notes.pol":path;
    const char *argv[]={application,application==path?0:path,0};
    long pid=pollikos_spawn(application,argv,0);
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
static void finish_prompt(void) {
    if(!entered_length || strchr(entered_name,'/') || strchr(entered_name,'\\') ||
       !strcmp(entered_name,".") || !strcmp(entered_name,"..")) {
        strcpy(status_text,"Enter a single folder or file name");return;
    }
    int result=name_prompt==1?mkdir(entered_name,0):
        selected<entry_count?rename(entries[selected].name,entered_name):-1;
    if(result<0){operation_error();return;}
    name_prompt=0;load_entries();
}
static void paste_clipboard(void) {
    if(!clipboard[0]){strcpy(status_text,"Copy or cut a file first");return;}
    const char *name=clipboard;for(const char *p=name;*p;p++)if(*p=='/')name=p+1;
    char destination[USER_PATH_MAX];if(joined_path(current_path,name,destination)<0){operation_error();return;}
    int result=clipboard_move?rename(clipboard,destination):copy_file(clipboard,destination);
    if(result<0){operation_error();return;}
    if(clipboard_move)clipboard[0]=0;
    load_entries();strcpy(status_text,clipboard_move?"Moved":"Copied");
}
static void delete_selected(void) {
    if(!strcmp(current_path,"/home/Trash")){strcpy(status_text,"Ctrl+Z restores the selected item");return;}
    char source[USER_PATH_MAX],item[USER_PATH_MAX];
    if(selected_path(source)<0 || trash_path(source,item)<0){operation_error();return;}
    strcpy(last_trash,item);load_entries();strcpy(status_text,"Moved to Trash; Ctrl+Z restores");
}
static void restore_selected(void) {
    char item[USER_PATH_MAX];
    if(!strcmp(current_path,"/home/Trash")){if(selected_path(item)<0){operation_error();return;}}
    else {if(!last_trash[0]){strcpy(status_text,"Open Trash and select an item to restore");return;}strcpy(item,last_trash);}
    if(restore_path(item)<0){operation_error();return;}
    last_trash[0]=0;load_entries();strcpy(status_text,"Restored without overwriting existing files");
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
    pollik_ui_fill(canvas,8,158,SIDE_WIDTH-16,35,!strcmp(current_path,"/home/Trash")?0x263a57:0x202b40);
    pollik_ui_text(canvas,28,168,"Trash",!strcmp(current_path,"/home/Trash")?0x9bc8ff:0xb6c0d2);
    pollik_ui_fill(canvas,SIDE_WIDTH,0,width-SIDE_WIDTH,58,0x151b29);
    pollik_ui_text(canvas,SIDE_WIDTH+22,12,applications_only?"Applications":"Home",0xf0f2f7);
    pollik_ui_text(canvas,SIDE_WIDTH+22,34,current_path,0x9ba7bb);
    pollik_ui_fill(canvas,SIDE_WIDTH,58,width-SIDE_WIDTH,1,0x2a3345);
    int row_y=70, footer=height-66;
    int visible=(footer-row_y)/ROW_HEIGHT;
    if (selected<top_row) top_row=(unsigned)selected;
    if (selected>=top_row+(unsigned)visible) top_row=(unsigned)selected-(unsigned)visible+1;
    for (int row=0;row<visible;row++) {
        unsigned index=top_row+(unsigned)row;
        if (index>=entry_count) break;
        int y=row_y+row*ROW_HEIGHT;
        if (index==selected) pollik_ui_fill(canvas,SIDE_WIDTH+9,y,width-SIDE_WIDTH-18,ROW_HEIGHT-2,0x25334a);
        pollik_ui_icon(canvas,SIDE_WIDTH+22,y+6,0x76c7ff,entries[index].type==POLLIKOS_TYPE_DIRECTORY);
        pollik_ui_text(canvas,SIDE_WIDTH+62,y+11,entries[index].label,0xdce2ed);
        if (entries[index].type==POLLIKOS_TYPE_DIRECTORY)
            pollik_ui_text(canvas,width-76,y+11,"Folder",0x8490a5);
        else if (is_pol(entries[index].name))
            pollik_ui_text(canvas,width-76,y+11,"POL",0x82d2bb);
    }
    pollik_ui_fill(canvas,SIDE_WIDTH,footer,width-SIDE_WIDTH,height-footer,0x171e2b);
    if(name_prompt) {
        pollik_ui_text(canvas,SIDE_WIDTH+12,footer+7,name_prompt==1?"New folder:":"Rename to:",0x82c5ff);
        pollik_ui_text(canvas,SIDE_WIDTH+110,footer+7,entered_name,0xf0f2f7);
        pollik_ui_text(canvas,SIDE_WIDTH+12,footer+27,"Enter confirms; Esc cancels; Ctrl+A clears",0x9eabc0);
    } else {
        pollik_ui_text(canvas,SIDE_WIDTH+12,footer+7,"Ctrl+N Folder  Ctrl+R Rename  Del Trash",0x9eabc0);
        pollik_ui_text(canvas,SIDE_WIDTH+12,footer+27,"Ctrl+C/X/V Copy/Cut/Paste  Ctrl+Z Restore",0x9eabc0);
    }
    pollik_ui_text(canvas,SIDE_WIDTH+12,footer+47,status_text,0x9eabc0);
}

int main(int argc,char **argv) {
    unsigned width=650,height=400;
    int64_t mapped=pollikos_window_create(width,height,"Files");
    if (mapped<0) { width=560;height=300; mapped=pollikos_window_create(width,height,"Files"); }
    if (mapped<0) { puts("[files] cannot create window"); return 1; }
    PollikCanvas canvas={(uint32_t *)(uintptr_t)mapped,width,height};
    if(argc>1) {
        if(chdir(argv[1])<0){puts("[files] cannot open requested folder");pollikos_window_destroy();return 1;}
        applications_only=0;
    } else if (chdir("/bin")!=0) chdir("/");
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
                if(name_prompt) {
                    if(event.key==27)name_prompt=0;
                    else if(event.key==13)finish_prompt();
                    else if((event.modifiers&POLLIKOS_INPUT_MOD_CONTROL) && (event.key=='a'||event.key=='A'))entered_length=0;
                    else if(event.key==8 && entered_length)--entered_length;
                    else if(event.key>=32 && event.key<127 && entered_length<sizeof(entered_name)-1)entered_name[entered_length++]=(char)event.key;
                    entered_name[entered_length]=0;changed=1;
                } else if(event.modifiers&POLLIKOS_INPUT_MOD_CONTROL) {
                    uint32_t key=event.key;if(key>='A' && key<='Z')key+=32;
                    if(key=='n'){name_prompt=1;entered_length=0;entered_name[0]=0;}
                    else if(key=='r' && selected<entry_count){name_prompt=2;snprintf(entered_name,sizeof(entered_name),"%s",entries[selected].name);entered_length=(unsigned)strlen(entered_name);}
                    else if((key=='c'||key=='x') && selected<entry_count){if(selected_path(clipboard)<0)operation_error();else{clipboard_move=key=='x';strcpy(status_text,clipboard_move?"Cut; open destination and Ctrl+V":"Copied selection; Ctrl+V pastes");}}
                    else if(key=='v')paste_clipboard();
                    else if(key=='z')restore_selected();
                    else if(key=='l')load_entries();
                    changed=1;
                } else if(event.key==POLLIKOS_KEY_DELETE && selected<entry_count){delete_selected();changed=1;}
                else if (event.key==POLLIKOS_KEY_UP && selected) { --selected; changed=1; }
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
                (event.changed&POLLIKOS_MOUSE_LEFT) && (event.buttons&POLLIKOS_MOUSE_LEFT) && !name_prompt) {
                pollikos_window_info_t info;
                if (pollikos_window_info(&info)==0) {
                    int x=event.x-info.content_x, y=event.y-info.content_y;
                    if (x<SIDE_WIDTH && y>=74 && y<109) { select_view(0); changed=1; }
                    else if (x<SIDE_WIDTH && y>=116 && y<151) { select_view(1); changed=1; }
                    else if (x<SIDE_WIDTH && y>=158 && y<193) {
                        if((mkdir("/home/Trash",0)==0 || errno==EEXIST) && chdir("/home/Trash")==0){applications_only=0;load_entries();}
                        else operation_error();changed=1;
                    }
                    else if (x>=SIDE_WIDTH && y>=70 && y<(int)height-66) {
                        int visible=(int)(height-66-70)/ROW_HEIGHT;
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
            if (changed) { draw(&canvas);(void)pollikos_window_present(); }
        }
        (void)pollikos_sleep_ms(10);
    }
    puts("[files] closing");
    free(entries);
    return pollikos_window_destroy()<0 ? 3 : 0;
}
