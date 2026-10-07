#ifndef POLLIK_NATIVE_CONTROL_PANEL_H
#define POLLIK_NATIVE_CONTROL_PANEL_H
#include <pollikos/devices.h>
#include <pollikos/time.h>
#include "window_ui.h"
/* Native Ring 3 overlay. All hardware access goes through the device syscall. */
static int panel_open,panel_list,panel_slide,panel_origin,panel_capture;
static long panel_started;
static void panel_toggle(void){panel_capture=0;panel_origin=panel_slide;panel_open=!panel_open;panel_started=pollikos_monotonic_ms();}
static int panel_tick(void){
 long elapsed=pollikos_monotonic_ms()-panel_started;if(elapsed>=200)elapsed=200;
 int inverse=256-(int)(elapsed*256/200),ease=256-((inverse*inverse*inverse)>>16);
 int target=panel_open?256:0,previous=panel_slide;
 panel_slide=panel_origin+(target-panel_origin)*ease/256;return panel_slide!=previous;
}
static void panel_round(PollikCanvas *c,int x,int y,int w,int h,unsigned color){
 for(int row=0;row<h;row++){int edge=row<10?10-row:row>=h-10?row-(h-11):0;
  int inset=0;while(inset<10&&edge*edge+(10-inset)*(10-inset)>100)inset++;
  pollik_ui_fill(c,x+inset,y+row,w-inset*2,1,color);
 }
}
static void panel_draw(PollikCanvas *c){
 if(!panel_slide||c->height<=38)return;
 PollikCanvas clipped={c->pixels+38*c->width,c->width,c->height-38};c=&clipped;
 int x=(int)c->width/2-190,y=2-(330*(256-panel_slide)/256);
 /* The canvas clips each span. Restore menu bar after the panel slides up. */
 panel_round(c,x,y,380,330,0x252a3b);
 pollik_ui_text(c,x+18,y+12,"Control Center",0xf0f2f7);
 for(int i=0;i<2;i++){
  panel_round(c,x+12+i*180,y+42,176,68,0x34394d);
  pollik_ui_text(c,x+24+i*180,y+50,i?"Bluetooth":"Wi-Fi",0xf0f2f7);
  pollik_ui_text(c,x+24+i*180,y+76,"No adapter",0xb4bdcf);
  panel_round(c,x+155+i*180,y+50,24,26,0x494c63);
  pollik_ui_text(c,x+163+i*180,y+52,">",0xffffff);
 }
 int blocked=(int)pollikos_device(USER_DEVICE_AIRPLANE_GET,0);
 panel_round(c,x+12,y+120,356,34,blocked?0x6854bf:0x34394d);
 pollik_ui_text(c,x+24,y+127,"Airplane mode",0xf0f2f7);
 pollik_ui_text(c,x+300,y+127,blocked?"On":"Off",0xf0f2f7);
 if(panel_list==1||panel_list==2){
  pollik_ui_text(c,x+24,y+167,panel_list==1?"Wi-Fi networks":"Bluetooth devices",0xf0f2f7);
  pollik_ui_text(c,x+24,y+192,"No supported adapter",0xb4bdcf);
 }else pollik_ui_text(c,x+24,y+172,blocked?"Internet blocked":pollikos_device(USER_DEVICE_CONNECTED,0)>0?"Ethernet connected":"Ethernet disconnected",0xb4bdcf);
 pollik_ui_fill(c,x+16,y+215,348,1,0x54596c);
 int volume=(int)pollikos_device(USER_DEVICE_VOLUME_GET,0);char text[20];snprintf(text,sizeof(text),"Volume %d",volume);
 pollik_ui_text(c,x+24,y+224,text,0xf0f2f7);
 panel_round(c,x+334,y+220,28,28,panel_list==3?0x6854bf:0x494c63);pollik_ui_text(c,x+344,y+224,">",0xffffff);
 pollik_ui_fill(c,x+24,y+264,300,5,0x4b5064);pollik_ui_fill(c,x+24,y+264,volume*3,5,0xa797f0);
 panel_round(c,x+18+volume*3,y+257,12,19,0xffffff);
 if(panel_list==3){
  long mask=pollikos_device(USER_DEVICE_OUTPUT_MASK,0),selected=pollikos_device(USER_DEVICE_OUTPUT_GET,0);
  for(int i=0;i<2;i++)if(mask&(1<<i)){
   panel_round(c,x+12+i*180,y+290,176,28,selected==i?0x6854bf:0x34394d);
   pollik_ui_text(c,x+24+i*180,y+294,i?"AC'97 PCM":"PC Speaker",0xf0f2f7);
  }
 }else pollik_ui_text(c,x+24,y+294,pollikos_device(USER_DEVICE_OUTPUT_GET,0)?"Output: AC'97 PCM":"Output: PC Speaker",0xb4bdcf);
}
static int panel_pointer(PollikCanvas *c,int px,int py,int pressed){
 int x=(int)c->width/2-190,y=40;
 if(pressed&&py>=0&&py<38&&px>=(int)c->width/2-70&&px<(int)c->width/2+70){panel_toggle();return 1;}
 if(!panel_open&&!panel_slide)return 0;
 if(!pressed)return 1;
 if(px<x||px>=x+380||py<y||py>=y+330){if(panel_open)panel_toggle();return 1;}
 if(panel_slide!=256)return 1;
 int lx=px-x,ly=py-y;
 if(ly>=42&&ly<110){
  int radio=lx>=192?2:1;
  if((lx>=155&&lx<179)||(lx>=335&&lx<359)){
   panel_list=panel_list==radio?0:radio;
   (void)pollikos_device(radio==1?USER_DEVICE_WIFI_SCAN:USER_DEVICE_BT_SCAN,0);
  }else (void)pollikos_device(radio==1?USER_DEVICE_WIFI_SET:USER_DEVICE_BT_SET,1);
 }else if(ly>=120&&ly<154){(void)pollikos_device(USER_DEVICE_AIRPLANE_SET,!pollikos_device(USER_DEVICE_AIRPLANE_GET,0));}
 else if(lx>=334&&ly>=220&&ly<250)panel_list=panel_list==3?0:3;
 else if(ly>=252&&ly<281){panel_capture=1;int volume=(lx-24)/3;if(volume<0)volume=0;if(volume>100)volume=100;(void)pollikos_device(USER_DEVICE_VOLUME_SET,volume);}
 else if(panel_list==3&&ly>=290&&ly<318){(void)pollikos_device(USER_DEVICE_OUTPUT_SET,lx>=192);}
 return 1;
}
static void panel_drag(PollikCanvas *c,int px,int down){
 if(!down){panel_capture=0;return;}if(!panel_capture)return;
 int volume=(px-((int)c->width/2-190)-24)/3;
 if(volume<0)volume=0;if(volume>100)volume=100;
 (void)pollikos_device(USER_DEVICE_VOLUME_SET,volume);
}
#endif
