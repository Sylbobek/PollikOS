#ifndef POLLIK_UTF8_H
#define POLLIK_UTF8_H
#include <stddef.h>
#include <stdint.h>
/* Bounded decoder: malformed input consumes one byte and displays '?'. */
static inline uint32_t pollik_utf8_next(const char *text,size_t length,size_t *used) {
    *used=0;if(!length)return 0;
    const unsigned char *s=(const unsigned char *)text;
    uint32_t value=s[0];size_t count;
    *used=1;if(value<128)return value;
    if(value>=0xc2 && value<=0xdf){value&=31;count=2;}
    else if(value>=0xe0 && value<=0xef){value&=15;count=3;}
    else if(value>=0xf0 && value<=0xf4){value&=7;count=4;}
    else return '?';
    if(count>length)return '?';
    for(size_t i=1;i<count;i++){if((s[i]&0xc0)!=0x80)return '?';value=(value<<6)|(s[i]&63);}
    if((count==3 && value<0x800)||(count==4 && value<0x10000)||
       (value>=0xd800 && value<=0xdfff)||value>0x10ffff)return '?';
    *used=count;return value;
}
static inline size_t pollik_utf8_previous(const char *text,size_t cursor) {
    if(!cursor)return 0;
    size_t at=cursor-1,used;
    while(at && cursor-at<4 && ((unsigned char)text[at]&0xc0)==0x80)--at;
    (void)pollik_utf8_next(text+at,cursor-at,&used);
    return used==cursor-at?at:cursor-1;
}
static inline size_t pollik_utf8_encode(uint32_t value,char out[4]) {
    if(value<0x80){out[0]=(char)value;return 1;}
    if(value<0x800){out[0]=(char)(0xc0|(value>>6));out[1]=(char)(0x80|(value&63));return 2;}
    if(value>=0xd800 && value<=0xdfff)return 0;
    if(value<0x10000){out[0]=(char)(0xe0|(value>>12));out[1]=(char)(0x80|((value>>6)&63));out[2]=(char)(0x80|(value&63));return 3;}
    if(value>0x10ffff)return 0;
    out[0]=(char)(0xf0|(value>>18));out[1]=(char)(0x80|((value>>12)&63));out[2]=(char)(0x80|((value>>6)&63));out[3]=(char)(0x80|(value&63));return 4;
}
#endif
