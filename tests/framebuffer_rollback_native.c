#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/arch/x86_64/console_fb.c"
static int maps,unmaps,fail;
AddressSpace *vmm64_kernel(void){return 0;}
VmResult vmm64_map_borrowed(AddressSpace *s,virt_addr_t v,phys_addr_t p,unsigned f){
 (void)s;(void)v;(void)p;(void)f;if(maps==fail)return VM_NOMEM;maps++;return VM_OK;
}
VmResult vmm64_unmap(AddressSpace *s,virt_addr_t v,int release,phys_addr_t *p){
 (void)s;(void)v;(void)p;if(release)abort();unmaps++;return VM_OK;
}
void memory_require(int condition,const char *s){if(!condition){puts(s);abort();}}
void window64_console_begin(void){}void window64_console_end(void){}
void window64_console_damage(unsigned x,unsigned y,unsigned w,unsigned h){(void)x;(void)y;(void)w;(void)h;}
int main(void){
 unsigned char vbe[256]={0};unsigned short a=0x91,w=1024,h=768,pitch=4096;unsigned phys=0xe0000000;
 memcpy(vbe,&a,2);memcpy(vbe+16,&pitch,2);memcpy(vbe+18,&w,2);memcpy(vbe+20,&h,2);memcpy(vbe+40,&phys,4);
 vbe[25]=32;vbe[27]=6;vbe[31]=8;vbe[32]=16;vbe[33]=8;vbe[34]=8;vbe[35]=8;
 for(fail=0;fail<12;fail++){
  maps=unmaps=0;if(console_fb_init(vbe)||maps!=unmaps||console_fb_width()||console_fb_bpp())return 1;
 }
 puts("PASS framebuffer failure injection: 12 borrowed-map failures, mappings returned exactly to baseline");return 0;
}
