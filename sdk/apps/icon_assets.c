#include "icon_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_THREAD_LOCALS
#define STBI_EXTERNAL_RUNTIME
#define STBI_MAX_DIMENSIONS 64
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "../../third_party/stb/stb_image.h"
#pragma clang diagnostic pop
static unsigned char *images[8];
void pollik_assets_init(void){
    const char *names[]={"welcome","files","terminal","notes","settings","browser","pollikmark","calculator"};
    for(int i=0;i<8;i++){
        char path[80];snprintf(path,sizeof path,"/usr/share/icons/%s.png",names[i]);
        struct stat s;if(stat(path,&s)||s.st_size<33||s.st_size>65536)continue;
        FILE *f=fopen(path,"rb");if(!f)continue;
        unsigned char *data=malloc((size_t)s.st_size);if(!data){fclose(f);continue;}
        size_t count=fread(data,1,(size_t)s.st_size,f);fclose(f);
        const unsigned char signature[]={137,80,78,71,13,10,26,10};int w=0,h=0,channels;
        if(count==(size_t)s.st_size&&!memcmp(data,signature,8)&&!memcmp(data+12,"IHDR",4)&&
           !data[16]&&!data[17]&&!data[18]&&data[19]==64&&
           !data[20]&&!data[21]&&!data[22]&&data[23]==64)
            images[i]=stbi_load_from_memory(data,(int)count,&w,&h,&channels,4);
        free(data);if(images[i]&&(w!=64||h!=64)){stbi_image_free(images[i]);images[i]=0;}
    }
}
void pollik_assets_destroy(void){for(int i=0;i<8;i++){stbi_image_free(images[i]);images[i]=0;}}
void pollik_assets_icon(PollikCanvas *c,int id,int x,int y,int size){
    if(id<0||id>=8||size<=0)return;
    if(!images[id]){pollik_ui_fill(c,x,y,size,size,0x777089);return;}
    unsigned step=(64u<<16)/(unsigned)size;
    for(int row=0;row<size;row++)for(int col=0;col<size;col++){
        int px=x+col,py=y+row;if(px<0||py<0||(unsigned)px>=c->width||(unsigned)py>=c->height)continue;
        const unsigned char *s=images[id]+(((unsigned)row*step>>16)*64+((unsigned)col*step>>16))*4;
        uint32_t rgb=((uint32_t)s[0]<<16)|((uint32_t)s[1]<<8)|s[2],old=c->pixels[(size_t)py*c->width+px];
        unsigned a=s[3];if(!a)continue;
        unsigned r=(((rgb>>16)&255)*a+((old>>16)&255)*(255-a)+127)/255;
        unsigned g=(((rgb>>8)&255)*a+((old>>8)&255)*(255-a)+127)/255;
        unsigned b=((rgb&255)*a+(old&255)*(255-a)+127)/255;
        c->pixels[(size_t)py*c->width+px]=(r<<16)|(g<<8)|b;
    }
}
