/* Real x64 save-under/overlay functions; kernel-only init is discarded. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/arch/x86_64/console_fb.c"
/* External kernel services are unreachable in this pixel-only test. */
void window64_console_begin(void){assert(0);}
void window64_console_end(void){assert(0);}
void window64_console_damage(unsigned x,unsigned y,unsigned w,unsigned h){(void)x;(void)y;(void)w;(void)h;assert(0);}
AddressSpace *vmm64_kernel(void){assert(0);return NULL;}
VmResult vmm64_alloc_page(AddressSpace *space,virt_addr_t va,unsigned flags){(void)space;(void)va;(void)flags;assert(0);return VM_INVALID;}
VmResult vmm64_map_borrowed(AddressSpace *space,virt_addr_t va,phys_addr_t pa,unsigned flags){(void)space;(void)va;(void)pa;(void)flags;assert(0);return VM_INVALID;}
VmResult vmm64_unmap(AddressSpace *space,virt_addr_t va,int release,phys_addr_t *old){(void)space;(void)va;(void)release;(void)old;assert(0);return VM_INVALID;}
void memory_require(int condition,const char *message){(void)condition;(void)message;assert(0);}
void account_wipe(void *memory,size_t length){(void)memory;(void)length;assert(0);}
int main(void){
    unsigned char framebuffer[67*4*49];unsigned expected[67*49];
    pixels=framebuffer;width=64;height=49;byte_pitch=67*4;bytes_per_pixel=4;ready=1;
    for(unsigned i=0;i<67*49;i++)expected[i]=(i*6173)&0xffffff;
    memcpy(framebuffer,expected,sizeof(expected));
    console_fb_mouse_move(10,10);console_fb_mouse_enable();assert(mouse_drawn);
    console_fb_overlay_begin();assert(!mouse_drawn);assert(!memcmp(framebuffer,expected,sizeof(expected)));
    console_fb_mouse_move(30,22);assert(!mouse_drawn);
    console_fb_overlay_begin();for(unsigned i=0;i<67*49;i++)expected[i]^=0x1f3571;memcpy(framebuffer,expected,sizeof(expected));
    console_fb_mouse_move(50,35);console_fb_overlay_end();assert(!mouse_drawn);
    console_fb_overlay_end();assert(mouse_drawn);console_fb_overlay_begin();assert(!memcmp(framebuffer,expected,sizeof(expected)));console_fb_overlay_end();
    puts("PASS x64 cursor: input during nested repaint preserves old/new frames, padded pitch and screen edges");
    for(unsigned w=1;w<=39;w++)for(unsigned h=1;h<=31;h++){
        unsigned sw=(w+1)/2,sh=(h+1)/2,expanded[39*31],small[20*16];
        for(unsigned i=0;i<sw*sh;i++)expanded[i]=small[i]=(i*719537+0x714189)&0xffffff;
        pollik_expand_blur2(expanded,w,h);
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
            unsigned sx=x/2,sy=y/2,nx=sx+1<sw?sx+1:sx,ny=sy+1<sh?sy+1:sy;
            unsigned a=pollik_color_mix(small[sy*sw+sx],small[sy*sw+nx],(x&1)*128);
            unsigned b=pollik_color_mix(small[ny*sw+sx],small[ny*sw+nx],(x&1)*128);
            assert(expanded[y*w+x]==pollik_color_mix(pollik_color_mix(a,b,(y&1)*128),0x100b20,112));
        }
    }
    puts("PASS cached blur: in-place expansion equals separate bilinear reference for 1209 odd/even dimensions");return 0;
}
