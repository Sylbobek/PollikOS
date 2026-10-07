#include <pollikos/media.h>
#include <pollikos/audio.h>
#include <pollikos/devices.h>
#include <pollikos/window.h>
#include <pollikos/time.h>
#include <stdio.h>
#include <string.h>
#include "window_ui.h"
static PollikCanvas canvas;
static pollikos_audio_decoder *decoder;
static int running=1,playing,paused,headless;
static int16_t block[2048];static unsigned buffered;
static uint64_t submitted;
static char path[USER_PATH_MAX]="/usr/share/sounds/demo.wav",status[96]="Ready";
static void paint(void){
 if(headless)return;
 pollik_ui_fill(&canvas,0,0,520,300,0x161b28);
 pollik_ui_text(&canvas,24,22,"Media Player",0xf0f2f7);
 pollik_ui_text(&canvas,24,58,path,0xa9b2c4);
 pollik_ui_text(&canvas,24,94,status,0xbaa8f4);
 char text[64];snprintf(text,sizeof(text),"Played: %llu s",(unsigned long long)(submitted/48000));
 pollik_ui_text(&canvas,24,130,text,0xf0f2f7);
 for(int i=0;i<3;i++){
  pollik_ui_fill(&canvas,24+i*156,198,140,48,i==1&&paused?0x8872d4:0x34394d);
  pollik_ui_text(&canvas,54+i*156,213,i==0?"Play":i==1?(paused?"Resume":"Pause"):"Stop",0xf0f2f7);
 }
 pollik_ui_text(&canvas,24,268,"WAV / MP3",0xa9b2c4);(void)pollikos_window_present();
}
static void stop(void){
 if(playing)(void)pollikos_audio_stop();
 playing=paused=buffered=0;pollikos_media_close(decoder);decoder=0;
}
static int play(void){
 stop();submitted=0;
 decoder=pollikos_media_open_file(path);
 if(!decoder){strcpy(status,"Unsupported, missing or invalid audio");paint();return 0;}
 long result=pollikos_audio_begin();
 if(result<0){snprintf(status,sizeof(status),"PCM output unavailable (%ld)",result);pollikos_media_close(decoder);decoder=0;paint();return 0;}
 playing=1;snprintf(status,sizeof(status),"Playing %s (%u Hz)",pollikos_media_format(decoder),pollikos_media_source_rate(decoder));
 printf("[media] playing %s format=%s source_rate=%u output=48000 stereo\n",path,pollikos_media_format(decoder),pollikos_media_source_rate(decoder));paint();return 1;
}
static void action(int id){
 if(id==0)(void)play();
 else if(id==1&&playing){if(pollikos_audio_pause(!paused)==0){paused=!paused;strcpy(status,paused?"Paused":"Playing");paint();}}
 else if(id==2){stop();strcpy(status,"Stopped");paint();}
}
int main(int argc,char **argv){
 unsigned arg=1;if(argc>1&&!strcmp(argv[1],"--headless")){headless=1;arg++;}
 if(arg<(unsigned)argc){if(strlen(argv[arg])>=sizeof(path))return 2;strcpy(path,argv[arg]);}
 if(!headless){
  long mapped=pollikos_window_create(520,300,"Media Player");if(mapped<0)return 3;
  canvas=(PollikCanvas){(uint32_t *)(uintptr_t)mapped,520,300};paint();
 }
 if(!play()&&headless)return 4;
 while(running){
  if(playing&&!paused){
   if(!buffered)buffered=(unsigned)pollikos_media_read(decoder,block,1024);
   if(buffered){
    long n=pollikos_audio_submit(block,buffered);
    if(n>0){submitted+=(unsigned)n;buffered=0;}
    else if(n!=-USER_EAGAIN){printf("[media] audio error=%ld\n",n);stop();strcpy(status,"Audio output error");paint();if(headless)return 5;}
   }else{
    long remaining=pollikos_audio_pending();
    if(remaining<0){stop();strcpy(status,"Audio output error");paint();if(headless)return 5;}
    else if(!remaining){printf("[media] complete frames=%llu\n",(unsigned long long)submitted);stop();strcpy(status,"Playback complete");paint();if(headless)running=0;}
   }
  }
  if(!headless){
   pollikos_input_event_t e;
   if(pollikos_input_read(&e)==(int64_t)sizeof(e)){
    if(e.kind&POLLIKOS_INPUT_WINDOW_CLOSE)running=0;
    if(e.kind&POLLIKOS_INPUT_KEY_DOWN){if(e.key==27)running=0;else if(e.key==' '&&playing)action(1);else if(e.key==13)action(0);}
    if((e.kind&POLLIKOS_INPUT_MOUSE_BUTTON)&&(e.changed&POLLIKOS_MOUSE_LEFT)&&(e.buttons&POLLIKOS_MOUSE_LEFT)){
     pollikos_window_info_t w;if(!pollikos_window_info(&w)){
      int x=e.x-w.content_x,y=e.y-w.content_y;
      if(y>=198&&y<246&&x>=24&&x<492)action((x-24)/156);
     }
    }
   }
  }
  (void)pollikos_sleep_ms(1);
 }
 stop();if(!headless)(void)pollikos_window_destroy();return 0;
}
