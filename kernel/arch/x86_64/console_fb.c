#include "console_fb.h"
#include "memory.h"
#include "window.h"

typedef uint8_t u8;
typedef uint32_t u32;
#include "../../font_data.h"

#define FB_VIRTUAL (MM_KERNEL_START + UINT64_C(0x04000000))
static volatile uint8_t *pixels;
static unsigned width, height, byte_pitch, bytes_per_pixel, columns, rows, cx, cy;
static int ready, esc_state, esc_value;
static int mouse_visible, mouse_drawn, mouse_x, mouse_y;
static uint8_t mouse_under[16][12][4];

static const uint16_t mouse_shape[16] = {
    0x001,0x003,0x005,0x009,0x011,0x021,0x041,0x081,
    0x101,0x1ff,0x1b0,0x190,0x110,0x0a0,0x0a0,0x040
};

static void put_pixel(unsigned x, unsigned y, uint32_t color) {
    volatile uint8_t *p=pixels+y*byte_pitch+x*bytes_per_pixel;
    p[0]=(uint8_t)color; p[1]=(uint8_t)(color>>8); p[2]=(uint8_t)(color>>16);
    if (bytes_per_pixel==4) p[3]=0;
}
static void mouse_erase(void) {
    if (!mouse_drawn) return;
    for (unsigned y=0; y<16; ++y) for (unsigned x=0; x<12; ++x) {
        unsigned px=(unsigned)mouse_x+x, py=(unsigned)mouse_y+y;
        if (px>=width || py>=height) continue;
        volatile uint8_t *p=pixels+py*byte_pitch+px*bytes_per_pixel;
        for (unsigned b=0; b<bytes_per_pixel; ++b) p[b]=mouse_under[y][x][b];
    }
    mouse_drawn=0;
}
static void mouse_draw(void) {
    if (!ready || !mouse_visible || mouse_drawn) return;
    for (unsigned y=0; y<16; ++y) for (unsigned x=0; x<12; ++x) {
        unsigned px=(unsigned)mouse_x+x, py=(unsigned)mouse_y+y;
        if (px>=width || py>=height) continue;
        volatile uint8_t *p=pixels+py*byte_pitch+px*bytes_per_pixel;
        for (unsigned b=0; b<bytes_per_pixel; ++b) mouse_under[y][x][b]=p[b];
        if (mouse_shape[y] & (1u<<x)) put_pixel(px,py,0x11131a);
        else if ((x && (mouse_shape[y] & (1u<<(x-1)))) ||
                 (y && (mouse_shape[y-1] & (1u<<x)))) put_pixel(px,py,0xf4f5fa);
        else if (x<11 && (mouse_shape[y] & (1u<<(x+1)))) put_pixel(px,py,0xf4f5fa);
    }
    mouse_drawn=1;
}
static void fill(unsigned x, unsigned y, unsigned w, unsigned h, uint32_t color) {
    if (x >= width || y >= height) return;
    if (x+w > width) w=width-x;
    if (y+h > height) h=height-y;
    for (unsigned yy=0; yy<h; yy++)
        for (unsigned xx=0; xx<w; xx++) put_pixel(x+xx,y+yy,color);
}
static void glyph(unsigned cell_x, unsigned cell_y, uint8_t ch) {
    if (ch < 32 || ch >= 127) return;
    const FontGlyph *g=&font_glyphs[0][ch-32];
    unsigned ox=cell_x*12+(12-g->advance)/2, oy=cell_y*20+2;
    for (unsigned y=0;y<g->height;y++) for (unsigned x=0;x<g->width;x++) {
        unsigned index=y*g->width+x;
        uint8_t packed=font_coverage[g->offset+index/2];
        unsigned coverage=(index&1)?packed&15:packed>>4;
        if (coverage) put_pixel(ox+x,oy+y,coverage>5?0xe8e8ef:0x717789);
    }
}
static void glyph_pixels(unsigned px, unsigned py, uint8_t ch, uint32_t color) {
    if (ch < 32 || ch >= 127) return;
    const FontGlyph *g=&font_glyphs[0][ch-32];
    for (unsigned y=0;y<g->height;y++) for (unsigned x=0;x<g->width;x++) {
        unsigned index=y*g->width+x;
        uint8_t packed=font_coverage[g->offset+index/2];
        unsigned coverage=(index&1)?packed&15:packed>>4;
        if (coverage>5) put_pixel(px+x,py+y,color);
    }
}
static void scroll(void) {
    window64_console_damage(0,0,width,height);
    unsigned line=20;
    for (unsigned y=0;y+line<height;y++)
        for (unsigned x=0;x<byte_pitch;x++) pixels[y*byte_pitch+x]=pixels[(y+line)*byte_pitch+x];
    fill(0,height-line,width,line,0x11131a);
    cy=rows-1;
}
static void newline(void) { cx=0; if (++cy>=rows) scroll(); }
static void clear_screen(void) {
    window64_console_damage(0,0,width,height);
    fill(0,0,width,height,0x11131a); cx=cy=0;
}
static void ansi(uint8_t final) {
    unsigned value=esc_value? (unsigned)esc_value:1;
    if (final=='D') cx=value>cx?0:cx-value;
    else if (final=='C') { cx+=value; if (cx>=columns) cx=columns-1; }
    else if (final=='K') {
        window64_console_damage(cx*12,cy*20,width-cx*12,20);
        fill(cx*12,cy*20,width-cx*12,20,0x11131a);
    }
    else if (final=='H') cx=cy=0;
    else if (final=='J' && esc_value==2) clear_screen();
    esc_state=esc_value=0;
}
void console_fb_write(const char *data, size_t length) {
    if (!ready) return;
    mouse_erase();
    window64_console_begin();
    for (size_t i=0;i<length;i++) {
        uint8_t ch=(uint8_t)data[i];
        if (esc_state==1) { if (ch=='[') { esc_state=2; esc_value=0; } else esc_state=0; continue; }
        if (esc_state==2) {
            if (ch>='0'&&ch<='9') { esc_value=esc_value*10+ch-'0'; continue; }
            ansi(ch); continue;
        }
        if (ch==0x1b) { esc_state=1; continue; }
        if (ch=='\r') { cx=0; continue; }
        if (ch=='\n') { newline(); continue; }
        if (ch=='\b') { if (cx) cx--; continue; }
        if (ch<' '||ch>=127) continue;
        window64_console_damage(cx*12,cy*20,12,20);
        fill(cx*12,cy*20,12,20,0x11131a); glyph(cx,cy,ch);
        if (++cx>=columns) newline();
    }
    window64_console_end();
    mouse_draw();
}
void console_fb_mouse_enable(void) { mouse_visible=1; mouse_draw(); }
void console_fb_mouse_move(int x, int y) {
    if (!ready) return;
    mouse_erase();
    if (x<0) x=0; else if ((unsigned)x>=width) x=(int)width-1;
    if (y<0) y=0; else if ((unsigned)y>=height) y=(int)height-1;
    mouse_x=x; mouse_y=y;
    mouse_draw();
}
unsigned console_fb_width(void) { return width; }
unsigned console_fb_height(void) { return height; }
void console_fb_overlay_begin(void) { mouse_erase(); }
void console_fb_overlay_end(void) { mouse_draw(); }
int console_fb_read_pixels(unsigned x, unsigned y, unsigned count, uint32_t *out) {
    if (!ready || !out || y>=height || x>width || count>width-x) return 0;
    for (unsigned i=0;i<count;i++) {
        volatile uint8_t *p=pixels+y*byte_pitch+(x+i)*bytes_per_pixel;
        out[i]=(uint32_t)p[2]<<16|(uint32_t)p[1]<<8|p[0];
    }
    return 1;
}
int console_fb_write_pixels(unsigned x, unsigned y, unsigned count, const uint32_t *in) {
    if (!ready || !in || y>=height || x>width || count>width-x) return 0;
    for (unsigned i=0;i<count;i++) put_pixel(x+i,y,in[i]);
    return 1;
}
void console_fb_draw_window_frame(unsigned x, unsigned y, unsigned w, unsigned h,
                                  const char *title) {
    if (!ready || !title || !w || !h || x>=width || y>=height || w>width-x || h>height-y)
        return;
    fill(x,y,w,h,0x777b88);
    fill(x+1,y+1,w-2,h-2,0x191b22);
    fill(x+2,y+2,w-4,28,0x2b2e39);
    fill(x+2,y+30,w-4,h-32,0x20232c);
    fill(x+w-19,y+10,10,10,0xd95755);
    fill(x+w-16,y+14,4,2,0x711f23);
    unsigned tx=x+10;
    for (unsigned i=0;title[i] && tx+8<x+w-28;i++,tx+=9)
        glyph_pixels(tx,y+7,(uint8_t)title[i],0xe8e9ef);
}
int console_fb_init(const volatile uint8_t *vbe) {
    uint16_t attributes=*(const volatile uint16_t *)(vbe+0);
    width=*(const volatile uint16_t *)(vbe+18);
    height=*(const volatile uint16_t *)(vbe+20);
    bytes_per_pixel=vbe[25]/8;
    byte_pitch=*(const volatile uint16_t *)(vbe+16);
    phys_addr_t physical=*(const volatile uint32_t *)(vbe+40);
    if ((attributes&0x91)!=0x91 || !physical || (bytes_per_pixel!=3&&bytes_per_pixel!=4) ||
        width<640 || height<400 || byte_pitch<width*bytes_per_pixel) return 0;
    uint64_t size=(uint64_t)byte_pitch*height;
    phys_addr_t first=physical&~(MM_PAGE_SIZE-1);
    uint64_t offset=physical-first;
    uint64_t pages=(offset+size+MM_PAGE_SIZE-1)/MM_PAGE_SIZE;
    for (uint64_t i=0;i<pages;i++)
        if (vmm64_map_borrowed(vmm64_kernel(),FB_VIRTUAL+i*MM_PAGE_SIZE,first+i*MM_PAGE_SIZE,VM_WRITE|VM_DEVICE)!=VM_OK) return 0;
    pixels=(volatile uint8_t *)(uintptr_t)(FB_VIRTUAL+offset);
    columns=width/12; rows=height/20;
    if (!columns || !rows) return 0;
    ready=1; clear_screen();
    static const char banner[]="PollikOS x86_64 graphical console\n";
    console_fb_write(banner,sizeof(banner)-1);
    return 1;
}
