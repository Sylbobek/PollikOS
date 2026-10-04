"""Maximize during opening must survive the opening animation's deadline."""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
body = re.search(r'void toggle_maximize\(int id\) \{.*?\n\}',
                 (ROOT / 'kernel/desktop.c').read_text(), re.S).group()
source = r'''
#include "ui_animation.h"
#include "wm.h"
extern int printf(const char *, ...);
static Window w;
volatile u32 ticks;
void *memset(void *p, int c, unsigned n) {
    unsigned char *s = p; while (n--) *s++ = (unsigned char)c; return p;
}
Window *wm_get_window(int id) { return id == 3 ? &w : 0; }
u32 wm_time_ms(void) { return 100; }
void wm_invalidate_rect(int x,int y,int a,int b) {(void)x;(void)y;(void)a;(void)b;}
void cancel_interaction(int id) {(void)id;}
void compositor_invalidate(int id) {(void)id;}
void gui_app_resized(int id,int a,int b) {(void)id;(void)a;(void)b;}
void request_scene_redraw(void) {}
int window_width(int id) {(void)id;return w.width;}
int window_height(int id) {(void)id;return w.height;}
void wm_toggle_maximize(int id) {
    (void)id; w.state=WINDOW_STATE_MAXIMIZED;
    w.x=0;w.y=32;w.width=1024;w.height=640;
}
'''+body+r'''
int main(void) {
    w.id=3;w.open=w.visible=1;w.x=170;w.y=125;w.width=680;w.height=410;
    ui_anim_init(); ui_anim_start_open(3,w.x,w.y,w.width,w.height);
    const WindowAnim *a=ui_anim_get(3);
    if (!a) return 2;
    u32 deadline=a->start_ms+a->duration_ms+1;
    toggle_maximize(3);ui_anim_update(deadline);
    printf("RAW maximize during open: state=%d rect=%d,%d,%d,%d animation_active=%d\n",
           w.state,w.x,w.y,w.width,w.height,ui_anim_get(3)!=0);
    if(w.state!=WINDOW_STATE_MAXIMIZED||w.x!=0||w.y!=32||w.width!=1024||w.height!=640) {
        printf("FAIL opening completion overwrote maximize\n");return 1;
    }
    printf("PASS maximize survives opening animation deadline\n"); return 0;
}
'''
path = ROOT / 'build/window_animation_race.c'
path.write_text(source)
exe = path.with_suffix('.exe')
cmd = ['clang', '-std=c11', '-fno-builtin', '-Wall', '-Wextra', '-Werror',
       '-fuse-ld=lld', '-Ikernel', str(path), 'kernel/ui_animation.c', '-o', str(exe)]
print('Compile: '+subprocess.list2cmdline(cmd), flush=True)
subprocess.run(cmd, cwd=ROOT, check=True)
sys.exit(subprocess.run([str(exe)], cwd=ROOT).returncode)
