#ifndef POLLIK_NATIVE_CONTROL_PANEL_H
#define POLLIK_NATIVE_CONTROL_PANEL_H
#include <pollikos/devices.h>
#include <pollikos/time.h>
#include <pollikos/session.h>
#include <pollikos/fs.h>
#include "../../common/app_search.h"
#include "window_ui.h"
/* Native Ring 3 overlay. All hardware access goes through the device syscall. */
static int panel_open,panel_list,panel_slide,panel_origin,panel_capture;
static long panel_started;
static int panel_profile,panel_profile_slide,panel_search_focus,panel_selected;
static AppSearchInput panel_input;
#define panel_query panel_input.text
static unsigned panel_pins;
static int panel_context=-1,panel_hover=-1,panel_blink;
static long panel_edit_started;
static long panel_profile_started;
static int panel_profile_origin;
static const struct {const char *name,*path;int icon;} panel_apps[]={
 {"Files","/bin/files.pol",1},{"Terminal","/bin/terminal.pol",2},{"Demo","/bin/windowdemo.pol",0},
 {"Browser","/bin/browser.pol",5},{"Notes","/bin/notes.pol",3},
 {"Calculator","/bin/calculator.pol",7}
};
static int panel_matches(unsigned id){return panel_query[0]?app_search_matches(panel_apps[id].name,panel_query):(panel_pins&(1u<<panel_apps[id].icon))!=0;}
static int panel_app_at(int row){for(unsigned i=0;i<sizeof(panel_apps)/sizeof(panel_apps[0]);i++)if(panel_matches(i)&&row--==0)return (int)i;return -1;}
static void panel_load_pins(void){char text[24]={0};FILE *f=fopen("/home/.config/launcher.conf","rb");panel_pins=0;if(f){fread(text,1,sizeof(text)-1,f);fclose(f);panel_pins=app_search_parse_pins(text);}}
static void panel_pin(unsigned id){
 unsigned pins=panel_pins^(1u<<panel_apps[id].icon);char text[24];int n=snprintf(text,sizeof(text),"pins=%u\n",pins);
 mkdir("/home/.config",0700);FILE *f=fopen("/home/.config/launcher.conf.pending","wb");if(!f)return;
 int ok=fwrite(text,1,(size_t)n,f)==(size_t)n;int closed=fclose(f);
 if(ok&&closed==0&&rename_replace("/home/.config/launcher.conf.pending","/home/.config/launcher.conf")==0)panel_pins=pins;
}
static int panel_query_width(int count){char text[32];int i=0;while(panel_query[i]&&i<count){text[i]=panel_query[i];i++;}text[i]=0;return pollik_ui_text_width(text);}
static void panel_edited(void){panel_edit_started=pollikos_monotonic_ms();panel_blink=1;panel_selected=0;}
static int panel_search_width(PollikCanvas *c){return c->width>=636?220:(int)c->width-404;}
static int panel_left(PollikCanvas *c){int sw=panel_search_width(c);return ((int)c->width-380-sw-12)/2+sw+12;}
static void panel_profile_set(int open){panel_profile_origin=panel_profile_slide;panel_profile=open;panel_profile_started=pollikos_monotonic_ms();}
static void panel_toggle(void){panel_capture=0;panel_profile=panel_profile_slide=0;panel_search_focus=0;
 app_search_reset(&panel_input);panel_selected=0;panel_context=panel_hover=-1;panel_origin=panel_slide;panel_open=!panel_open;
 if(panel_open)panel_load_pins();panel_started=pollikos_monotonic_ms();}
static int panel_tick(void){
 int blink=panel_search_focus&&((pollikos_monotonic_ms()-panel_edit_started)/500%2==0),blink_changed=blink!=panel_blink;panel_blink=blink;
 int profile_previous=panel_profile_slide,target_profile=panel_profile?96:0;
 if(panel_profile_slide!=target_profile){
  long elapsed=pollikos_monotonic_ms()-panel_profile_started;if(elapsed>=200)elapsed=200;
  int inverse=256-(int)(elapsed*256/200),ease=256-((inverse*inverse*inverse)>>16);
  panel_profile_slide=panel_profile_origin+(target_profile-panel_profile_origin)*ease/256;
 }
 long elapsed=pollikos_monotonic_ms()-panel_started;if(elapsed>=200)elapsed=200;
 int inverse=256-(int)(elapsed*256/200),ease=256-((inverse*inverse*inverse)>>16);
 int target=panel_open?256:0,previous=panel_slide;
 panel_slide=panel_origin+(target-panel_origin)*ease/256;return panel_slide!=previous||panel_profile_slide!=profile_previous||blink_changed;
}
static void panel_round(PollikCanvas *c,int x,int y,int w,int h,unsigned color){
 int radius=w<h?w/2:h/2;if(radius>10)radius=10;
 for(int row=0;row<h;row++){int edge=row<radius?radius-row:row>=h-radius?row-(h-radius-1):0;
  int inset=0;while(inset<radius&&edge*edge+(radius-inset)*(radius-inset)>radius*radius)inset++;
  pollik_ui_fill(c,x+inset,y+row,w-inset*2,1,color);
 }
}
static void panel_draw(PollikCanvas *c){
 if(!panel_slide||c->height<=38)return;
 PollikCanvas clipped={c->pixels+38*c->width,c->width,c->height-38};c=&clipped;
 int x=panel_left(c),y=2-(380*(256-panel_slide)/256);
 int sw=panel_search_width(c),sx=x-sw-12,row=0;
 panel_round(c,sx,y,sw,330,0x252a3b);
 pollik_ui_text(c,sx+14,y+12,"Applications",0xf0f2f7);
 panel_round(c,sx+10,y+42,sw-20,34,0x34394d);
 pollik_ui_fill(c,sx+10,y+74,sw-20,panel_search_focus?2:1,panel_search_focus?0xa797f0:0x54596c);
 int start=0;while(panel_query[start]&&panel_query_width(panel_input.cursor)-panel_query_width(start)>sw-36)start++;
 int a=panel_input.anchor,b=panel_input.cursor;if(a>b){int temp=a;a=b;b=temp;}
 int text_x=sx+16;
 if(!panel_query[0])pollik_ui_text(c,text_x,y+50,"Search apps",0xb4bdcf);
 for(int i=start;panel_query[i];i++){
  char letter[2]={panel_query[i],0};int w=pollik_ui_text_width(letter);if(text_x+w>sx+sw-16)break;
  if(panel_search_focus&&panel_input.anchor>=0&&i>=a&&i<b)pollik_ui_fill(c,text_x,y+48,w,24,0x6854bf);
  pollik_ui_text(c,text_x,y+50,letter,0xf0f2f7);text_x+=w;
 }
 if(panel_search_focus&&panel_blink)pollik_ui_fill(c,sx+16+panel_query_width(panel_input.cursor)-panel_query_width(start),y+49,2,20,0xf0f2f7);
 pollik_ui_text(c,sx+14,y+88,panel_query[0]?"Results":"Recommended",0xb4bdcf);
 for(unsigned i=0;i<sizeof(panel_apps)/sizeof(panel_apps[0]);i++){
  if(!panel_matches(i))continue;
  if(row==panel_hover||(panel_search_focus&&row==panel_selected))panel_round(c,sx+8,y+112+row*30,sw-16,28,0x494c63);
  pollik_assets_icon(c,panel_apps[i].icon,sx+12,y+114+row*30,24);
  pollik_ui_text(c,sx+42,y+117+row*30,panel_apps[i].name,0xf0f2f7);
  for(int dot=0;dot<3;dot++)pollik_ui_fill(c,sx+sw-15,y+118+row*30+dot*4,2,2,0xb4bdcf);row++;
 }
 if(!row&&panel_query[0])pollik_ui_text(c,sx+16,y+117,"No results",0xb4bdcf);
 if(panel_context>=0){
  panel_round(c,sx+8,y+110,sw-16,104,0x494c63);
  pollik_ui_text(c,sx+16,y+120,"Open",0xf0f2f7);pollik_ui_text(c,sx+16,y+154,"Run as admin",0xf0f2f7);
  pollik_ui_text(c,sx+16,y+188,panel_pins&(1u<<panel_apps[panel_context].icon)?"Unpin":"Pin",0xf0f2f7);
 }
 /* The canvas clips each span. Restore menu bar after the panel slides up. */
 panel_round(c,x,y,380,380,0x252a3b);
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
 panel_round(c,x+12,y+336,356,34,0x34394d);
 panel_round(c,x+28,y+341,12,12,0xb4bdcf);panel_round(c,x+22,y+354,24,11,0xb4bdcf);
 pollik_ui_text(c,x+62,y+343,"Profile",0xf0f2f7);pollik_ui_text(c,x+340,y+343,panel_profile?"v":"^",0xf0f2f7);
 if(panel_profile_slide){
  int my=y+330-panel_profile_slide;
  panel_round(c,x+12,my,356,panel_profile_slide,0x494c63);
  const char *labels[]={"Turn off","Restart","Logout"};
  for(int i=0;i<3;i++)if(i*32+24<=panel_profile_slide)pollik_ui_text(c,x+24,my+6+i*32,labels[i],0xf0f2f7);
 }
}
static void panel_launch(unsigned id){
 if(launch(panel_apps[id].path)){panel_toggle();}else {puts("[desktop] search launch failed");}
}
static void panel_launch_admin(unsigned id){
 const char *argv[]={panel_apps[id].path,0};
 if(pollikos_session_control(USER_SESSION_ELEVATE)<0)return;
 long pid=pollikos_spawn_rights(panel_apps[id].path,argv,0,USER_CAP_ADMIN_ALL);
 if(pid>0){printf("[desktop] administrator launched %s pid=%ld\n",panel_apps[id].path,pid);panel_toggle();}
 else puts("[desktop] administrator launch failed");
}
static void panel_key(unsigned key,unsigned modifiers){
 if(key==27){if(panel_context>=0)panel_context=-1;else if(panel_profile)panel_profile_set(0);else if(panel_open)panel_toggle();return;}
 if(!panel_search_focus||panel_slide!=256)return;
 int count=0;for(unsigned i=0;i<sizeof(panel_apps)/sizeof(panel_apps[0]);i++)if(panel_matches(i))count++;
 if(key==USER_KEY_UP&&panel_selected>0)panel_selected--;
 else if(key==USER_KEY_DOWN&&panel_selected+1<count)panel_selected++;
 else if(key==13){int id=panel_app_at(panel_selected);if(id>=0)panel_launch((unsigned)id);}
 else {
  unsigned command=key==USER_KEY_LEFT?APP_SEARCH_LEFT:key==USER_KEY_RIGHT?APP_SEARCH_RIGHT:key==USER_KEY_HOME?APP_SEARCH_HOME:key==USER_KEY_END?APP_SEARCH_END:key==USER_KEY_DELETE?127:key;
  if(app_search_edit(&panel_input,command,modifiers&USER_INPUT_MOD_SHIFT,modifiers&USER_INPUT_MOD_CONTROL))panel_edited();
 }
}
static int panel_pointer(PollikCanvas *c,int px,int py,int pressed){
 int x=panel_left(c),y=40;
 if(pressed&&py>=0&&py<38&&px>=(int)c->width/2-70&&px<(int)c->width/2+70){panel_toggle();return 1;}
 if(!panel_open&&!panel_slide)return 0;
 if(!pressed)return 1;
 int sw=panel_search_width(c),ly=py-y;
 if(px>=x-sw-12&&px<x-12&&ly>=0&&ly<330){
  if(panel_slide!=256)return 1;if(panel_profile)panel_profile_set(0);
  if(ly>=42&&ly<76){panel_search_focus=1;panel_context=-1;int start=0;
   while(panel_query[start]&&panel_query_width(panel_input.cursor)-panel_query_width(start)>sw-36)start++;
   int at=0,position=px-(x-sw-12)-16+panel_query_width(start);
   while(panel_query[at]&&panel_query_width(at+1)<position)at++;app_search_select(&panel_input,at,0);panel_edited();return 1;}
  panel_search_focus=0;
  if(panel_context>=0){unsigned id=(unsigned)panel_context;panel_context=-1;
   if(ly>=110&&ly<212){int action=(ly-110)/34;if(action==2)panel_pin(id);else if(action==1)panel_launch_admin(id);else panel_launch(id);}
   return 1;
  }
  if(ly>=112){int id=panel_app_at((ly-112)/30);if(id>=0){if(px>=x-12-32)panel_context=id;else panel_launch((unsigned)id);}}
  return 1;
 }
 if(px<x||px>=x+380||py<y||py>=y+380){if(panel_open)panel_toggle();return 1;}
 if(panel_slide!=256)return 1;
 int lx=px-x;panel_search_focus=0;
 if(lx>=12&&lx<368&&ly>=336&&ly<370){panel_profile_set(!panel_profile);return 1;}
 if(panel_profile_slide){
  if(panel_profile&&panel_profile_slide==96&&lx>=12&&lx<368&&ly>=234&&ly<330){
   int action=(ly-234)/32;
   long result=action==2?pollikos_session_control(USER_SESSION_LOGOUT):pollikos_device(USER_DEVICE_POWER,action==1);
   if(result<0)puts("[desktop] session action failed");
  }else if(panel_profile)panel_profile_set(0);
  return 1;
 }
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
static int panel_hover_at(PollikCanvas *c,int px,int py){
 int sw=panel_search_width(c),sx=panel_left(c)-sw-12;
 int hover=px>=sx+8&&px<sx+sw-8&&py>=40+112?(py-40-112)/30:-1;
 if(hover<0||panel_app_at(hover)<0)hover=-1;int changed=hover!=panel_hover;panel_hover=hover;return changed&&(panel_open||panel_slide);
}
static void panel_drag(PollikCanvas *c,int px,int down){
 if(!down){panel_capture=0;return;}if(!panel_capture)return;
 int volume=(px-panel_left(c)-24)/3;
 if(volume<0)volume=0;if(volume>100)volume=100;
 (void)pollikos_device(USER_DEVICE_VOLUME_SET,volume);
}
#endif
