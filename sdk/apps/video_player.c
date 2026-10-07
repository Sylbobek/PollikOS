#include <pollikos/movie.h>
#include <pollikos/audio.h>
#include <pollikos/time.h>
#include <pollikos/window.h>
#include <stdio.h>
#include <string.h>
#include "window_ui.h"
static PollikCanvas canvas;static pollikos_movie *movie;static int headless,paused,running=1,audio_owned;
static uint64_t submitted,pts,shown,last_pts;static unsigned count;static int16_t pcm[2048];static unsigned buffered;
static void draw(void){
 if(headless)return;pollik_ui_fill(&canvas,0,0,720,580,0x111725);
 unsigned w=pollikos_movie_width(movie),h=pollikos_movie_height(movie);const uint32_t *image=pollikos_movie_rgb(movie);
 int dw=640,dh=(int)((uint64_t)h*640/w);if(dh>480){dh=480;dw=(int)((uint64_t)w*480/h);}
 int left=(720-dw)/2,top=42+(480-dh)/2;
 for(int y=0;y<dh;y++)for(int x=0;x<dw;x++)canvas.pixels[(size_t)(top+y)*canvas.width+left+x]=image[(size_t)(y*h/dh)*w+x*w/dw];
 pollik_ui_text(&canvas,24,12,"MP4 Player",0xf0f2f7);
 pollik_ui_text(&canvas,24,546,paused?"Paused - Space resumes":"Space: Pause   Esc: Close",0xb4bdcf);
 (void)pollikos_window_present();
}
int main(int argc,char **argv){
 unsigned arg=1;if(argc>1&&!strcmp(argv[1],"--headless")){headless=1;arg++;}
 const char *path=arg<(unsigned)argc?argv[arg]:"/usr/share/videos/demo.mp4";
 movie=pollikos_movie_open_file(path);if(!movie){puts("[video] unsupported or invalid MP4 (H264 Baseline/AAC-LC required)");return 2;}
 puts("Code from FAAD2 is copyright (c) Nero AG, www.nero.com");
 if(!headless){long p=pollikos_window_create(720,580,"MP4 Player");if(p<0){pollikos_movie_close(movie);return 3;}canvas=(PollikCanvas){(uint32_t *)(uintptr_t)p,720,580};}
 int have=pollikos_movie_next_video(movie,&pts);if(have!=1){pollikos_movie_close(movie);return 4;}
 if(pollikos_movie_audio_rate(movie)){long r=pollikos_audio_begin();if(r<0){printf("[video] PCM output unavailable=%ld\n",r);pollikos_movie_close(movie);if(!headless)pollikos_window_destroy();return 5;}audio_owned=1;}
 unsigned width=pollikos_movie_width(movie),height=pollikos_movie_height(movie);
 printf("[video] ready %ux%u frames=%u AAC_rate=%u\n",width,height,pollikos_movie_video_count(movie),pollikos_movie_audio_rate(movie));
 long started=pollikos_monotonic_ms(),pause_at=0,audio_finished=-1;int audio_eof=!audio_owned,error=0;
 while(running){
  long pending=audio_owned?pollikos_audio_pending():0;if(pending<0){error=1;break;}
  if(!paused&&audio_owned&&!audio_eof){
   if(!buffered)buffered=(unsigned)pollikos_movie_read_audio(movie,pcm,1024);
   if(buffered){long r=pollikos_audio_submit(pcm,buffered);if(r>0){submitted+=(unsigned)r;buffered=0;}else if(r!=-USER_EAGAIN){error=1;break;}}
   else {if(pollikos_movie_error(movie)){error=1;break;}audio_eof=1;}
  }
  if(audio_owned){pending=pollikos_audio_pending();if(pending<0){error=1;break;}}
  uint64_t now=audio_owned?(submitted-(uint64_t)pending)*1000/48000:(uint64_t)(pollikos_monotonic_ms()-started);
  if(audio_owned&&audio_eof&&!pending){if(audio_finished<0)audio_finished=pollikos_monotonic_ms();now+=(uint64_t)(pollikos_monotonic_ms()-audio_finished);}
  if(!paused&&have&&pts<=now){
   count++;last_pts=pts;
   if(headless){shown++;printf("[video] frame=%u pts_ms=%llu\n",count,(unsigned long long)pts);}
   else if(now-pts<=125){draw();shown++;}
   have=pollikos_movie_next_video(movie,&pts);if(have<0){error=1;break;}
  }
  if(!have&&audio_eof&&!pending)break;
  if(!headless){pollikos_input_event_t e;if(pollikos_input_read(&e)==(int64_t)sizeof(e)){
   if(e.kind&POLLIKOS_INPUT_WINDOW_CLOSE)running=0;
   if(e.kind&POLLIKOS_INPUT_KEY_DOWN){
    if(e.key==27)running=0;
    if(e.key==' '){
     if(!audio_owned||pollikos_audio_pause(!paused)==0){paused=!paused;if(paused)pause_at=pollikos_monotonic_ms();else started+=pollikos_monotonic_ms()-pause_at;}
    }
   }
  }}
  (void)pollikos_sleep_ms(1);
 }
 if(audio_owned)pollikos_audio_stop();pollikos_movie_close(movie);if(!headless)pollikos_window_destroy();
 printf("[video] %s decoded=%u shown=%llu last_pts_ms=%llu PCM_frames=%llu\n",error?"error":"complete",count,(unsigned long long)shown,(unsigned long long)last_pts,(unsigned long long)submitted);
 return error?6:0;
}
