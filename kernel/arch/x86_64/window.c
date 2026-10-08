/* Process-owned x86_64 window surfaces and a bounded framebuffer compositor. */
#include "window.h"
#include "console_fb.h"
#include "heap.h"
#include "mouse.h"
#include "paging.h"

#define WINDOW_BORDER 2u
#define WINDOW_TITLEBAR 30u
#define WINDOW_MIN_WIDTH 80u
#define WINDOW_MIN_HEIGHT 48u
#define WINDOW_MAX_WIDTH 960u
#define WINDOW_MAX_HEIGHT 640u
#define WINDOW_KEY_QUEUE 32u
#define WINDOW_KEYBOARD_BASE (MM_KERNEL_START+UINT64_C(0x30000000))
#define WINDOW_STATE_BASE (MM_KERNEL_START+UINT64_C(0x32000000))
#define WINDOW_OUTER_W(w) ((w)+2u*WINDOW_BORDER)
#define WINDOW_OUTER_H(h) ((h)+WINDOW_TITLEBAR+2u*WINDOW_BORDER)
#define WINDOW_CONTENT_X(w) ((w)->x+(int)WINDOW_BORDER)
#define WINDOW_CONTENT_Y(w) ((w)->y+(int)(WINDOW_TITLEBAR+WINDOW_BORDER))

typedef struct {
    Process64 *owner;
    virt_addr_t pixels, backing;
    unsigned width, height, outer_width, outer_height;
    int x, y, visible;
    int underlay_valid;
    int drag_x, drag_y;
    uint64_t z;
    char title[USER_WINDOW_TITLE_MAX];
} Window64;

/* A process owns at most one window. Reuse the process table's natural
 * capacity instead of imposing a separate desktop-wide window ceiling. */
static Window64 *windows;
typedef struct { MouseEvent64 events[WINDOW_KEY_QUEUE]; unsigned head, count; } KeyQueue64;
static KeyQueue64 *keyboard;
static uint64_t next_z=1;
static int dragging=-1;
static int console_suspended;
static int session_hidden;
static uint64_t keyboard_sequence;

int window64_init(void) {
    if (keyboard&&windows) return 1;
    size_t capacity=process64_capacity();
    size_t bytes=capacity*sizeof(*keyboard);
    size_t pages=(bytes+MM_PAGE_SIZE-1)/MM_PAGE_SIZE, mapped=0;
    while (mapped<pages &&
           vmm64_alloc_page(vmm64_kernel(),WINDOW_KEYBOARD_BASE+mapped*MM_PAGE_SIZE,VM_WRITE)==VM_OK)
        ++mapped;
    if (mapped!=pages) {
        while (mapped) {
            --mapped;
            memory_require(vmm64_unmap(vmm64_kernel(),WINDOW_KEYBOARD_BASE+mapped*MM_PAGE_SIZE,1,0)==VM_OK,
                           "window keyboard queue allocation rollback");
        }
        return 0;
    }
    size_t state_pages=(capacity*sizeof(*windows)+MM_PAGE_SIZE-1)/MM_PAGE_SIZE,state_mapped=0;
    while(state_mapped<state_pages&&vmm64_alloc_page(vmm64_kernel(),WINDOW_STATE_BASE+state_mapped*MM_PAGE_SIZE,VM_WRITE)==VM_OK)state_mapped++;
    if(state_mapped!=state_pages){
        while(state_mapped){state_mapped--;memory_require(vmm64_unmap(vmm64_kernel(),WINDOW_STATE_BASE+state_mapped*MM_PAGE_SIZE,1,0)==VM_OK,"window state allocation rollback");}
        while(mapped){mapped--;memory_require(vmm64_unmap(vmm64_kernel(),WINDOW_KEYBOARD_BASE+mapped*MM_PAGE_SIZE,1,0)==VM_OK,"window keyboard allocation rollback");}
        return 0;
    }
    windows=(Window64 *)(uintptr_t)WINDOW_STATE_BASE;
    keyboard=(KeyQueue64 *)(uintptr_t)WINDOW_KEYBOARD_BASE;
    for(size_t i=0;i<capacity;i++)windows[i]=(Window64){0};
    for (size_t i=0;i<capacity;i++) keyboard[i]=(KeyQueue64){0};
    return 1;
}

static int window_index(const Process64 *process) {
    if(!windows)return -1;
    for (unsigned i=0;i<process64_capacity();i++)
        if (windows[i].owner==process) return (int)i;
    return -1;
}
static int topmost(void) {
    if(session_hidden||!windows) return -1;
    int best=-1;
    for (unsigned i=0;i<process64_capacity();i++)
        if (windows[i].owner && windows[i].visible &&
            (best<0 || windows[i].z>windows[best].z)) best=(int)i;
    return best;
}
static unsigned ordered_windows(int order[PROCESS_MAX], int descending) {
    if(!windows)return 0;
    unsigned count=0;
    for (unsigned i=0;i<process64_capacity();i++) {
        if (!windows[i].owner) continue;
        unsigned at=count;
        while (at && (descending ? windows[order[at-1]].z<windows[i].z
                                 : windows[order[at-1]].z>windows[i].z)) {
            order[at]=order[at-1];
            --at;
        }
        order[at]=(int)i;
        ++count;
    }
    return count;
}
static uint64_t window_bytes(unsigned width, unsigned height) {
    return (uint64_t)width*height*4;
}
static int row_transfer(Window64 *window, int backing, unsigned row, int restore) {
    uint32_t pixels[1024];
    unsigned width=window->outer_width;
    virt_addr_t base=backing ? window->backing : window->pixels;
    if (backing) {
        virt_addr_t at=base+(uint64_t)row*width*4;
        if (restore) {
            if (copy_from_user64(&window->owner->space,pixels,at,width*4)!=USER_COPY_OK)
                return 0;
            return console_fb_write_pixels((unsigned)window->x,(unsigned)window->y+row,
                                           width,pixels);
        }
        if (!console_fb_read_pixels((unsigned)window->x,(unsigned)window->y+row,
                                    width,pixels)) return 0;
        return copy_to_user64(&window->owner->space,at,pixels,width*4)==USER_COPY_OK;
    }
    width=window->width;
    virt_addr_t at=base+(uint64_t)row*width*4;
    if (copy_from_user64(&window->owner->space,pixels,at,width*4)!=USER_COPY_OK)
        return 0;
    return console_fb_write_pixels((unsigned)WINDOW_CONTENT_X(window),
                                   (unsigned)WINDOW_CONTENT_Y(window)+row,width,pixels);
}
static int restore_windows(void) {
    int order[PROCESS_MAX];
    unsigned count=ordered_windows(order,1);
    for (unsigned step=0;step<count;step++) {
        int index=order[step];
        Window64 *window=&windows[index];
        if (!window->underlay_valid) continue;
        for (unsigned row=0;row<window->outer_height;row++)
            if (!row_transfer(window,1,row,1)) return 0;
        window->underlay_valid=0;
    }
    return 1;
}
static int paint_windows(void) {
    if(session_hidden) return 1;
    int order[PROCESS_MAX];
    unsigned count=ordered_windows(order,0);
    for (unsigned step=0;step<count;step++) {
        int index=order[step];
        Window64 *window=&windows[index];
        if (!window->visible) continue;
        for (unsigned row=0;row<window->outer_height;row++)
            if (!row_transfer(window,1,row,0)) return 0;
        window->underlay_valid=1;
        console_fb_draw_window_frame((unsigned)window->x,(unsigned)window->y,
                                     window->outer_width,window->outer_height,
                                     window->title);
        for (unsigned row=0;row<window->height;row++)
            if (!row_transfer(window,0,row,0)) return 0;
    }
    return 1;
}
static int repaint_windows(void) {
    console_fb_overlay_begin();
    int ok=restore_windows() && paint_windows();
    console_fb_overlay_end();
    return ok;
}
void window64_session_hide(int hidden) {
    console_fb_overlay_begin();
    if(hidden && !session_hidden) memory_require(restore_windows(),"session hide windows");
    session_hidden=hidden;
    dragging=-1;
    if(keyboard) for(size_t i=0;i<process64_capacity();i++) keyboard[i]=(KeyQueue64){0};
    mouse64_flush();
    if(!hidden) memory_require(paint_windows(),"session restore windows");
    console_fb_overlay_end();
}
void window64_console_begin(void) { console_suspended=0; }
void window64_console_damage(unsigned x, unsigned y, unsigned width, unsigned height) {
    if (!windows||console_suspended || !width || !height) return;
    for (unsigned i=0;i<process64_capacity();i++) {
        Window64 *window=&windows[i];
        if (!window->owner || !window->visible) continue;
        uint64_t right=(uint64_t)x+width, bottom=(uint64_t)y+height;
        if (right>(unsigned)window->x && bottom>(unsigned)window->y &&
            x<(unsigned)(window->x+(int)window->outer_width) &&
            y<(unsigned)(window->y+(int)window->outer_height)) {
            memory_require(restore_windows(),"window underlay restore");
            console_suspended=1;
            return;
        }
    }
}
void window64_console_end(void) {
    if (console_suspended)
        memory_require(paint_windows(),"window underlay capture");
    console_suspended=0;
}
static int64_t create_window(Process64 *process, UserFrame *frame) {
    uint64_t requested_width=frame->rdi, requested_height=frame->rsi;
    if (window_index(process)>=0) return -USER_EEXIST;
    unsigned screen_width=console_fb_width(), screen_height=console_fb_height();
    if (requested_width<WINDOW_MIN_WIDTH || requested_height<WINDOW_MIN_HEIGHT ||
        requested_width>WINDOW_MAX_WIDTH || requested_height>WINDOW_MAX_HEIGHT ||
        requested_width+2*WINDOW_BORDER>screen_width ||
        requested_height+WINDOW_TITLEBAR+2*WINDOW_BORDER>screen_height) return -USER_EINVAL;
    if (!window64_init()) return -USER_ENOMEM;
    int slot=-1;
    for (unsigned i=0;i<process64_capacity();i++) if (!windows[i].owner) { slot=(int)i; break; }
    if (slot<0) return -USER_ENOSPC;
    unsigned width=(unsigned)requested_width, height=(unsigned)requested_height;
    char title[USER_WINDOW_TITLE_MAX];
    if (frame->rdx) {
        if (copy_string_from_user64(&process->space,title,frame->rdx,sizeof(title))!=USER_COPY_OK)
            return -USER_EFAULT;
    } else {
        const char default_title[]="PollikOS App";
        for (unsigned i=0;i<sizeof(default_title);i++) title[i]=default_title[i];
    }
    if (!title[0]) {
        const char default_title[]="PollikOS App";
        for (unsigned i=0;i<sizeof(default_title);i++) title[i]=default_title[i];
    }
    uint64_t pixels_bytes=window_bytes(width,height);
    uint64_t backing_bytes=(uint64_t)WINDOW_OUTER_W(width)*WINDOW_OUTER_H(height)*4;
    if (pixels_bytes>(uint64_t)USER_MMAP_MAX_PAGES*MM_PAGE_SIZE ||
        backing_bytes>(uint64_t)USER_MMAP_MAX_PAGES*MM_PAGE_SIZE) return -USER_E2BIG;
    int64_t pixels=heap64_mmap(process,pixels_bytes,0);
    if (pixels<0) return pixels;
    int64_t backing=heap64_mmap(process,backing_bytes,0);
    if (backing<0) {
        memory_require(heap64_munmap(process,(uint64_t)pixels,pixels_bytes)==0,
                       "window surface allocation rollback");
        return backing;
    }
    Window64 window={0};
    window.owner=process;
    window.pixels=(virt_addr_t)pixels;
    window.backing=(virt_addr_t)backing;
    window.width=width; window.height=height;
    window.outer_width=WINDOW_OUTER_W(width); window.outer_height=WINDOW_OUTER_H(height);
    window.x=(int)(screen_width-window.outer_width)/2;
    window.y=(int)(screen_height-window.outer_height)/2;
    window.visible=1;
    window.z=next_z++;
    if (!next_z) next_z=1;
    for (unsigned i=0;i<sizeof(window.title);i++) window.title[i]=title[i];
    keyboard[slot]=(KeyQueue64){0};
    windows[slot]=window;
    mouse64_flush();
    if (!repaint_windows()) {
        windows[slot]=(Window64){0};
        memory_require(heap64_munmap(process,(uint64_t)backing,backing_bytes)==0 &&
                       heap64_munmap(process,(uint64_t)pixels,pixels_bytes)==0,
                       "window initialization rollback");
        return -USER_EFAULT;
    }
    return pixels;
}
static int64_t destroy_window(Process64 *process) {
    int index=window_index(process);
    if (index<0) return -USER_EINVAL;
    Window64 old=windows[index];
    if (!restore_windows()) return -USER_EFAULT;
    windows[index]=(Window64){0};
    keyboard[index]=(KeyQueue64){0};
    if (dragging==index) dragging=-1;
    memory_require(heap64_munmap(process,old.backing,
                   (uint64_t)old.outer_width*old.outer_height*4)==0 &&
                   heap64_munmap(process,old.pixels,window_bytes(old.width,old.height))==0,
                   "window surface release");
    return repaint_windows() ? 0 : -USER_EFAULT;
}
static int hit_window(const Window64 *window, int x, int y) {
    return window->visible && x>=window->x && y>=window->y &&
           x<window->x+(int)window->outer_width && y<window->y+(int)window->outer_height;
}
static void clamp_position(Window64 *window, int x, int y) {
    int max_x=(int)console_fb_width()-(int)window->outer_width;
    int max_y=(int)console_fb_height()-(int)window->outer_height;
    if (x<0) x=0; else if (x>max_x) x=max_x;
    if (y<0) y=0; else if (y>max_y) y=max_y;
    window->x=x; window->y=y;
}
static int64_t read_input(Process64 *process, uint64_t destination) {
    int own=window_index(process), front=topmost();
    if (own<0 || own!=front) return -USER_EPERM;
    MouseEvent64 event;
    KeyQueue64 *keys=&keyboard[own];
    if (keys->count) {
        event=keys->events[keys->head];
        keys->head=(keys->head+1)%WINDOW_KEY_QUEUE;
        --keys->count;
    } else if (!mouse64_pop(&event)) return -USER_EAGAIN;
    if ((event.kind&USER_INPUT_MOUSE_BUTTON) && (event.changed&USER_MOUSE_BUTTON_LEFT) &&
        (event.buttons&USER_MOUSE_BUTTON_LEFT)) {
        int target=-1;
        for (unsigned i=0;i<process64_capacity();i++)
            if (windows[i].owner && hit_window(&windows[i],event.x,event.y) &&
                (target<0 || windows[i].z>windows[target].z)) target=(int)i;
        if (target>=0 && target!=own) {
            /* Saved underlays belong to the current stacking order. Restore
             * them before raising a window, then capture the new order. */
            console_fb_overlay_begin();
            int restored=restore_windows();
            windows[target].z=next_z++;
            int painted=restored&&paint_windows();
            console_fb_overlay_end();
            if(!painted)return -USER_EFAULT;
            return -USER_EAGAIN;
        }
        Window64 *window=&windows[own];
        if (target==own && event.y<window->y+(int)WINDOW_TITLEBAR) {
            if (event.x>=window->x+(int)window->outer_width-22) {
                window->visible=0;
                event.kind|=USER_INPUT_WINDOW_CLOSE;
                dragging=-1;
                if (!repaint_windows()) return -USER_EFAULT;
            } else {
                dragging=own;
                window->drag_x=event.x-window->x;
                window->drag_y=event.y-window->y;
            }
        }
    }
    if ((event.kind&USER_INPUT_MOUSE_MOVE) && dragging==own) {
        if (event.buttons&USER_MOUSE_BUTTON_LEFT) {
            Window64 *window=&windows[own];
            int old_x=window->x,old_y=window->y;
            clamp_position(window,event.x-window->drag_x,event.y-window->drag_y);
            if ((old_x!=window->x || old_y!=window->y) && !repaint_windows()) return -USER_EFAULT;
        } else dragging=-1;
    }
    if ((event.kind&USER_INPUT_MOUSE_BUTTON) && !(event.buttons&USER_MOUSE_BUTTON_LEFT) &&
        dragging==own) dragging=-1;
    if (copy_to_user64(&process->space,destination,&event,sizeof(event))!=USER_COPY_OK)
        return -USER_EFAULT;
    return sizeof(event);
}
int window64_dispatch(Process64 *process, UserFrame *frame) {
    if((frame->rax>=USER_INPUT_READ && frame->rax<=USER_WINDOW_INFO) &&
       !security_has(&process->credentials,CAP_WINDOW)) {
        frame->rax=(uint64_t)-(int64_t)USER_EPERM;return 1;
    }
    int64_t result;
    switch (frame->rax) {
    case USER_WINDOW_CREATE: result=create_window(process,frame); break;
    case USER_WINDOW_PRESENT: {
        int index=window_index(process);
        if (index<0 || !windows[index].visible) result=-USER_EINVAL;
        else result=repaint_windows()?0:-USER_EFAULT;
        break;
    }
    case USER_WINDOW_DESTROY: result=destroy_window(process); break;
    case USER_INPUT_READ: result=read_input(process,frame->rdi); break;
    case USER_WINDOW_INFO: {
        int index=window_index(process);
        if (index<0) result=-USER_EINVAL;
        else {
            struct { uint32_t version,size; int32_t content_x,content_y; uint32_t width,height; } info={
                1,sizeof(info),WINDOW_CONTENT_X(&windows[index]),WINDOW_CONTENT_Y(&windows[index]),
                windows[index].width,windows[index].height};
            result=copy_to_user64(&process->space,frame->rdi,&info,sizeof(info))==USER_COPY_OK
                ? 0 : -USER_EFAULT;
        }
        break;
    }
    default: return 0;
    }
    frame->rax=(uint64_t)result;
    return 1;
}
void window64_process_cleanup(Process64 *process) {
    int index=window_index(process);
    if (index<0) return;
    if (restore_windows()) {
        windows[index]=(Window64){0};
        keyboard[index]=(KeyQueue64){0};
        if (dragging==index) dragging=-1;
        memory_require(paint_windows(),"window cleanup repaint");
    }
}
int window64_key_event(uint32_t key, uint32_t modifiers, int down) {
    int index=topmost();
    if (index<0) return 0;
    KeyQueue64 *queue=&keyboard[index];
    if (queue->count==WINDOW_KEY_QUEUE) {
        queue->head=(queue->head+1)%WINDOW_KEY_QUEUE;
        --queue->count;
    }
    unsigned tail=(queue->head+queue->count)%WINDOW_KEY_QUEUE;
    queue->events[tail]=(MouseEvent64){USER_INPUT_EVENT_VERSION,USER_INPUT_EVENT_SIZE,
        down?USER_INPUT_KEY_DOWN:USER_INPUT_KEY_UP,0,0,key,modifiers,0,0,0,++keyboard_sequence};
    ++queue->count;
    return 1;
}
int window64_mapping_busy(const Process64 *process, virt_addr_t address) {
    int index=window_index(process);
    if (index<0) return 0;
    return address==windows[index].pixels || address==windows[index].backing;
}
