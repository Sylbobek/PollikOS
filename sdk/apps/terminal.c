#include <pollikos/window.h>
#include <pollikos/process.h>
#include <pollikos/fs.h>
#include <pollikos/time.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "window_ui.h"

#define TERM_LINES 192
#define TERM_COLS 128
#define TERM_CELL_W 8
#define TERM_CELL_H 18
#define TERM_MARGIN 16

static char lines[TERM_LINES][TERM_COLS];
static uint64_t line_number;
static unsigned cursor_column, scroll_back;
static unsigned csi_value;
static int escape_state;
static int input_pipe=-1, output_pipe=-1;
static long shell_pid=-1;
static int shell_output_seen;

static char *current_line(void) { return lines[line_number%TERM_LINES]; }
static void clear_output(void) {
    memset(lines,0,sizeof(lines));
    line_number=cursor_column=scroll_back=0;
}
static void next_line(void) {
    ++line_number;
    memset(current_line(),0,TERM_COLS);
    cursor_column=0;
    scroll_back=0;
}
static void clear_to_end(void) {
    char *line=current_line();
    if (cursor_column<TERM_COLS) memset(line+cursor_column,0,TERM_COLS-cursor_column);
}
static void output_byte(unsigned char byte) {
    if (escape_state==1) {
        escape_state=byte=='['?2:0;
        csi_value=0;
        return;
    }
    if (escape_state==2) {
        if (byte>='0'&&byte<='9') {
            if (csi_value<1000) csi_value=csi_value*10+(byte-'0');
            return;
        }
        if (byte==';') { csi_value=0; return; }
        if (byte>=0x40&&byte<=0x7e) {
            if (byte=='J'&&(csi_value==2||csi_value==3)) clear_output();
            else if (byte=='K') clear_to_end();
            else if (byte=='D') cursor_column=cursor_column>csi_value?cursor_column-(csi_value?csi_value:1):0;
            else if (byte=='C') {
                unsigned step=csi_value?csi_value:1;
                cursor_column+=step;
                if (cursor_column>=TERM_COLS) cursor_column=TERM_COLS-1;
            } else if (byte=='H'||byte=='f') { line_number=cursor_column=0; }
            escape_state=0;
        }
        return;
    }
    if (byte==27) { escape_state=1; return; }
    if (byte=='\r') { cursor_column=0; return; }
    if (byte=='\n') { next_line(); return; }
    if (byte=='\b'||byte==127) {
        if (cursor_column) current_line()[--cursor_column]=0;
        return;
    }
    if (byte=='\t') {
        unsigned target=(cursor_column+4)&~3u;
        while (cursor_column<target&&cursor_column<TERM_COLS-1) current_line()[cursor_column++]=' ';
        return;
    }
    if (byte<32||byte>=127) return;
    if (cursor_column>=TERM_COLS-1) next_line();
    current_line()[cursor_column++]=(char)byte;
}

static void draw_text(PollikCanvas *canvas,int x,int y,const char *text,
                      unsigned max_columns,uint32_t color) {
    for (unsigned column=0;column<max_columns&&text[column];column++) {
        unsigned char ch=(unsigned char)text[column];
        if (ch<32||ch>=127) ch='?';
        const FontGlyph *glyph=&font_glyphs[0][ch-32];
        int origin=x+(int)column*TERM_CELL_W+(TERM_CELL_W-(int)glyph->width)/2;
        for (unsigned row=0;row<glyph->height;row++) {
            for (unsigned col=0;col<glyph->width;col++) {
                unsigned pixel=row*glyph->width+col;
                uint8_t packed=font_coverage[glyph->offset+pixel/2];
                unsigned coverage=(pixel&1)?packed&15:packed>>4;
                int px=origin+(int)col,py=y+(int)row;
                if (coverage>=6&&px>=0&&py>=0&&(unsigned)px<canvas->width&&
                    (unsigned)py<canvas->height)
                    canvas->pixels[(size_t)py*canvas->width+(unsigned)px]=color;
            }
        }
    }
}

static void draw(PollikCanvas *canvas) {
    int width=(int)canvas->width,height=(int)canvas->height;
    unsigned columns=(canvas->width-2*TERM_MARGIN)/TERM_CELL_W;
    unsigned visible=(canvas->height>104?(canvas->height-104)/TERM_CELL_H:1);
    if (columns>TERM_COLS) columns=TERM_COLS;
    if (visible>TERM_LINES) visible=TERM_LINES;
    pollik_ui_fill(canvas,0,0,width,height,0x0d111b);
    pollik_ui_fill(canvas,0,0,width,37,0x171e2b);
    pollik_ui_fill(canvas,16,12,8,8,0x79c8ff);
    draw_text(canvas,34,10,"Terminal",24,0xf0f2f7);
    draw_text(canvas,width-200,11,"PollikOS  |  SHELL",22,0x9ba8bd);
    pollik_ui_fill(canvas,10,44,width-20,height-88,0x101521);

    uint64_t newest=line_number;
    uint64_t oldest=newest>=TERM_LINES-1?newest-(TERM_LINES-1):0;
    uint64_t start=newest+1>visible?newest+1-visible:0;
    if (scroll_back>0) start=start>scroll_back?start-scroll_back:oldest;
    if (start<oldest) start=oldest;
    for (unsigned row=0;row<visible;row++) {
        uint64_t index=start+row;
        if (index>newest) break;
        draw_text(canvas,TERM_MARGIN,51+(int)row*TERM_CELL_H,
                  lines[index%TERM_LINES],columns,0xd7deea);
    }
    if (!scroll_back&&line_number>=start&&line_number<start+visible&&
        cursor_column<columns) {
        int x=TERM_MARGIN+(int)cursor_column*TERM_CELL_W;
        int y=51+(int)(line_number-start)*TERM_CELL_H+1;
        pollik_ui_fill(canvas,x,y,2,14,0x82b9ff);
    }
    pollik_ui_fill(canvas,0,height-34,width,34,0x171e2b);
    draw_text(canvas,16,height-23,"UP/DOWN history   CTRL+C interrupt   CTRL+SHIFT+Q close",55,0x8d9bb1);
}

static int launch_shell(void) {
    int input[2]={-1,-1},output[2]={-1,-1};
    int saved[3]={-1,-1,-1};
    if (pipe(input)<0||pipe(output)<0) goto fail;
    for (int i=0;i<3;i++) { saved[i]=dup(i); if (saved[i]<0) goto fail; }
    if (dup2(input[0],0)<0||dup2(output[1],1)<0||dup2(output[1],2)<0) goto fail;
    const char *arguments[]={"pollish",0};
    shell_pid=pollikos_spawn_group("/bin/pollish",arguments,0,0);
    for (int i=0;i<3;i++) { if (saved[i]>=0) { (void)dup2(saved[i],i); close(saved[i]); saved[i]=-1; } }
    if (shell_pid<0) goto fail;
    close(input[0]); input[0]=-1;
    close(output[1]); output[1]=-1;
    input_pipe=input[1]; input[1]=-1;
    output_pipe=output[0]; output[0]=-1;
    puts("[terminal] shell connected through PollikOS pipes");
    return 1;
fail:
    for (int i=0;i<3;i++) if (saved[i]>=0) { (void)dup2(saved[i],i); close(saved[i]); }
    for (int i=0;i<2;i++) { if (input[i]>=0) close(input[i]); if (output[i]>=0) close(output[i]); }
    return 0;
}

static void send_bytes(const void *bytes,size_t length) {
    if (input_pipe>=0&&length) (void)write(input_pipe,bytes,length);
}
static void send_escape(const char *sequence) { send_bytes(sequence,strlen(sequence)); }
static void key_down(const pollikos_input_event_t *event) {
    unsigned key=event->key;
    if ((event->modifiers&POLLIKOS_INPUT_MOD_CONTROL)&&(key=='c'||key=='C')) {
        const unsigned char interrupt=3; send_bytes(&interrupt,1); return;
    }
    if (key==POLLIKOS_KEY_UP) send_escape("\x1b[A");
    else if (key==POLLIKOS_KEY_DOWN) send_escape("\x1b[B");
    else if (key==POLLIKOS_KEY_RIGHT) send_escape("\x1b[C");
    else if (key==POLLIKOS_KEY_LEFT) send_escape("\x1b[D");
    else if (key==POLLIKOS_KEY_HOME) send_escape("\x1b[H");
    else if (key==POLLIKOS_KEY_END) send_escape("\x1b[F");
    else if (key==POLLIKOS_KEY_DELETE) send_escape("\x1b[3~");
    else if (key==13||key==10) { const char enter='\r'; send_bytes(&enter,1); }
    else if (key==8||key==127) { const char backspace=127; send_bytes(&backspace,1); }
    else if (key>=32&&key<127) { const char character=(char)key; send_bytes(&character,1); }
}

int main(void) {
    unsigned width=760,height=440;
    int64_t mapped=pollikos_window_create(width,height,"Terminal");
    if (mapped<0) { width=620;height=360; mapped=pollikos_window_create(width,height,"Terminal"); }
    if (mapped<0) { puts("[terminal] cannot create window"); return 1; }
    PollikCanvas canvas={(uint32_t *)(uintptr_t)mapped,width,height};
    clear_output();
    if (!launch_shell()) {
        lines[0][0]='T'; lines[0][1]='e'; lines[0][2]='r'; lines[0][3]='m';
        lines[0][4]='i'; lines[0][5]='n'; lines[0][6]='a'; lines[0][7]='l';
        draw(&canvas); (void)pollikos_window_present();
        (void)pollikos_window_destroy();
        return 2;
    }
    draw(&canvas);
    if (pollikos_window_present()<0) { (void)kill((pid_t)-shell_pid,SIGTERM); pollikos_window_destroy(); return 3; }
    int running=1;
    while (running) {
        char output[512];
        long received=pollikos_pipe_read_available(output_pipe,output,sizeof(output));
        if (received>0) {
            if (!shell_output_seen) {
                puts("[terminal] output active");
                shell_output_seen=1;
            }
            for (long i=0;i<received;i++) output_byte((unsigned char)output[i]);
            scroll_back=0;
            draw(&canvas);
            if (pollikos_window_present()<0) break;
        }
        int shell_status;
        if (waitpid((pid_t)shell_pid,&shell_status,WNOHANG)==(pid_t)shell_pid) {
            shell_pid=-1;
            running=0;
        }
        pollikos_input_event_t event;
        if (pollikos_input_read(&event)==(int64_t)sizeof(event)) {
            if (event.kind&POLLIKOS_INPUT_WINDOW_CLOSE) running=0;
            if ((event.kind&POLLIKOS_INPUT_KEY_DOWN)&&
                (event.modifiers&(POLLIKOS_INPUT_MOD_CONTROL|POLLIKOS_INPUT_MOD_SHIFT))==
                    (POLLIKOS_INPUT_MOD_CONTROL|POLLIKOS_INPUT_MOD_SHIFT)&&
                (event.key=='q'||event.key=='Q')) running=0;
            else if (event.kind&POLLIKOS_INPUT_KEY_DOWN) key_down(&event);
            if ((event.kind&POLLIKOS_INPUT_MOUSE_WHEEL)&&event.wheel) {
                if (event.wheel>0&&scroll_back<TERM_LINES) ++scroll_back;
                else if (event.wheel<0&&scroll_back) --scroll_back;
                draw(&canvas); (void)pollikos_window_present();
            }
        }
        (void)pollikos_sleep_ms(8);
    }
    if (shell_pid>0) {
        (void)kill((pid_t)-shell_pid,SIGTERM);
        int status;
        (void)waitpid((pid_t)shell_pid,&status,0);
    }
    if (input_pipe>=0) close(input_pipe);
    if (output_pipe>=0) close(output_pipe);
    puts("[terminal] closing");
    return pollikos_window_destroy()<0?4:0;
}
