#include <pollikos/media.h>
#include <stdio.h>
#include <stdint.h>
int main(int argc,char **argv){
 if(argc!=3)return 2;
 pollikos_audio_decoder *d=pollikos_media_open_file(argv[1]);
 if(!d){puts("REJECT unsupported/invalid audio");return 3;}
 FILE *out=fopen(argv[2],"wb");if(!out){pollikos_media_close(d);return 4;}
 int16_t samples[2048];uint64_t total=0;size_t count;
 while((count=pollikos_media_read(d,samples,1024))!=0){if(fwrite(samples,4,count,out)!=count)return 5;total+=count;}
 printf("DECODE %s source_rate=%u output_rate=48000 stereo_frames=%llu\n",pollikos_media_format(d),pollikos_media_source_rate(d),(unsigned long long)total);
 fclose(out);pollikos_media_close(d);return 0;
}
