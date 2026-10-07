#include <pollikos/audio.h>
#include <pollikos/devices.h>
#include <pollikos/time.h>
#include <stdio.h>
int main(int argc,char **argv){
 (void)argv;
 printf("PROFILE disk_bytes=%lld framebuffer=%lldx%lld\n",(long long)pollikos_device(USER_DEVICE_DISK_BYTES,0),(long long)pollikos_device(USER_DEVICE_FB_WIDTH,0),(long long)pollikos_device(USER_DEVICE_FB_HEIGHT,0));
 if(pollikos_device(USER_DEVICE_VOLUME_SET,100)||pollikos_device(USER_DEVICE_OUTPUT_SET,1))return 1;
 if(argc==1){puts("PASS audio setup: native PCM output, volume 100");return 0;}
 if(pollikos_audio_begin())return 2;
 static int16_t data[2048];for(int i=0;i<2048;i++)data[i]=(i&32)?6000:-6000;
 if(pollikos_audio_submit(data,0)!=-USER_EINVAL||pollikos_audio_submit((int16_t *)1,1)!=-USER_EFAULT)return 3;
 for(int n=0;n<16;n++)if(pollikos_audio_submit(data,1024)!=1024)return 4;
 if(pollikos_audio_pause(1))return 5;
 long first=pollikos_audio_pending();pollikos_sleep_ms(100);long second=pollikos_audio_pending();
 if(first!=second||first<=0)return 6;
 if(pollikos_audio_pause(0))return 7;
 pollikos_sleep_ms(50);
 if(pollikos_audio_pending()>=first)return 8;
 printf("PASS native pause/resume queued_frames=%ld->%ld; exiting with live DMA for owner cleanup\n",first,(long)pollikos_audio_pending());
 return 0;
}
