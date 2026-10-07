#include <pollikos/movie.h>
#include <pollikos/audio.h>
#include <pollikos/devices.h>
#include <pollikos/time.h>
#include <stdio.h>
int main(int argc,char **argv){
 if(argc!=2)return 2;pollikos_movie *m=pollikos_movie_open_file(argv[1]);if(!m){puts("REJECT native MP4");return 3;}
 unsigned w=pollikos_movie_width(m),h=pollikos_movie_height(m),n=0;uint64_t pts;int r;
 while((r=pollikos_movie_next_video(m,&pts))==1){
  const unsigned char *bytes=pollikos_movie_yuv(m);uint32_t hash=2166136261u;
  for(size_t i=0;i<(size_t)w*h*3/2;i++)hash=(hash^bytes[i])*16777619u;
  printf("VIDEO_ORACLE frame=%u pts_ms=%llu hash=%08x\n",n++,(unsigned long long)pts,hash);
 }
 if(r<0){pollikos_movie_close(m);return 4;}
 pollikos_movie_close(m);m=pollikos_movie_open_file(argv[1]);if(!m)return 5;
 if(pollikos_device(USER_DEVICE_VOLUME_SET,100)||pollikos_device(USER_DEVICE_OUTPUT_SET,1)||pollikos_audio_begin())return 6;
 int16_t out[2048];size_t frames,total=0;
 while((frames=pollikos_movie_read_audio(m,out,1024))!=0){
  long accepted;while((accepted=(long)pollikos_audio_submit(out,(unsigned)frames))==-USER_EAGAIN)pollikos_sleep_ms(10);
  if(accepted<0){pollikos_audio_stop();pollikos_movie_close(m);return 7;}total+=frames;
 }
 long pending;while((pending=(long)pollikos_audio_pending())>0)pollikos_sleep_ms(10);
 int decode_error=pollikos_movie_error(m);
 long stopped=(long)pollikos_audio_stop();pollikos_movie_close(m);
 if(pending<0||stopped<0||decode_error)return 8;
 printf("PASS native MP4 decoded_frames=%u PCM_frames=%zu\n",n,total);return 0;
}
