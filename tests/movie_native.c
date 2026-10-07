#include <pollikos/movie.h>
#include <stdio.h>
int main(int argc,char **argv){
 if(argc!=4)return 2;pollikos_movie *m=pollikos_movie_open_file(argv[1]);if(!m){puts("REJECT MP4");return 3;}
 FILE *video=fopen(argv[2],"wb"),*audio=fopen(argv[3],"wb");if(!video||!audio)return 4;
 unsigned w=pollikos_movie_width(m),h=pollikos_movie_height(m),count=0;uint64_t pts;int result;
 while((result=pollikos_movie_next_video(m,&pts))>0){
  fwrite(pollikos_movie_yuv(m),1,(size_t)w*h*3/2,video);printf("FRAME %u pts_ms=%llu\n",count++,(unsigned long long)pts);
 }
 if(result<0){puts("ERROR video decode");return 5;}
 int16_t samples[2048];size_t frames,total=0;
 while((frames=pollikos_movie_read_audio(m,samples,1024))!=0){fwrite(samples,4,frames,audio);total+=frames;}
 if(pollikos_movie_error(m)){puts("ERROR audio decode");return 6;}
 printf("MOVIE %ux%u frames=%u audio_rate=%u stereo_frames=%zu\n",w,h,count,pollikos_movie_audio_rate(m),total);
 fclose(video);fclose(audio);pollikos_movie_close(m);return 0;
}
