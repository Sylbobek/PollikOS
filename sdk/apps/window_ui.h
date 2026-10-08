#ifndef POLLIKOS_WINDOW_UI_H
#define POLLIKOS_WINDOW_UI_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../../common/utf8.h"

typedef uint8_t u8;
typedef uint32_t u32;
#include "../../kernel/font_data.h"

typedef struct { uint32_t *pixels; unsigned width, height; } PollikCanvas;

static inline void pollik_ui_fill(PollikCanvas *canvas,int x,int y,int width,int height,
                                  uint32_t color) {
    if (x<0) { width+=x; x=0; }
    if (y<0) { height+=y; y=0; }
    if (width<=0 || height<=0 || (unsigned)x>=canvas->width || (unsigned)y>=canvas->height) return;
    if ((unsigned)(x+width)>canvas->width) width=(int)canvas->width-x;
    if ((unsigned)(y+height)>canvas->height) height=(int)canvas->height-y;
    for (int row=0;row<height;row++) {
        uint32_t *target=canvas->pixels+(size_t)(y+row)*canvas->width+x;
        for (int col=0;col<width;col++) target[col]=color;
    }
}

static inline const FontGlyph *pollik_ui_glyph(uint32_t codepoint) {
    if(codepoint>=32 && codepoint<127)return &font_glyphs[0][codepoint-32];
    for(unsigned i=95;i<sizeof(font_codepoints)/sizeof(font_codepoints[0]);i++)
        if(font_codepoints[i]==codepoint)return &font_glyphs[0][i];
    return &font_glyphs[0]['?'-32];
}
static inline int pollik_ui_text_width(const char *text) {
    size_t remaining=strlen(text);int width=0;
    while(remaining){size_t used;uint32_t codepoint=pollik_utf8_next(text,remaining,&used);
        width+=codepoint<32?7:pollik_ui_glyph(codepoint)->advance+2;text+=used;remaining-=used;}
    return width;
}
static inline void pollik_ui_text(PollikCanvas *canvas,int x,int y,const char *text,uint32_t color) {
    size_t remaining=strlen(text);
    while (remaining) {
        size_t used;uint32_t ch=pollik_utf8_next(text,remaining,&used);text+=used;remaining-=used;
        if (ch<32) { x+=7; continue; }
        const FontGlyph *glyph=pollik_ui_glyph(ch);
        int origin=x+(glyph->advance-glyph->width)/2;
        for (unsigned row=0;row<glyph->height;row++) for (unsigned col=0;col<glyph->width;col++) {
            unsigned index=row*glyph->width+col;
            uint8_t packed=font_coverage[glyph->offset+index/2];
            unsigned coverage=(index&1)?packed&15:packed>>4;
            if (coverage>=6) {
                int px=origin+(int)col, py=y+(int)row;
                if (px>=0 && py>=0 && (unsigned)px<canvas->width && (unsigned)py<canvas->height)
                    canvas->pixels[(size_t)py*canvas->width+px]=color;
            }
        }
        x+=glyph->advance+2;
    }
}

static inline void pollik_ui_icon(PollikCanvas *canvas,int x,int y,int color,int folder) {
    if (folder) {
        pollik_ui_fill(canvas,x+3,y+4,15,5,0xf4c46a);
        pollik_ui_fill(canvas,x+1,y+8,24,17,0xd99e44);
        pollik_ui_fill(canvas,x+3,y+10,20,13,0xf0bd5b);
    } else {
        pollik_ui_fill(canvas,x+4,y+2,18,24,0x4a5873);
        pollik_ui_fill(canvas,x+7,y+6,12,2,(uint32_t)color);
        pollik_ui_fill(canvas,x+7,y+11,12,2,(uint32_t)color);
        pollik_ui_fill(canvas,x+7,y+16,9,2,(uint32_t)color);
    }
}
#endif
