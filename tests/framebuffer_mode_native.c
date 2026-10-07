#include <stdio.h>
#include "../kernel/gfx/framebuffer_mode.h"
int main(void){
 int checks=0;
 for(unsigned bpp=24;bpp<=32;bpp+=8)for(unsigned h=768;h<=1080;h+=312){
  unsigned w=h==768?1024:1920,pitch=w*bpp/8;
  if(!gfx_framebuffer_mode_valid(0x91,w,h,bpp,pitch,0xe0000000,6,8,16,8,8,8,0))return 1;checks++;
  if(gfx_framebuffer_mode_valid(0x91,w,h,bpp,pitch-1,0xe0000000,6,8,16,8,8,8,0))return 2;checks++;
 }
 if(gfx_framebuffer_mode_valid(0x91,1024,768,31,4096,0xe0000000,6,8,16,8,8,8,0))return 3;
 if(gfx_framebuffer_mode_valid(0x91,1024,768,32,4096,0xe0000000,6,8,0,8,8,8,16))return 4;
 printf("PASS framebuffer geometry: %d valid/pitch vectors; reject 31bpp and swapped RGB masks\n",checks);return 0;
}
