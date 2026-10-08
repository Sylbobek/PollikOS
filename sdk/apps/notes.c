#include <pollikos/window.h>
#include <pollikos/fs.h>
#include <pollikos/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
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
static int editable;
static char note_path[USER_PATH_MAX]="/home/notes.txt";
typedef struct { char text[NOTE_CAPACITY]; unsigned length,cursor; } NoteSnapshot;
static NoteSnapshot undo_history[8],redo_history[8];
static unsigned undo_count,redo_count;
static int saving_as;
static char save_path[USER_PATH_MAX];
static unsigned save_path_length;
static size_t selection_anchor=(size_t)-1;
static char status[96]="Ready";
static PollikCanvas canvas;

static int glyph_width(uint32_t c) {
    return c<32?7:pollik_ui_glyph(c)->advance+2;
}
static int text_width(const char *s) {
    return pollik_ui_text_width(s);
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
            size_t used;int next=glyph_width(pollik_utf8_next(note+at,note_length-at,&used));
            if (width && width+next>max_width) break;
            width+=next;
            at+=(unsigned)used;
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
static int write_note(const char *path,int replace) {
    if(!editable) { strcpy(status,"Read failed or file too large; saving disabled"); return 0; }
    char temporary[USER_PATH_MAX];
    int length=snprintf(temporary,sizeof(temporary),"%s.XXXXXX",path);
    if(length<0 || (size_t)length>=sizeof(temporary)) { strcpy(status,"Save path is too long"); return 0; }
    int fd=mkstemp(temporary);
    if (fd<0) { snprintf(status,sizeof(status),"Save failed (%d); original preserved",errno); return 0; }
    unsigned done=0;
    while (done<note_length) {
        ssize_t count=write(fd,note+done,note_length-done);
        if (count<=0) { close(fd); unlink(temporary); strcpy(status,"Write failed; original preserved"); return 0; }
        done+=(unsigned)count;
    }
    if (close(fd)<0) { unlink(temporary); strcpy(status,"Close failed; original preserved"); return 0; }
    if((replace?rename_replace(temporary,path):rename(temporary,path))<0) {
        if(errno==EEXIST){unlink(temporary);strcpy(status,"Name already exists; choose another path");return 0;}
        strcpy(status,"Replace reported an error; document remains in editor");return 0;
    }
    dirty=0;
    snprintf(status,sizeof(status),"Saved %u bytes",note_length);
    printf("[notes] saved %s\n",path);
    return 1;
}
static int save_note(void) { return write_note(note_path,1); }
static int save_as_path(const char *path) {
    if(!*path || strlen(path)>=sizeof(note_path)){strcpy(status,"Save path is empty or too long");return 0;}
    if(!strcmp(path,note_path))return save_note();
    if(!write_note(path,0))return 0;
    strcpy(note_path,path);return 1;
}
static void store_snapshot(NoteSnapshot *history,unsigned *count) {
    if(*count==8){memmove(history,history+1,7*sizeof(*history));--*count;}
    NoteSnapshot *snapshot=&history[(*count)++];
    memcpy(snapshot->text,note,note_length+1);snapshot->length=note_length;snapshot->cursor=cursor;
}
static void remember_edit(void) { store_snapshot(undo_history,&undo_count);redo_count=0; }
static void undo_edit(int redo) {
    unsigned *count=redo?&redo_count:&undo_count;
    NoteSnapshot *history=redo?redo_history:undo_history;
    if(!*count){strcpy(status,redo?"Nothing to redo":"Nothing to undo");return;}
    store_snapshot(redo?undo_history:redo_history,redo?&undo_count:&redo_count);
    NoteSnapshot *snapshot=&history[--*count];
    memcpy(note,snapshot->text,snapshot->length+1);note_length=snapshot->length;cursor=snapshot->cursor;
    selection_anchor=(size_t)-1;dirty=1;
}
static int load_note(void) {
    editable=dirty=0;note_length=cursor=first_row=0;note[0]=0;
    undo_count=redo_count=0;
    int fd=open(note_path,O_RDONLY);
    if (fd<0) {
        if(errno!=ENOENT) { strcpy(status,"Open failed; editing disabled"); return 0; }
        static const char starter[]="Welcome to Notes\n\nWrite here, then press Ctrl+S to save.\n";
        memcpy(note,starter,sizeof(starter)-1);
        note_length=sizeof(starter)-1;
        editable=dirty=1;
        strcpy(status,"New note | Ctrl+S saves");
        return 1;
    }
    struct stat information;
    if(fstat(fd,&information)<0 || information.st_size>=NOTE_CAPACITY) {
        close(fd);strcpy(status,"File unreadable or larger than 8191 bytes; editing disabled");return 0;
    }
    unsigned expected=(unsigned)information.st_size;
    while(note_length<expected) {
        ssize_t count=read(fd,note+note_length,expected-note_length);
        if(count<=0) { close(fd);note_length=0;note[0]=0;strcpy(status,"Read failed; editing disabled");return 0; }
        note_length+=(unsigned)count;
    }
    char extra;
    ssize_t tail=read(fd,&extra,1);
    int closed=close(fd);
    if(tail!=0 || closed<0) { note_length=0;note[0]=0;strcpy(status,"File changed or read failed; editing disabled");return 0; }
    note[note_length]=0;
    for(unsigned i=0;i<note_length;i++)if(!note[i]) {
        note_length=0;note[0]=0;strcpy(status,"Binary file; editing disabled");return 0;
    }
    editable=1;
    snprintf(status,sizeof(status),"Loaded %u bytes",note_length);
    printf("[notes] loaded %s\n",note_path);
    return 1;
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
    if(!editable) return;
    if (selection_anchor==(size_t)-1 || selection_anchor==cursor) { clear_selection(); return; }
    size_t lo=selection_anchor<cursor?selection_anchor:cursor;
    size_t hi=selection_anchor<cursor?cursor:selection_anchor;
    memmove(note+lo,note+hi,note_length-hi);
    note_length-=(unsigned)(hi-lo); cursor=(unsigned)lo;
    clear_selection(); dirty=1;
}
static void insert_character(uint32_t codepoint) {
    if(!editable) return;
    char encoded[4];size_t bytes=pollik_utf8_encode(codepoint,encoded);if(!bytes)return;
    size_t removed=selection_anchor!=(size_t)-1?(selection_anchor<cursor?cursor-selection_anchor:selection_anchor-cursor):0;
    if (note_length-removed+bytes>=NOTE_CAPACITY) { strcpy(status,"Note is full (8191 bytes)"); return; }
    remember_edit();
    if (selection_anchor!=(size_t)-1 && selection_anchor!=cursor) erase_selection();
    else clear_selection();
    memmove(note+cursor+bytes,note+cursor,note_length-cursor);
    memcpy(note+cursor,encoded,bytes);cursor+=(unsigned)bytes;note_length+=(unsigned)bytes;note[note_length]=0;dirty=1;
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
    pollik_ui_text(&canvas,70,43,note_path,0x94a3b8);
    pollik_ui_fill(&canvas,w-117,22,93,35,dirty?0x315a91:0x252d40);
    pollik_ui_text(&canvas,w-94,33,dirty?"UNSAVED":"SAVED",dirty?0xffffff:0x94a3b8);
    pollik_ui_fill(&canvas,0,88,w,38,0x151b29);
    if(saving_as) {
        pollik_ui_text(&canvas,28,101,"Save as:",0x82c5ff);
        pollik_ui_text(&canvas,116,101,save_path,0xdce2ed);
    } else {
        pollik_ui_text(&canvas,28,101,"Ctrl+S Save  Ctrl+Shift+S Save as  Ctrl+Z/Y Undo/Redo",0x82c5ff);
    }

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
        char number[12], line[1024];
        snprintf(number,sizeof(number),"%u",row+1);
        pollik_ui_text(&canvas,38,y,number,0x77849a);
        unsigned start=row_start[row], end=row_end[row];
        unsigned count=end-start;
        if (count>=sizeof(line)) count=sizeof(line)-1;
        memcpy(line,note+start,count); line[count]=0;
        if (selection_anchor!=(size_t)-1 && sel_hi>sel_lo) {
            size_t a=sel_lo>start?sel_lo:start, b=sel_hi<end?sel_hi:end;
            if (b>a) {
                char before[1024], selected[1024];
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
            char prefix[1024]; size_t n=caret-start; if(n>sizeof(prefix)-1)n=sizeof(prefix)-1;
            memcpy(prefix,line,n); prefix[n]=0;
            pollik_ui_fill(&canvas,96+text_width(prefix),y-1,2,19,0x315a91);
        }
    }
    pollik_ui_fill(&canvas,0,h-31,w,31,0x171e2b);
    pollik_ui_fill(&canvas,0,h-32,w,1,0x2b3549);
    unsigned line_no=1,column=1;
    unsigned characters=0;
    for(unsigned i=0;i<note_length;) { size_t used;uint32_t c=pollik_utf8_next(note+i,note_length-i,&used);
        if(i<cursor){if(c=='\n'){++line_no;column=1;}else ++column;}++characters;i+=(unsigned)used; }
    unsigned words=0; int in_word=0;
    for (unsigned i=0;i<note_length;i++) { if(note[i]==' '||note[i]=='\n'||note[i]=='\t')in_word=0;else if(!in_word){++words;in_word=1;} }
    char info[100]; snprintf(info,sizeof(info),"Ln %u, Col %u    %u words    %u characters",line_no,column,words,characters);
    pollik_ui_text(&canvas,28,h-22,saving_as?"Enter saves; Esc cancels; Ctrl+A clears the path":info,0x94a3b8);
    pollik_ui_text(&canvas,96,h-53,status,0x94a3b8);
}
int main(int argc,char **argv) {
    if(argc>1) {
        if(strlen(argv[1])>=sizeof(note_path)) { puts("[notes] path too long");return 1; }
        strcpy(note_path,argv[1]);
    }
    unsigned width=760,height=560;
    int64_t mapped=pollikos_window_create(width,height,"Notes");
    if(mapped<0){width=620;height=440;mapped=pollikos_window_create(width,height,"Notes");}
    if(mapped<0){puts("[notes] cannot create window");return 1;}
    canvas=(PollikCanvas){(uint32_t *)(uintptr_t)mapped,width,height};
    load_note(); make_rows(); draw();
    if(pollikos_window_present()<0){pollikos_window_destroy();return 2;}
    printf("[notes] ready: Ctrl+S saves %s\n",note_path);
    int running=1;
    while(running) {
        pollikos_input_event_t e;
        if(pollikos_input_read(&e)==(int64_t)sizeof(e)) {
            int changed=0;
            int extend=(e.modifiers&POLLIKOS_INPUT_MOD_SHIFT)!=0;
            if(e.kind&POLLIKOS_INPUT_WINDOW_CLOSE) {
                if(saving_as)saving_as=0;
                else if(!dirty || save_note()) running=0;
                changed=1;
            }
            if((e.kind&POLLIKOS_INPUT_KEY_DOWN) && saving_as) {
                if(e.key==27)saving_as=0;
                else if(e.key==13){if(save_as_path(save_path))saving_as=0;}
                else if((e.modifiers&POLLIKOS_INPUT_MOD_CONTROL) && (e.key=='a'||e.key=='A'))save_path_length=0;
                else if(e.key==8 && save_path_length)save_path_length=(unsigned)pollik_utf8_previous(save_path,save_path_length);
                else if(e.key>=32 && e.key<127 && !(e.modifiers&POLLIKOS_INPUT_MOD_CONTROL) && save_path_length<sizeof(save_path)-1)
                    save_path[save_path_length++]=(char)((e.modifiers&POLLIKOS_INPUT_MOD_SHIFT)?shifted_char(e.key):(int)e.key);
                save_path[save_path_length]=0;changed=1;
            } else if((e.kind&POLLIKOS_INPUT_KEY_DOWN) && (e.modifiers&POLLIKOS_INPUT_MOD_CONTROL)) {
                if(e.modifiers&POLLIKOS_INPUT_MOD_ALT) {
                    static const char keys[]="acelnosxz";
                    static const uint32_t lower[]={0x105,0x107,0x119,0x142,0x144,0xf3,0x15b,0x17a,0x17c};
                    static const uint32_t upper[]={0x104,0x106,0x118,0x141,0x143,0xd3,0x15a,0x179,0x17b};
                    uint32_t key=e.key;if(key>='A' && key<='Z')key+=32;
                    for(unsigned i=0;i<sizeof(keys)-1;i++)if(key==(unsigned)keys[i])insert_character(extend?upper[i]:lower[i]);
                    changed=1;
                } else if(e.key=='s'||e.key=='S') {
                    if(extend){saving_as=1;strcpy(save_path,note_path);save_path_length=(unsigned)strlen(save_path);}
                    else save_note();changed=1;
                }
                else if(e.key=='z'||e.key=='Z'||e.key=='y'||e.key=='Y') { if(editable)undo_edit(extend||e.key=='y'||e.key=='Y');changed=1; }
                else if(e.key=='a'||e.key=='A') { selection_anchor=0;cursor=note_length;changed=1; }
            } else if((e.kind&POLLIKOS_INPUT_KEY_DOWN) && editable) {
                if(e.key==13) { insert_character('\n'); changed=1; }
                else if(e.key==8) {
                    if(cursor || (selection_anchor!=(size_t)-1 && selection_anchor!=cursor))remember_edit();
                    if(selection_anchor!=(size_t)-1&&selection_anchor!=cursor)erase_selection();
                    else if(cursor){unsigned previous=(unsigned)pollik_utf8_previous(note,cursor);clear_selection();memmove(note+previous,note+cursor,note_length-cursor);note_length-=cursor-previous;cursor=previous;note[note_length]=0;dirty=1;}
                    changed=1;
                } else if(e.key==POLLIKOS_KEY_DELETE) {
                    if(cursor<note_length || (selection_anchor!=(size_t)-1 && selection_anchor!=cursor))remember_edit();
                    if(selection_anchor!=(size_t)-1&&selection_anchor!=cursor)erase_selection();
                    else if(cursor<note_length){size_t used;(void)pollik_utf8_next(note+cursor,note_length-cursor,&used);clear_selection();memmove(note+cursor,note+cursor+used,note_length-cursor-used);note_length-=(unsigned)used;note[note_length]=0;dirty=1;}
                    changed=1;
                } else if(e.key==POLLIKOS_KEY_LEFT) { if(cursor)move_cursor(pollik_utf8_previous(note,cursor),extend);changed=1; }
                else if(e.key==POLLIKOS_KEY_RIGHT) { if(cursor<note_length){size_t used;(void)pollik_utf8_next(note+cursor,note_length-cursor,&used);move_cursor(cursor+used,extend);}changed=1; }
                else if(e.key==POLLIKOS_KEY_HOME||e.key==POLLIKOS_KEY_END) {
                    unsigned row=cursor_row(); size_t target=e.key==POLLIKOS_KEY_HOME?row_start[row]:row_end[row];
                    move_cursor(target,extend);changed=1;
                } else if(e.key==POLLIKOS_KEY_UP||e.key==POLLIKOS_KEY_DOWN) {
                    unsigned row=cursor_row(), col=0;
                    for(unsigned at=row_start[row];at<cursor;col++){size_t used;(void)pollik_utf8_next(note+at,cursor-at,&used);at+=(unsigned)used;}
                    if(e.key==POLLIKOS_KEY_UP&&row) --row;
                    else if(e.key==POLLIKOS_KEY_DOWN&&row+1<row_count) ++row;
                    unsigned target=row_start[row];
                    while(col-- && target<row_end[row]){size_t used;(void)pollik_utf8_next(note+target,row_end[row]-target,&used);target+=(unsigned)used;}
                    move_cursor(target,extend);changed=1;
                } else if(e.key>=32&&e.key<127) {
                    int c=(e.modifiers&POLLIKOS_INPUT_MOD_SHIFT)?shifted_char(e.key):(int)e.key;
                    insert_character((uint32_t)c);changed=1;
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
