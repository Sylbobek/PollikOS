#ifndef POLLIKMARK_H
#define POLLIKMARK_H
#include "../system.h"
extern u8 pollikmark_icon[256], pollikmark_alpha[128];
extern const u32 pollikmark_palette[4];
void pollikmark_init(void);
void pollikmark_open(void);
void pollikmark_close(void);
void pollikmark_resize(int width,int height);
void pollikmark_render(int width,int height,int active);
void pollikmark_key(u8 code,char ch,int shift,int control);
void pollikmark_click(int x,int y);
int pollikmark_poll(void);
#endif
