#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../common/ps2_pointer.h"
#include "../common/hid_pointer.h"
static Ps2Motion packet(Ps2Pointer *p,const unsigned char *b,unsigned now){Ps2Motion m={0};for(unsigned i=0;i<p->size;i++){int done=ps2_pointer_feed(p,b[i],now,&m);assert(done==(i==p->size-1));}return m;}
static Ps2Motion syn(Ps2Pointer *p,int x,int y,int z,int w,unsigned now){unsigned char b[]={0x80|((w&12)<<2)|((w&2)<<1),((x>>8)&15)|((y>>4)&0xf0),z,0xc0|((w&1)<<2)|((x>>8)&0x10)|((y>>7)&0x20),x,y};return packet(p,b,now);}
static void ps2_test(void){
    Ps2Pointer p={.size=4,.device_id=3};unsigned char b[]={0x38,0xfc,0xfa,0xff};Ps2Motion m=packet(&p,b,0);assert(m.dx==-4&&m.dy==6&&m.wheel==-1);
    b[0]=0xc9;m=packet(&p,b,1);assert(!m.dx&&!m.dy&&m.buttons==1);
    p=(Ps2Pointer){.size=6,.synaptics=1,.wmode=1,.multifinger=1,.palm=1};
    m=syn(&p,2000,2000,50,4,10);assert(!m.dx&&!m.dy);
    m=syn(&p,2024,1976,50,4,20);assert(m.dx==2&&m.dy==2);
    m=syn(&p,2024,1976,0,4,50);assert(m.tap);
    syn(&p,2000,2000,50,0,100);m=syn(&p,2000,1880,50,0,120);assert(m.wheel==1&&!m.dx&&!m.dy);
    m=syn(&p,2000,1880,0,0,140);assert(!m.tap);
    syn(&p,2000,2000,50,4,200);m=syn(&p,2500,2500,220,14,210);assert(!m.dx&&!m.dy&&!m.tap&&!p.contact);
    p=(Ps2Pointer){.size=3};ps2_pointer_feed(&p,8,0,&m);ps2_pointer_feed(&p,20,101,&m);assert(p.index==0);
    puts("PASS PS/2: relative/wheel, overflow buttons, Synaptics motion/tap/scroll/palm, stream resync");
}
static void hid_test(void){
    static const unsigned char mouse[]={5,1,9,2,0xa1,1,9,1,0xa1,0,5,9,0x19,1,0x29,3,0x15,0,0x25,1,0x95,3,0x75,1,0x81,2,0x95,1,0x75,5,0x81,1,5,1,9,0x30,9,0x31,9,0x38,0x15,0x81,0x25,0x7f,0x75,8,0x95,3,0x81,6,0xc0,0xc0};
    HidPointerLayout l;HidPointerReport r;assert(hid_pointer_parse(&l,mouse,sizeof(mouse)));
    unsigned char report[]={5,0xfc,7,0xff};assert(hid_pointer_decode(&l,report,4,&r));assert(r.buttons==5&&r.x==-4&&r.y==7&&r.wheel==-1&&!r.absolute);
    assert(!hid_pointer_decode(&l,report,2,&r));
    static const unsigned char touch[]={5,13,9,5,0xa1,1,0x85,7,9,0x22,0xa1,2,9,0x42,9,0x47,0x15,0,0x25,1,0x75,1,0x95,2,0x81,2,0x75,6,0x95,1,0x81,1,9,0x51,0x75,8,0x95,1,0x25,0xff,0x81,2,5,1,9,0x30,9,0x31,0x15,0,0x26,0xe8,3,0x75,16,0x95,2,0x81,2,0xc0,5,13,9,0x54,0x25,5,0x75,8,0x95,1,0x81,2,0xc0};
    assert(hid_pointer_parse(&l,touch,sizeof(touch))&&l.digitizer);
    unsigned char finger[]={7,3,4,0x20,3,0x58,2,2};assert(hid_pointer_decode(&l,finger,sizeof(finger),&r));assert(r.tip&&r.confidence&&r.contact_id==4&&r.contacts==2&&r.x==800&&r.y==600&&r.max_x==1000);
    finger[1]=2;finger[7]=0;assert(hid_pointer_decode(&l,finger,sizeof(finger),&r)&&!r.tip); /* lift */
    finger[0]=8;assert(!hid_pointer_decode(&l,finger,sizeof(finger),&r));
    finger[0]=7;finger[3]=0xff;finger[4]=0x7f;assert(!hid_pointer_decode(&l,finger,sizeof(finger),&r));
    unsigned char invalid[]={0x75,33,0x95,1,0x81,2};assert(!hid_pointer_parse(&l,invalid,sizeof(invalid)));
    assert(!hid_pointer_parse(&l,mouse,sizeof(mouse)-1)); /* unmatched collection */
    static const unsigned char wide[]={5,1,9,2,0xa1,1,9,0x30,9,0x31,0x17,0,0,0,0x80,0x27,0xff,0xff,0xff,0x7f,0x75,32,0x95,2,0x81,6,0xc0};
    assert(hid_pointer_parse(&l,wide,sizeof(wide)));
    unsigned char huge[]={0,0,0,0x80,0,0,0,0};assert(!hid_pointer_decode(&l,huge,sizeof(huge),&r));
    puts("PASS USB HID: descriptor layouts, signed mouse/wheel/buttons, digitizer/IDs/lift, invalid/truncated/out-of-range input");
}
int main(void){ps2_test();hid_test();return 0;}
