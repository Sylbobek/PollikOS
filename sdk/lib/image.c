#include <pollikos/image.h>
#include <stdlib.h>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_EXTERNAL_RUNTIME
#define STBI_MAX_DIMENSIONS 4096
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include <pollikos/stb_image.h>
#pragma clang diagnostic pop
unsigned char *pollikos_image_decode(const void *data,size_t bytes,int *w,int *h){
 if(w)*w=0;if(h)*h=0;if(!data||!bytes||bytes>4u*1024u*1024u||!w||!h)return 0;
 int width,height,channels;
 if(!stbi_info_from_memory(data,(int)bytes,&width,&height,&channels)||width<1||height<1||width>4096||height>4096||
    (unsigned)width*(unsigned)height>16u*1024u*1024u)return 0;
 unsigned char *pixels=stbi_load_from_memory(data,(int)bytes,&width,&height,&channels,4);
 if(pixels){*w=width;*h=height;}return pixels;
}
void pollikos_image_free(void *p){stbi_image_free(p);}
