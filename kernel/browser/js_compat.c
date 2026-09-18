#include "../../third_party/elk/pollik_compat.h"
static void put(char *b,size_t cap,int *n,char c){if((size_t)*n+1<cap)b[*n]=c;(*n)++;}
int vsnprintf(char *b,size_t cap,const char *f,va_list args) {
 int n=0;
 while(*f) {
  if(*f!='%'){put(b,cap,&n,*f++);continue;}f++;
  int limit=-1;if(*f=='.'){f++;if(*f=='*'){limit=va_arg(args,int);f++;}else while(*f>='0'&&*f<='9')f++;}
  if(*f=='l')f++;
  if(*f=='s'){const char *s=va_arg(args,const char*);int i=0;while(s&&*s&&(limit<0||i++<limit))put(b,cap,&n,*s++);}
  else if(*f=='g') {
   double d=va_arg(args,double);
   if(d!=d){put(b,cap,&n,'N');put(b,cap,&n,'a');put(b,cap,&n,'N');}
   else if(d>2147483647.0||d< -2147483647.0)put(b,cap,&n,'?');
   else {if(d<0){put(b,cap,&n,'-');d=-d;}u32 v=(u32)d;char t[12];number(t,v);for(char *s=t;*s;s++)put(b,cap,&n,*s);d-=v;if(d>0.000001){put(b,cap,&n,'.');for(int i=0;i<6&&d>0.000001;i++){d*=10;int q=(int)d;put(b,cap,&n,'0'+q);d-=q;}}}
  } else if(*f=='d'||*f=='u'||*f=='x') {
   u32 v=va_arg(args,u32);if(*f=='d'&&(int)v<0){put(b,cap,&n,'-');v=0-v;}
   char t[32];int k=0,base=*f=='x'?16:10;do{t[k++]="0123456789abcdef"[v%base];v/=base;}while(v);while(k)put(b,cap,&n,t[--k]);
  } else put(b,cap,&n,*f);
  if(*f)f++;
 }
 if(cap)b[(size_t)n<cap?(size_t)n:cap-1]=0;return n;
}
int snprintf(char *b,size_t cap,const char *f,...){va_list a;va_start(a,f);int n=vsnprintf(b,cap,f,a);va_end(a);return n;}
double strtod(const char *p,char **end) {
 while(*p==' ')p++;int sign=1;if(*p=='-'){sign=-1;p++;}else if(*p=='+')p++;
 double d=0;while(*p>='0'&&*p<='9')d=d*10+*p++-'0';
 if(*p=='.'){p++;double scale=.1;while(*p>='0'&&*p<='9'){d+=(*p++-'0')*scale;scale*=.1;}}
 if(*p=='e'||*p=='E'){p++;int s=1,e=0;if(*p=='-'){s=-1;p++;}else if(*p=='+')p++;while(*p>='0'&&*p<='9'){if(e<308)e=e*10+*p-'0';p++;}if(e>308)e=308;while(e--)d=s>0?d*10:d/10;}
 if(end)*end=(char*)p;return d*sign;
}
