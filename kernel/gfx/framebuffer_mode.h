#ifndef POLLIK_FRAMEBUFFER_MODE_H
#define POLLIK_FRAMEBUFFER_MODE_H
#include <stdint.h>
/* Boot VBE contract supported by the BGR linear framebuffer driver. */
static inline int gfx_framebuffer_mode_valid(unsigned attributes,unsigned width,unsigned height,
 unsigned bpp,unsigned pitch,uint32_t physical,unsigned model,unsigned red_size,unsigned red_pos,
 unsigned green_size,unsigned green_pos,unsigned blue_size,unsigned blue_pos){
 return (attributes&0x91)==0x91&&physical&&width>=640&&height>=400&&
   width<=8192&&height<=8192&&(bpp==24||bpp==32)&&pitch>=width*(bpp/8)&&
   (uint64_t)pitch*height<=UINT64_C(256)*1024*1024&&model==6&&
   red_size==8&&red_pos==16&&green_size==8&&green_pos==8&&blue_size==8&&blue_pos==0;
}
#endif
