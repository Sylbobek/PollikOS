#define _CRT_SECURE_NO_WARNINGS 1
#include "../sdk/apps/window_ui.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    uint32_t pixels[328*88];PollikCanvas c={pixels,328,88};
    pollik_ui_fill(&c,0,0,328,88,0x23293a);
    pollik_ui_text(&c,12,44,argv[1],0xf1f2fa);
    FILE *f=fopen(argv[2],"wb");if(!f)return 3;
    fprintf(f,"P6\n328 88\n255\n");
    for(unsigned i=0;i<328*88;i++) {
        unsigned char rgb[]={(unsigned char)(pixels[i]>>16),(unsigned char)(pixels[i]>>8),(unsigned char)pixels[i]};
        if(fwrite(rgb,1,3,f)!=3)return 4;
    }
    return fclose(f)!=0;
}
