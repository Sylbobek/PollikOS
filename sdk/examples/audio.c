#include <pollikos/media.h>
#include <pollikos/audio.h>
#include <pollikos/time.h>
#include <stdio.h>
int main(int argc,char **argv){
 if(argc!=2){puts("usage: audio.pol /path/file.wav-or-mp3");return 1;}
 pollikos_audio_decoder *decoder=pollikos_media_open_file(argv[1]);if(!decoder)return 2;
 if(pollikos_audio_begin()<0){pollikos_media_close(decoder);return 3;}
 int16_t samples[2048];size_t n;
 while((n=pollikos_media_read(decoder,samples,1024))!=0){
  long result;
  while((result=(long)pollikos_audio_submit(samples,(unsigned)n))==-USER_EAGAIN)pollikos_sleep_ms(10);
  if(result<0){pollikos_audio_stop();pollikos_media_close(decoder);return 4;}
 }
 long pending;while((pending=(long)pollikos_audio_pending())>0)pollikos_sleep_ms(10);
 long stopped=(long)pollikos_audio_stop();pollikos_media_close(decoder);return pending<0||stopped<0?5:0;
}
