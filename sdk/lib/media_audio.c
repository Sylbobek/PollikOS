#include <pollikos/media.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include <pollikos/minimp3.h>
#define ENCODED_LIMIT (16u*1024u*1024u)
struct pollikos_audio_decoder {
 const unsigned char *data;unsigned char *owned;size_t bytes,offset,end;
 unsigned kind,rate,channels,bits,align,format,phase;
 int have,last;int16_t a[2],b[2];
 mp3dec_t mp3;mp3dec_frame_info_t info;
 int16_t decoded[MINIMP3_MAX_SAMPLES_PER_FRAME];unsigned decoded_frames,decoded_at;
};
static unsigned le16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t le32(const unsigned char *p){return le16(p)|((uint32_t)le16(p+2)<<16);}
static int wave_header(pollikos_audio_decoder *d){
 if(d->bytes<12||memcmp(d->data,"RIFF",4)||memcmp(d->data+8,"WAVE",4))return 0;
 uint64_t end=(uint64_t)le32(d->data+4)+8;if(end>d->bytes||end<12)return 0;
 size_t pos=12;int fmt=0,data=0;
 while(pos+8<=end){
  const unsigned char *p=d->data+pos;uint32_t n=le32(p+4);pos+=8;
  if(n>end-pos)return 0;
  if(!memcmp(p,"fmt ",4)){
   if(n<16||fmt)return 0;fmt=1;
   d->format=le16(p+8);d->channels=le16(p+10);d->rate=le32(p+12);
   d->align=le16(p+20);d->bits=le16(p+22);
   if(d->format==0xfffe){
    static const unsigned char guid_tail[12]={0,0,16,0,128,0,0,170,0,56,155,113};
    if(n<40||le16(p+24)<22||le16(p+24)>n-18||!le16(p+26)||le16(p+26)>d->bits||
       le16(p+34)||memcmp(p+36,guid_tail,12))return 0;
    d->format=le16(p+32);
   }
   if(d->rate<8000||d->rate>48000||!d->channels||d->channels>2||
      (d->format!=1&&d->format!=3)||
      (d->format==1&&d->bits!=8&&d->bits!=16&&d->bits!=24&&d->bits!=32)||
      (d->format==3&&d->bits!=32)||d->align!=d->channels*(d->bits/8)||
      le32(p+16)!=d->rate*d->align)return 0;
  }else if(!memcmp(p,"data",4)){
   if(data)return 0;data=1;d->offset=pos;d->end=pos+n;
  }
  pos+=n;if(n&1){if(pos==end)return 0;pos++;}
 }
 return pos==end&&fmt&&data&&d->end>d->offset&&(d->end-d->offset)%d->align==0;
}
static int16_t wave_sample(const unsigned char *p,unsigned format,unsigned bits){
 if(format==3){
  union {uint32_t u;float f;} x={.u=le32(p)};
  if((x.u&0x7f800000)==0x7f800000)return 0;
  if(x.f>=1)return 32767;if(x.f<=-1)return -32768;return (int16_t)(x.f*32768.0f);
 }
 if(bits==8)return (int16_t)(((int)p[0]-128)*256);
 if(bits==16)return (int16_t)le16(p);
 if(bits==24)return (int16_t)((int32_t)(le16(p)|((uint32_t)p[2]<<16))>>8);
 return (int16_t)((int32_t)le32(p)>>16);
}
static int source_frame(pollikos_audio_decoder *d,int16_t pair[2]){
 if(d->kind==1){
  if(d->offset>=d->end)return 0;const unsigned char *p=d->data+d->offset;
  pair[0]=wave_sample(p,d->format,d->bits);
  pair[1]=d->channels==1?pair[0]:wave_sample(p+d->bits/8,d->format,d->bits);
  d->offset+=d->align;return 1;
 }
 while(d->decoded_at==d->decoded_frames){
  if(d->offset==d->bytes)return 0;
  int samples=mp3dec_decode_frame(&d->mp3,d->data+d->offset,(int)(d->bytes-d->offset),d->decoded,&d->info);
  if(d->info.frame_bytes<=0)return 0;
  d->offset+=(unsigned)d->info.frame_bytes;
  if(!samples)continue;
  if(d->info.layer!=3||d->info.hz<8000||d->info.hz>48000||d->info.channels<1||d->info.channels>2)return 0;
  if(d->rate&&d->rate!=(unsigned)d->info.hz)return 0; /* no mid-stream rate change */
  d->rate=(unsigned)d->info.hz;d->channels=(unsigned)d->info.channels;
  d->decoded_at=0;d->decoded_frames=(unsigned)samples;
 }
 pair[0]=d->decoded[d->decoded_at*d->channels];
 pair[1]=d->channels==1?pair[0]:d->decoded[d->decoded_at*d->channels+1];d->decoded_at++;return 1;
}
pollikos_audio_decoder *pollikos_media_open(const void *data,size_t bytes){
 if(!data||!bytes||bytes>ENCODED_LIMIT)return 0;
 pollikos_audio_decoder *d=calloc(1,sizeof(*d));if(!d)return 0;d->data=data;d->bytes=bytes;
 if(bytes>=4&&!memcmp(data,"RIFF",4)){d->kind=1;if(!wave_header(d)){free(d);return 0;}}
 else {d->kind=2;mp3dec_init(&d->mp3);}
 if(!source_frame(d,d->a)){free(d);return 0;}d->have=1;
 if(!source_frame(d,d->b)){d->last=1;d->b[0]=d->a[0];d->b[1]=d->a[1];}
 return d;
}
pollikos_audio_decoder *pollikos_media_open_file(const char *path){
 FILE *f=fopen(path,"rb");if(!f)return 0;
 if(fseek(f,0,SEEK_END)){fclose(f);return 0;}long length=ftell(f);
 if(length<=0||(unsigned long)length>ENCODED_LIMIT||fseek(f,0,SEEK_SET)){fclose(f);return 0;}
 unsigned char *data=malloc((size_t)length);if(!data){fclose(f);return 0;}
 size_t got=fread(data,1,(size_t)length,f);fclose(f);
 if(got!=(size_t)length){free(data);return 0;}
 pollikos_audio_decoder *d=pollikos_media_open(data,got);if(!d){free(data);return 0;}d->owned=data;return d;
}
size_t pollikos_media_read(pollikos_audio_decoder *d,int16_t *out,size_t frames){
 if(!d||!out||frames>SIZE_MAX/4)return 0;size_t count=0;
 while(count<frames&&d->have){
  for(unsigned c=0;c<2;c++)out[count*2+c]=(int16_t)(d->a[c]+((int64_t)d->b[c]-d->a[c])*d->phase/48000);
  count++;d->phase+=d->rate;
  while(d->phase>=48000){
   d->phase-=48000;if(d->last){d->have=0;break;}
   d->a[0]=d->b[0];d->a[1]=d->b[1];
   if(!source_frame(d,d->b)){d->last=1;d->b[0]=d->a[0];d->b[1]=d->a[1];}
  }
 }
 return count;
}
void pollikos_media_close(pollikos_audio_decoder *d){if(d){free(d->owned);free(d);}}
unsigned pollikos_media_source_rate(const pollikos_audio_decoder *d){return d?d->rate:0;}
const char *pollikos_media_format(const pollikos_audio_decoder *d){return !d?"unsupported":d->kind==1?"WAV":"MP3";}
