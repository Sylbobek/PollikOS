#include <pollikos/window.h>
#include <pollikos/fs.h>
#include <pollikos/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "window_ui.h"

#define NOTE_CAPACITY 8192
#define NOTE_ROWS (NOTE_CAPACITY + 1)
#define LINE_HEIGHT 24

static char note[NOTE_CAPACITY];
static uint16_t row_start[NOTE_ROWS], row_end[NOTE_ROWS];
static unsigned note_length, cursor, row_count, first_row;
static int dirty;
static size_t selection_anchor=(size_t)-1;
static char status[96]="Ready";
static PollikCanvas canvas;

static int glyph_width(unsigned char c) {
    if (c<32 || c>=127) return 7;
    return font_glyphs[0][c-32].advance+2;
}
static int text_width(const char *s) {
    int width=0;
    while (*s) width+=glyph_width((unsigned char)*s++);
    return width;
}
static void make_rows(void) {
    row_count=0;
    unsigned at=0;
    int max_width=(int)canvas.width-126;
    if (max_width<120) max_width=120;
    while (at<note_length && row_count<NOTE_ROWS-1) {
        unsigned start=at;
        int width=0;
        while (at<note_length && note[at]!='\n') {
            int next=glyph_width((unsigned char)note[at]);
            if (width && width+next>max_width) break;
            width+=next;
            ++at;
        }
        row_start[row_count]=(uint16_t)start;
        row_end[row_count]=(uint16_t)at;
        ++row_count;
        if (at<note_length && note[at]=='\n') ++at;
    }
    if (!row_count || (note_length && note[note_length-1]=='\n')) {
        row_start[row_count]=(uint16_t)note_length;
        row_end[row_count]=(uint16_t)note_length;
        ++row_count;
    }
}
static unsigned cursor_row(void) {
    for (unsigned row=0;row<row_count;row++) {
        if (cursor>=row_start[row] && cursor<=row_end[row]) return row;
    }
    return row_count?row_count-1:0;
}
static void save_note(void) {
    int fd=open("/home/notes.txt",O_WRONLY|O_CREAT|O_TRUNC);
    if (fd<0) { snprintf(status,sizeof(status),"Save failed (%d)",fd); return; }
    unsigned done=0;
    while (done<note_length) {
        ssize_t count=write(fd,note+done,note_length-done);
        if (count<=0) { close(fd); strcpy(status,"Save failed while writing"); return; }
        done+=(unsigned)count;
    }
    if (close(fd)<0) { strcpy(status,"File written; close reported an error"); return; }
    dirty=0;
    snprintf(status,sizeof(status),"Saved %u chars",note_length);
    puts("[notes] saved /home/notes.txt");
}
static void load_note(void) {
    int fd=open("/home/notes.txt",O_RDONLY);
    if (fd<0) {
        static const char starter[]="Welcome to Notes\n\nWrite here, then press Ctrl+S to save.\n";
        memcpy(note,starter,sizeof(starter)-1);
        note_length=sizeof(starter)-1;
        dirty=1;
        strcpy(status,"New note | Ctrl+S saves to /home/notes.txt");
        return;
    }
    ssize_t count=read(fd,note,NOTE_CAPACITY-1);
    close(fd);
    note_length=count>0?(unsigned)count:0;
    note[note_length]=0;
    dirty=0;
    snprintf(status,sizeof(status),"Loaded %u chars",note_length);
    puts("[notes] loaded /home/notes.txt");
}
static void clear_selection(void) { selection_anchor=(size_t)-1; }
static void move_cursor(size_t next,int extend) {
    if (next>note_length) next=note_length;
    if (extend) {
        if (selection_anchor==(size_t)-1) selection_anchor=cursor;
    } else clear_selection();
    cursor=(unsigned)next;
}
static void erase_selection(void) {
    if (selection_anchor==(size_t)-1 || selection_anchor==cursor) { clear_selection(); return; }
    size_t lo=selection_anchor<cursor?selection_anchor:cursor;
    size_t hi=selection_anchor<cursor?cursor:selection_anchor;
    memmove(note+lo,note+hi,note_length-hi);
    note_length-=(unsigned)(hi-lo); cursor=(unsigned)lo;
    clear_selection(); dirty=1;
}
static void insert_char(char c) {
    if (selection_anchor!=(size_t)-1 && selection_anchor!=cursor) erase_selection();
    else clear_selection();
    if (note_length>=NOTE_CAPACITY-1) { strcpy(status,"Note is full (8191 characters)"); return; }
    memmove(note+cursor+1,note+cursor,note_length-cursor);
    note[cursor++]=c; ++note_length; note[note_length]=0; dirty=1;
}
static int shifted_char(uint32_t key) {
    if (key>='a'&&key<='z') return (int)(key-'a'+'A');
    switch(key) {
    case '1':return '!'; case '2':return '@'; case '3':return '#'; case '4':return '$';
    case '5':return '%'; case '6':return '^'; case '7':return '&'; case '8':return '*';
    case '9':return '('; case '0':return ')'; case '-':return '_'; case '=':return '+';
    case '[':return '{'; case ']':return '}'; case ';':return ':'; case ',':return '<';
    case '.':return '>'; case '/':return '?'; case '\\':return '|'; case '`':return '~';
    case '\'':return '"'; default:return (int)key;
    }
}
static void draw(void) {
    int w=(int)canvas.width,h=(int)canvas.height;
    pollik_ui_fill(&canvas,0,0,w,h,0x101521);
    pollik_ui_fill(&canvas,0,0,w,88,0x171d2b);
    pollik_ui_fill(&canvas,0,87,w,1,0x2b3549);
    pollik_ui_fill(&canvas,24,17,34,42,0x26334a);
    pollik_ui_fill(&canvas,32,24,18,27,0x5277ad);
    pollik_ui_fill(&canvas,36,30,10,2,0xffffff);
    pollik_ui_fill(&canvas,36,36,10,2,0xffffff);
    pollik_ui_fill(&canvas,36,42,7,2,0xffffff);
    pollik_ui_text(&canvas,70,17,"Notes",0xe2e8f0);
    pollik_ui_text(&canvas,70,43,"/home/notes.txt",0x94a3b8);
    pollik_ui_fill(&canvas,w-117,22,93,35,dirty?0x315a91:0x252d40);
    pollik_ui_text(&canvas,w-94,33,dirty?"UNSAVED":"SAVED",dirty?0xffffff:0x94a3b8);
    pollik_ui_fill(&canvas,0,88,w,38,0x151b29);
    pollik_ui_text(&canvas,28,101,"Ctrl+S  Save",0x82c5ff);
    pollik_ui_text(&canvas,153,101,"Ctrl+A  Select all",0x94a3b8);
    pollik_ui_text(&canvas,w-213,101,status,0x94a3b8);

    int top=139, bottom=h-42, visible=(bottom-top)/LINE_HEIGHT;
    unsigned caret_row=cursor_row();
    if (caret_row<first_row) first_row=caret_row;
    if (caret_row>=first_row+(unsigned)visible) first_row=caret_row-(unsigned)visible+1;
    pollik_ui_fill(&canvas,22,top-8,w-44,bottom-top+17,0x151b28);
    pollik_ui_fill(&canvas,82,top-8,1,bottom-top+17,0x2b3549);
    size_t sel_lo=selection_anchor<cursor?selection_anchor:cursor;
    size_t sel_hi=selection_anchor<cursor?cursor:selection_anchor;
    for (int screen=0;screen<visible;screen++) {
        unsigned row=first_row+(unsigned)screen;
        if (row>=row_count) break;
        int y=top+screen*LINE_HEIGHT;
        char number[12], line[256];
        snprintf(number,sizeof(number),"%u",row+1);
        pollik_ui_text(&canvas,38,y,number,0x77849a);
        unsigned start=row_start[row], end=row_end[row];
        unsigned count=end-start;
        if (count>=sizeof(line)) count=sizeof(line)-1;
        memcpy(line,note+start,count); line[count]=0;
        if (selection_anchor!=(size_t)-1 && sel_hi>sel_lo) {
            size_t a=sel_lo>start?sel_lo:start, b=sel_hi<end?sel_hi:end;
            if (b>a) {
                char before[256], selected[256];
                size_t n=a-start; if(n>sizeof(before)-1)n=sizeof(before)-1;
                memcpy(before,line,n); before[n]=0;
                size_t sn=b-a; if(sn>sizeof(selected)-1)sn=sizeof(selected)-1;
                memcpy(selected,note+a,sn); selected[sn]=0;
                pollik_ui_fill(&canvas,96+text_width(before),y-2,text_width(selected),LINE_HEIGHT-1,0x33445f);
            }
        }
        pollik_ui_text(&canvas,96,y,line,0xdce2ed);
        if (row==caret_row) {
            unsigned caret=cursor<start?start:cursor;
            if (caret>end) caret=end;
            char prefix[256]; size_t n=caret-start; if(n>sizeof(prefix)-1)n=sizeof(prefix)-1;
            memcpy(prefix,line,n); prefix[n]=0;
            pollik_ui_fill(&canvas,96+text_width(prefix),y-1,2,19,0x315a91);
        }
    }
    pollik_ui_fill(&canvas,0,h-31,w,31,0x171e2b);
    pollik_ui_fill(&canvas,0,h-32,w,1,0x2b3549);
    unsigned line_no=1,column=1;
    for (unsigned i=0;i<cursor;i++) { if(note[i]=='\n'){++line_no;column=1;}else ++column; }
    unsigned words=0; int in_word=0;
    for (unsigned i=0;i<note_length;i++) { if(note[i]==' '||note[i]=='\n'||note[i]=='\t')in_word=0;else if(!in_word){++words;in_word=1;} }
    char info[100]; snprintf(info,sizeof(info),"Ln %u, Col %u    %u words    %u characters",line_no,column,words,note_length);
    pollik_ui_text(&canvas,28,h-22,info,0x94a3b8);
    pollik_ui_text(&canvas,w-151,h-22,"PollikOS Notes",0x77849a);
}
int main(void) {
    unsigned width=760,height=560;
    int64_t mapped=pollikos_window_create(width,height,"Notes");
    if(mapped<0){width=620;height=440;mapped=pollikos_window_create(width,height,"Notes");}
    if(mapped<0){puts("[notes] cannot create window");return 1;}
    canvas=(PollikCanvas){(uint32_t *)(uintptr_t)mapped,width,height};
    load_note(); make_rows(); draw();
    if(pollikos_window_present()<0){pollikos_window_destroy();return 2;}
    puts("[notes] ready: Ctrl+S saves /home/notes.txt");
    int running=1;
    while(running) {
        pollikos_input_event_t e;
        if(pollikos_input_read(&e)==(int64_t)sizeof(e)) {
            int changed=0;
            int extend=(e.modifiers&POLLIKOS_INPUT_MOD_SHIFT)!=0;
            if(e.kind&POLLIKOS_INPUT_WINDOW_CLOSE) { if(dirty)save_note(); running=0; }
            if((e.kind&POLLIKOS_INPUT_KEY_DOWN) && (e.modifiers&POLLIKOS_INPUT_MOD_CONTROL)) {
                if(e.key=='s'||e.key=='S') { save_note(); changed=1; }
                else if(e.key=='a'||e.key=='A') { selection_anchor=0;cursor=note_length;changed=1; }
            } else if(e.kind&POLLIKOS_INPUT_KEY_DOWN) {
                if(e.key==13) { insert_char('\n'); changed=1; }
                else if(e.key==8) {
                    if(selection_anchor!=(size_t)-1&&selection_anchor!=cursor)erase_selection();
                    else if(cursor){clear_selection();memmove(note+cursor-1,note+cursor,note_length-cursor);--cursor;--note_length;note[note_length]=0;dirty=1;}
                    changed=1;
                } else if(e.key==POLLIKOS_KEY_DELETE) {
                    if(selection_anchor!=(size_t)-1&&selection_anchor!=cursor)erase_selection();
                    else if(cursor<note_length){clear_selection();memmove(note+cursor,note+cursor+1,note_length-cursor-1);--note_length;note[note_length]=0;dirty=1;}
                    changed=1;
                } else if(e.key==POLLIKOS_KEY_LEFT) { if(cursor)move_cursor(cursor-1,extend);changed=1; }
                else if(e.key==POLLIKOS_KEY_RIGHT) { if(cursor<note_length)move_cursor(cursor+1,extend);changed=1; }
                else if(e.key==POLLIKOS_KEY_HOME||e.key==POLLIKOS_KEY_END) {
                    unsigned row=cursor_row(); size_t target=e.key==POLLIKOS_KEY_HOME?row_start[row]:row_end[row];
                    move_cursor(target,extend);changed=1;
                } else if(e.key==POLLIKOS_KEY_UP||e.key==POLLIKOS_KEY_DOWN) {
                    unsigned row=cursor_row(), col=cursor-row_start[row];
                    if(e.key==POLLIKOS_KEY_UP&&row) --row;
                    else if(e.key==POLLIKOS_KEY_DOWN&&row+1<row_count) ++row;
                    unsigned max=row_end[row]-row_start[row]; if(col>max)col=max;
                    move_cursor(row_start[row]+col,extend);changed=1;
                } else if(e.key>=32&&e.key<127) {
                    int c=(e.modifiers&POLLIKOS_INPUT_MOD_SHIFT)?shifted_char(e.key):(int)e.key;
                    insert_char((char)c);changed=1;
                }
            } else if((e.kind&POLLIKOS_INPUT_MOUSE_WHEEL)&&e.wheel) {
                if(e.wheel>0&&first_row) --first_row;
                else if(e.wheel<0&&first_row+(unsigned)((height-181)/LINE_HEIGHT)<row_count) ++first_row;
                changed=1;
            }
            if(changed){make_rows();draw();(void)pollikos_window_present();}
        }
        (void)pollikos_sleep_ms(10);
    }
    puts("[notes] closing");
    return pollikos_window_destroy()<0?3:0;
}
