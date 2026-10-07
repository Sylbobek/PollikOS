#include <pollikos/devices.h>
#include <pollikos/net.h>
#include <pollikos/time.h>
#include <stdio.h>
#include <string.h>
static int errors;
#define CHECK(c) do{if(!(c)){printf("FAIL devices %d: %s\n",__LINE__,#c);errors++;}}while(0)
static int fetch(const char *url){
 long h=pollikos_http_open(url);if(h<0)return 0;long end=pollikos_monotonic_ms()+15000;
 char data[128];int found=0;long n=-USER_EAGAIN;
 while(pollikos_monotonic_ms()<end){
  n=pollikos_http_read(h,data,sizeof(data)-1);
  if(n>0){data[n]=0;if(strstr(data,"device-proof"))found=1;}
  else if(n==0||n!=-USER_EAGAIN)break;
  pollikos_sleep_ms(10);
 }
 CHECK(pollikos_http_close(h)==0);return found&&n==0;
}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 long caps=pollikos_device(USER_DEVICE_CAPS,0),mask=pollikos_device(USER_DEVICE_OUTPUT_MASK,0);
 printf("DEVICES caps=%ld output_mask=%ld framebuffer=%ldx%ld bpp=%ld pitch=%ld\n",caps,mask,
  pollikos_device(USER_DEVICE_FB_WIDTH,0),pollikos_device(USER_DEVICE_FB_HEIGHT,0),
  pollikos_device(USER_DEVICE_FB_BPP,0),pollikos_device(USER_DEVICE_FB_PITCH,0));
 CHECK(caps&1);CHECK(caps&32);CHECK(pollikos_device(999,0)==-USER_EINVAL);
 CHECK(pollikos_device(USER_DEVICE_AIRPLANE_SET,2)==-USER_EINVAL);
 CHECK(pollikos_device(USER_DEVICE_VOLUME_SET,101)==-USER_EINVAL);
 CHECK(pollikos_device(USER_DEVICE_WIFI_SCAN,0)==-USER_ENOTSUP);
 CHECK(pollikos_device(USER_DEVICE_BT_SCAN,0)==-USER_ENOTSUP);
 CHECK(pollikos_device(USER_DEVICE_VOLUME_SET,75)==0);CHECK(pollikos_device(USER_DEVICE_VOLUME_GET,0)==75);
 if(mask&2){CHECK(pollikos_device(USER_DEVICE_OUTPUT_SET,1)==0);for(int n=0;n<12;n++){
   CHECK(pollikos_device(USER_DEVICE_TEST_TONE,0)==0);pollikos_sleep_ms(80);
 }}else CHECK(pollikos_device(USER_DEVICE_OUTPUT_SET,1)==-USER_ENOTSUP);
 long end=pollikos_monotonic_ms()+10000;
 while(!pollikos_device(USER_DEVICE_CONNECTED,0)&&pollikos_monotonic_ms()<end)pollikos_sleep_ms(10);
 CHECK(pollikos_device(USER_DEVICE_CONNECTED,0)==1);CHECK(fetch(argv[1]));
 long handle=pollikos_http_open(argv[1]);CHECK(handle>0);pollikos_sleep_ms(100);
 CHECK(pollikos_device(USER_DEVICE_AIRPLANE_SET,1)==0);
 long tx=pollikos_device(USER_DEVICE_TX_PACKETS,0),rx=pollikos_device(USER_DEVICE_RX_PACKETS,0);
 pollikos_sleep_ms(1000);
 long tx_after=pollikos_device(USER_DEVICE_TX_PACKETS,0),rx_after=pollikos_device(USER_DEVICE_RX_PACKETS,0);
 CHECK(tx_after==tx);CHECK(rx_after==rx);CHECK(pollikos_device(USER_DEVICE_CONNECTED,0)==0);
 CHECK(pollikos_http_open(argv[1])==-USER_EIO);char b[16];CHECK(pollikos_http_read(handle,b,sizeof(b))==-USER_EIO);
 CHECK(pollikos_http_close(handle)==0);
 printf("AIRPLANE packets tx=%ld->%ld rx=%ld->%ld; new/old HTTP EIO\n",tx,tx_after,rx,rx_after);
 CHECK(pollikos_device(USER_DEVICE_AIRPLANE_SET,0)==0);
 end=pollikos_monotonic_ms()+10000;while(!pollikos_device(USER_DEVICE_CONNECTED,0)&&pollikos_monotonic_ms()<end)pollikos_sleep_ms(10);
 CHECK(pollikos_device(USER_DEVICE_CONNECTED,0)==1);CHECK(fetch(argv[1]));
 printf("%s native device ABI: Ethernet reconnect/HTTP, airplane, audio selection/tone, bounds, framebuffer, unsupported radios\n",errors?"FAIL":"PASS");
 return !!errors;
}
