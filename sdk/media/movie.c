#include <pollikos/movie.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "h264bsd_decoder.h"
#include "neaacdec.h"
#define LIMIT (16u*1024u*1024u)
#define MAX_SAMPLES 8192
typedef struct {size_t start,end;} Range;
typedef struct {size_t offset;uint32_t bytes,duration;} Sample;
typedef struct {
 Range stsz,stts,stsc,stco,config;unsigned count,scale,codec,width,height,co64,length_bytes;
 uint64_t skip;Sample *samples;
} Track;
struct pollikos_movie {
 const unsigned char *data;unsigned char *owned;size_t bytes;Range mdat[16];unsigned mdats;
 Track video,audio;storage_t *h264;NeAACDecHandle aac;
 unsigned vi,ai,rate,channels,width,height,phase,have,last,decoded_at,decoded_frames,failed;
 uint64_t video_time,audio_skip;int16_t a[2],b[2];int16_t *decoded;
 unsigned char *yuv;uint32_t *rgb;
};
static uint32_t be32(const unsigned char *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint64_t be64(const unsigned char *p){return (uint64_t)be32(p)<<32|be32(p+4);}
static unsigned be16(const unsigned char *p){return p[0]*256u+p[1];}
static int atom(const unsigned char *d,size_t end,size_t *position,char type[5],Range *payload){
 size_t p=*position;if(p>end||end-p<8)return 0;uint64_t size=be32(d+p);size_t header=8;
 memcpy(type,d+p+4,4);type[4]=0;
 if(size==1){if(end-p<16)return 0;size=be64(d+p+8);header=16;}
 if(!size)size=end-p;if(size<header||size>end-p)return 0;
 *payload=(Range){p+header,p+(size_t)size};*position=payload->end;return 1;
}
static int asc(const unsigned char *d,Range r,Range *result,unsigned depth){
 if(depth>4)return 0;size_t p=r.start;
 while(p<r.end){
  unsigned tag=d[p++],length=0,n=0,b;
  do{if(p==r.end||n++==4)return 0;b=d[p++];length=(length<<7)|(b&127);}while(b&128);
  if(length>r.end-p)return 0;Range inner={p,p+length};
  if(tag==5){*result=inner;return length>=2&&length<=16;}
  if(tag==3){if(length<3)return 0;unsigned flags=d[p+2];inner.start+=3;
   if(flags&128)inner.start+=2;if(flags&64){if(inner.start>=inner.end)return 0;inner.start+=1+d[inner.start];}if(flags&32)inner.start+=2;
  }else if(tag==4){if(length<13||d[p]!=0x40)return 0;inner.start+=13;}
  else {p+=length;continue;}
  if(inner.start>inner.end)return 0;if(asc(d,inner,result,depth+1))return 1;p+=length;
 }
 return 0;
}
static int table(pollikos_movie *m,Range r,unsigned stride,unsigned *count){
 if(r.end-r.start<8)return 0;*count=be32(m->data+r.start+4);
 return *count<=MAX_SAMPLES&&r.end-r.start==8+(size_t)*count*stride;
}
static int walk_track(pollikos_movie *m,Track *t,Range r,unsigned depth){
 if(depth>6)return 0;size_t p=r.start;char type[5];Range s;
 while(p<r.end){
  if(!atom(m->data,r.end,&p,type,&s))return 0;const unsigned char *d=m->data+s.start;size_t n=s.end-s.start;
  if(!strcmp(type,"mdia")||!strcmp(type,"minf")||!strcmp(type,"stbl")||!strcmp(type,"edts")){
   if(!walk_track(m,t,s,depth+1))return 0;
  }else if(!strcmp(type,"mdhd")){
   if(n<20||d[0]>1)return 0;size_t o=d[0]?20:12;if(n<o+4)return 0;t->scale=be32(d+o);
  }else if(!strcmp(type,"elst")){
   if(n<20||d[0]!=0||be32(d+4)!=1||(int32_t)be32(d+12)<0||be32(d+16)!=0x10000)return 0;t->skip=be32(d+12);
  }else if(!strcmp(type,"stsz"))t->stsz=s;
  else if(!strcmp(type,"stts"))t->stts=s;
  else if(!strcmp(type,"stsc"))t->stsc=s;
  else if(!strcmp(type,"stco")||!strcmp(type,"co64")){t->stco=s;t->co64=!strcmp(type,"co64");}
  else if(!strcmp(type,"ctts")){
   unsigned count;if(!table(m,s,8,&count)||d[0]!=0)return 0;
   for(unsigned i=0;i<count;i++)if(be32(d+12+i*8))return 0;
  }else if(!strcmp(type,"stsd")){
   if(n<8||be32(d+4)!=1)return 0;size_t q=s.start+8;Range entry;char codec[5];
   if(!atom(m->data,s.end,&q,codec,&entry)||q!=s.end)return 0;
   size_t origin=entry.start-8,children;
   if(!strcmp(codec,"avc1")){
    if(entry.end-origin<86)return 0;t->codec=1;t->width=be16(m->data+origin+32);t->height=be16(m->data+origin+34);children=origin+86;
   }else if(!strcmp(codec,"mp4a")){if(entry.end-origin<36||be16(m->data+origin+16))return 0;t->codec=2;children=origin+36;}
   else return 0;
   while(children<entry.end){Range c;char ct[5];if(!atom(m->data,entry.end,&children,ct,&c))return 0;
    if(t->codec==1&&!strcmp(ct,"avcC"))t->config=c;
    if(t->codec==2&&!strcmp(ct,"esds")){if(c.end-c.start<4)return 0;c.start+=4;if(!asc(m->data,c,&t->config,0))return 0;}
   }
  }
 }
 return 1;
}
static int in_mdat(pollikos_movie *m,size_t p,size_t n){
 for(unsigned i=0;i<m->mdats;i++)if(p>=m->mdat[i].start&&p<=m->mdat[i].end&&n<=m->mdat[i].end-p)return 1;return 0;
}
static int samples(pollikos_movie *m,Track *t){
 if(!t->codec||!t->scale||t->scale>1000000||!t->config.end||t->stsz.end-t->stsz.start<12)return 0;
 const unsigned char *z=m->data+t->stsz.start;uint32_t fixed=be32(z+4);t->count=be32(z+8);
 if(!t->count||t->count>MAX_SAMPLES||t->stsz.end-t->stsz.start!=12+(fixed?0:(size_t)t->count*4))return 0;
 t->samples=calloc(t->count,sizeof(Sample));if(!t->samples)return 0;
 for(unsigned i=0;i<t->count;i++){t->samples[i].bytes=fixed?fixed:be32(z+12+i*4);if(!t->samples[i].bytes||t->samples[i].bytes>LIMIT)return 0;}
 unsigned entries;if(!table(m,t->stts,8,&entries))return 0;unsigned at=0;
 for(unsigned i=0;i<entries;i++){
  const unsigned char *p=m->data+t->stts.start+8+i*8;uint32_t count=be32(p),duration=be32(p+4);
  if(count>t->count-at||!duration||duration>t->scale*10)return 0;
  while(count--)t->samples[at++].duration=duration;
 }if(at!=t->count)return 0;
 unsigned chunks,runs;if(!table(m,t->stco,t->co64?8:4,&chunks)||!table(m,t->stsc,12,&runs)||!runs||!chunks)return 0;
 const unsigned char *sc=m->data+t->stsc.start+8;unsigned run=0;at=0;
 if(be32(sc)!=1)return 0;
 for(unsigned i=0;i<runs;i++)if(!be32(sc+i*12+4)||be32(sc+i*12+8)!=1||(i&&be32(sc+i*12)<=be32(sc+(i-1)*12)))return 0;
 for(unsigned chunk=1;chunk<=chunks;chunk++){
  if(run+1<runs&&chunk>=be32(sc+(run+1)*12))run++;
  uint32_t count=be32(sc+run*12+4);if(count>t->count-at)return 0;
  const unsigned char *p=m->data+t->stco.start+8+(chunk-1)*(t->co64?8:4);uint64_t offset=t->co64?be64(p):be32(p);
  if(offset>m->bytes)return 0;
  while(count--){Sample *sample=&t->samples[at++];if(!in_mdat(m,(size_t)offset,sample->bytes))return 0;sample->offset=(size_t)offset;offset+=sample->bytes;}
 }return at==t->count;
}
static int feed_nal(pollikos_movie *m,const unsigned char *p,unsigned n,unsigned id){
 if(!n||n>LIMIT)return 0;unsigned char *annex=malloc(n+4);if(!annex)return 0;
 annex[0]=annex[1]=annex[2]=0;annex[3]=1;memcpy(annex+4,p,n);unsigned used=0,tries=0,result;
 do{result=h264bsdDecode(m->h264,annex,n+4,id,&used);if(result==H264BSD_ERROR||result==H264BSD_PARAM_SET_ERROR||result==H264BSD_MEMALLOC_ERROR){free(annex);return 0;}}while(!used&&++tries<4);
 for(unsigned i=0;i<MAX_NUM_SEQ_PARAM_SETS;i++)if(m->h264->sps[i]){
  seqParamSet_t *s=m->h264->sps[i];
  if(s->picWidthInMbs>40||s->picHeightInMbs>30){free(annex);return 0;}
 }
 free(annex);return used!=0;
}
static int init_video(pollikos_movie *m){
 Track *t=&m->video;const unsigned char *d=m->data;Range r=t->config;
 if(t->width<16||t->height<16||t->width>640||t->height>480||(t->width&1)||(t->height&1)||r.end-r.start<7||d[r.start]!=1||d[r.start+1]!=66||t->skip)return 0;
 t->length_bytes=(d[r.start+4]&3)+1;if(t->length_bytes==3)return 0;
 m->h264=h264bsdAlloc();if(!m->h264||h264bsdInit(m->h264,1)!=0)return 0;
 size_t p=r.start+6;unsigned sets=d[r.start+5]&31;if(!sets)return 0;
 for(unsigned group=0;group<2;group++){
  while(sets--){if(p+2>r.end)return 0;unsigned n=be16(d+p);p+=2;if(n>r.end-p||!feed_nal(m,d+p,n,0))return 0;p+=n;}
  if(!group){if(p==r.end)return 0;sets=d[p++];if(!sets)return 0;}
 }
 m->width=t->width;m->height=t->height;
 m->rgb=malloc((size_t)m->width*m->height*4);m->yuv=malloc((size_t)m->width*m->height*3/2);return m->rgb&&m->yuv;
}
static int init_audio(pollikos_movie *m){
 if(!m->audio.codec)return 1;Range r=m->audio.config;
 if((m->data[r.start]>>3)!=2)return 0; /* AAC LC, no HE/SBR expansion */
 m->aac=NeAACDecOpen();if(!m->aac)return 0;
 NeAACDecConfigurationPtr cfg=NeAACDecGetCurrentConfiguration(m->aac);cfg->defObjectType=LC;cfg->outputFormat=FAAD_FMT_16BIT;cfg->downMatrix=0;
 if(!NeAACDecSetConfiguration(m->aac,cfg))return 0;
 unsigned long rate;unsigned char channels;
 if(NeAACDecInit2(m->aac,(unsigned char *)m->data+r.start,(unsigned long)(r.end-r.start),&rate,&channels)||rate<8000||rate>48000||!channels||channels>2)return 0;
 m->rate=(unsigned)rate;m->channels=channels;m->audio_skip=m->audio.skip*rate/m->audio.scale;return m->audio_skip<=rate*10;
}
pollikos_movie *pollikos_movie_open(const void *bytes,size_t n){
 if(!bytes||n<16||n>LIMIT)return 0;pollikos_movie *m=calloc(1,sizeof(*m));if(!m)return 0;m->data=bytes;m->bytes=n;
 size_t p=0;Range r,moov={0};char type[5];unsigned boxes=0;int ftyp=0;
 while(p<n){if(++boxes>2048||!atom(m->data,n,&p,type,&r))goto bad;
  if(!strcmp(type,"ftyp"))ftyp=1;else if(!strcmp(type,"moov")){if(moov.end)goto bad;moov=r;}
  else if(!strcmp(type,"mdat")){if(m->mdats==16)goto bad;m->mdat[m->mdats++]=r;}
  else if(!strcmp(type,"moof"))goto bad;
 }
 if(!ftyp||!moov.end||!m->mdats)goto bad;p=moov.start;unsigned tracks=0;
 while(p<moov.end){if(!atom(m->data,moov.end,&p,type,&r))goto bad;if(strcmp(type,"trak"))continue;
  if(++tracks>2)goto bad;Track t={0};if(!walk_track(m,&t,r,0)||!samples(m,&t)){free(t.samples);goto bad;}
  if(t.codec==1&&!m->video.codec)m->video=t;else if(t.codec==2&&!m->audio.codec)m->audio=t;else {free(t.samples);goto bad;}
 }
 if(!m->video.codec||!init_video(m)||!init_audio(m))goto bad;return m;
bad:pollikos_movie_close(m);return 0;
}
pollikos_movie *pollikos_movie_open_file(const char *path){
 FILE *f=fopen(path,"rb");if(!f)return 0;if(fseek(f,0,SEEK_END)){fclose(f);return 0;}long n=ftell(f);
 if(n<=0||(unsigned long)n>LIMIT||fseek(f,0,SEEK_SET)){fclose(f);return 0;}
 unsigned char *data=malloc((size_t)n);if(!data){fclose(f);return 0;}size_t got=fread(data,1,(size_t)n,f);fclose(f);
 if(got!=(size_t)n){free(data);return 0;}pollikos_movie *m=pollikos_movie_open(data,got);if(!m){free(data);return 0;}m->owned=data;return m;
}
void pollikos_movie_close(pollikos_movie *m){if(!m)return;if(m->h264){h264bsdShutdown(m->h264);h264bsdFree(m->h264);}if(m->aac)NeAACDecClose(m->aac);free(m->video.samples);free(m->audio.samples);free(m->rgb);free(m->yuv);free(m->owned);free(m);}
unsigned pollikos_movie_width(const pollikos_movie *m){return m?m->width:0;}
unsigned pollikos_movie_height(const pollikos_movie *m){return m?m->height:0;}
unsigned pollikos_movie_audio_rate(const pollikos_movie *m){return m?m->rate:0;}
unsigned pollikos_movie_video_count(const pollikos_movie *m){return m?m->video.count:0;}
int pollikos_movie_error(const pollikos_movie *m){return !m||m->failed;}
const unsigned char *pollikos_movie_yuv(const pollikos_movie *m){return m?m->yuv:0;}
const uint32_t *pollikos_movie_rgb(const pollikos_movie *m){return m?m->rgb:0;}
static unsigned clamp(int x){return x<0?0:x>255?255:(unsigned)x;}
int pollikos_movie_next_video(pollikos_movie *m,uint64_t *milliseconds){
 if(!m||!milliseconds)return -1;if(m->vi==m->video.count)return 0;
 Sample *sample=&m->video.samples[m->vi];size_t p=sample->offset,end=p+sample->bytes;
 while(p<end){if(end-p<m->video.length_bytes)return -1;unsigned n=0;for(unsigned i=0;i<m->video.length_bytes;i++)n=n*256+m->data[p++];
  if(n>end-p||!feed_nal(m,m->data+p,n,m->vi))return -1;p+=n;
 }
 unsigned id,idr,errors;const unsigned char *raw=h264bsdNextOutputPicture(m->h264,&id,&idr,&errors);
 if(!raw||errors||id!=m->vi||h264bsdProfile(m->h264)!=66)return -1;
 unsigned stride=h264bsdPicWidth(m->h264)*16,rows=h264bsdPicHeight(m->h264)*16;
 unsigned crop,left,width,top,height;h264bsdCroppingParams(m->h264,&crop,&left,&width,&top,&height);
 if(!crop){left=top=0;width=stride;height=rows;}
 if(width!=m->width||height!=m->height||left+width>stride||top+height>rows)return -1;
 const unsigned char *u=raw+stride*rows,*v=u+stride*rows/4;
 for(unsigned y=0;y<height;y++)memcpy(m->yuv+y*width,raw+(y+top)*stride+left,width);
 for(unsigned y=0;y<height/2;y++){
  memcpy(m->yuv+width*height+y*(width/2),u+(y+top/2)*(stride/2)+left/2,width/2);
  memcpy(m->yuv+width*height*5/4+y*(width/2),v+(y+top/2)*(stride/2)+left/2,width/2);
 }
 unsigned full=h264bsdVideoRange(m->h264),matrix=h264bsdMatrixCoefficients(m->h264);
 for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){
  unsigned xx=x+left,yy=y+top;int c=raw[yy*stride+xx]-(full?0:16),d=u[(yy/2)*(stride/2)+xx/2]-128,e=v[(yy/2)*(stride/2)+xx/2]-128;
  int k=full?256:298,rv=matrix==1?(full?403:459):(full?359:409),gu=matrix==1?(full?48:55):(full?88:100),gv=matrix==1?(full?120:136):(full?183:208),bu=matrix==1?(full?475:541):(full?454:516);
  m->rgb[y*width+x]=(clamp((k*c+rv*e+128)>>8)<<16)|(clamp((k*c-gu*d-gv*e+128)>>8)<<8)|clamp((k*c+bu*d+128)>>8);
 }
 *milliseconds=m->video_time*1000/m->video.scale;m->video_time+=sample->duration;m->vi++;return 1;
}
static int audio_frame(pollikos_movie *m,int16_t pair[2]){
 while(m->decoded_at==m->decoded_frames){
  if(m->ai==m->audio.count)return 0;Sample *s=&m->audio.samples[m->ai++];NeAACDecFrameInfo info;
  m->decoded=NeAACDecDecode(m->aac,&info,(unsigned char *)m->data+s->offset,s->bytes);
  if(info.error||info.channels!=m->channels||info.samplerate!=m->rate||info.samples>2048*m->channels||(!m->decoded&&info.samples)){m->failed=1;return 0;}
  m->decoded_at=0;m->decoded_frames=(unsigned)(info.samples/m->channels);
  if(!m->decoded_frames){
   /* FAAD already discards its first 1024-sample priming frame. Count that
    * against an MP4 edit offset instead of trimming the same delay twice. */
   if(m->ai==1){uint64_t skip=m->audio_skip<1024?m->audio_skip:1024;m->audio_skip-=skip;}
   continue;
  }
 }
 if(m->audio_skip){unsigned skip=m->decoded_frames-m->decoded_at;if(skip>m->audio_skip)skip=(unsigned)m->audio_skip;m->decoded_at+=skip;m->audio_skip-=skip;return audio_frame(m,pair);}
 pair[0]=m->decoded[m->decoded_at*m->channels];pair[1]=m->channels==1?pair[0]:m->decoded[m->decoded_at*m->channels+1];m->decoded_at++;return 1;
}
size_t pollikos_movie_read_audio(pollikos_movie *m,int16_t *out,size_t frames){
 if(!m||!m->aac||!out||frames>SIZE_MAX/4)return 0;
 if(!m->have){if(!audio_frame(m,m->a))return 0;m->have=1;if(!audio_frame(m,m->b)){m->last=1;m->b[0]=m->a[0];m->b[1]=m->a[1];}}
 size_t n=0;while(n<frames&&m->have){
  for(unsigned c=0;c<2;c++)out[n*2+c]=(int16_t)(m->a[c]+((int64_t)m->b[c]-m->a[c])*m->phase/48000);n++;m->phase+=m->rate;
  while(m->phase>=48000){m->phase-=48000;if(m->last){m->have=0;break;}m->a[0]=m->b[0];m->a[1]=m->b[1];if(!audio_frame(m,m->b)){m->last=1;m->b[0]=m->a[0];m->b[1]=m->a[1];}}
 }return n;
}
