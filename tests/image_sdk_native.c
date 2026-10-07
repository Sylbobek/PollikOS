#include <pollikos/image.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){
 if(argc!=3)return 2;FILE *f=fopen(argv[1],"rb");if(!f)return 3;
 fseek(f,0,SEEK_END);long bytes=ftell(f);rewind(f);unsigned char *data=malloc((size_t)bytes);
 if(!data){fclose(f);return 4;}size_t n=fread(data,1,(size_t)bytes,f);fclose(f);
 int w,h;unsigned char *pixels=pollikos_image_decode(data,n,&w,&h);free(data);
 if(!pixels){puts("REJECT image");return 5;}
 f=fopen(argv[2],"wb");if(!f){pollikos_image_free(pixels);return 6;}
 fwrite(pixels,4,(size_t)w*h,f);fclose(f);pollikos_image_free(pixels);
 printf("IMAGE %dx%d RGBA\n",w,h);return 0;
}
