#ifndef POLLIK_PS2_POINTER_H
#define POLLIK_PS2_POINTER_H
/* New-format Synaptics PS/2 plus standard/IntelliMouse compatibility.
 * Protocol register/packet definitions: Linux drivers/input/mouse/synaptics.
 * No platform I/O, allocation or UI callbacks are hidden in this decoder. */
typedef struct {int dx,dy,wheel;unsigned buttons;int tap;} Ps2Motion;
typedef struct {
    unsigned char bytes[6];unsigned size,index,device_id;
    int synaptics,wmode,multifinger,palm,clickpad,passthrough;
    int contact,last_x,last_y,start_x,start_y,remainder_x,remainder_y,scroll;
    unsigned started,last_byte;int tap_possible;
} Ps2Pointer;
typedef int (*Ps2Send)(unsigned char value);
typedef int (*Ps2Read)(void);
static inline int ps2_sliced(Ps2Send send,unsigned char value){
    if(!send(0xe6))return 0;
    for(int shift=6;shift>=0;shift-=2)if(!send(0xe8)||!send((value>>shift)&3))return 0;
    return 1;
}
static inline int ps2_query(Ps2Send send,Ps2Read read,unsigned query,unsigned char out[3]){
    if(!ps2_sliced(send,(unsigned char)query)||!send(0xe9))return 0;
    for(int i=0;i<3;i++){int byte=read();if(byte<0)return 0;out[i]=(unsigned char)byte;}return 1;
}
static inline int ps2_pointer_configure(Ps2Pointer *p,Ps2Send send,Ps2Read read){
    *p=(Ps2Pointer){.size=3};
    if(!send(0xf5)||!send(0xf6))return 0;
    unsigned char identity[3],model[3],caps[3]={0},ext[3]={0};
    if(ps2_query(send,read,0,identity)&&identity[1]==0x47&&ps2_query(send,read,3,model)&&(model[2]&0x80)){
        if(ps2_query(send,read,2,caps)&&(caps[0]&0x80)){
            p->wmode=1;p->multifinger=(caps[2]&2)!=0;p->palm=(caps[2]&1)!=0;p->passthrough=(caps[2]&0x80)!=0;
            if(((caps[0]>>4)&7)>=4&&ps2_query(send,read,0x0c,ext))p->clickpad=(ext[0]&0x10)!=0;
        }
        unsigned mode=0xc4u|(p->wmode?1u:0u);
        if(ps2_sliced(send,(unsigned char)mode)&&send(0xf3)&&send(0x14)){
            p->synaptics=1;p->size=6;return send(0xf4);
        }
    }
    /* A failed probe must restore standard relative mode before wheel unlock. */
    *p=(Ps2Pointer){.size=3};if(!send(0xf6))return 0;
    if(send(0xf3)&&send(200)&&send(0xf3)&&send(100)&&send(0xf3)&&send(80)&&send(0xf2)){
        int id=read();if(id==3||id==4){p->size=4;p->device_id=(unsigned)id;}
    }
    return send(0xf4);
}
static inline void ps2_pointer_stream_reset(Ps2Pointer *p){p->index=0;p->contact=0;p->tap_possible=0;p->remainder_x=p->remainder_y=p->scroll=0;}
static inline int ps2_pointer_feed(Ps2Pointer *p,unsigned char value,unsigned now,Ps2Motion *m){
    if(p->index&&now-p->last_byte>100)ps2_pointer_stream_reset(p);p->last_byte=now;
    if(p->synaptics){
        if((p->index==0&&(value&0xc8)!=0x80)||(p->index==3&&(value&0xc8)!=0xc0)){
            p->index=0;if((value&0xc8)!=0x80)return 0;
        }
    }else if(!p->index&&!(value&8))return 0;
    p->bytes[p->index++]=value;if(p->index<p->size)return 0;p->index=0;
    *m=(Ps2Motion){0};unsigned char *b=p->bytes;
    if(!p->synaptics){
        m->buttons=b[0]&7;
        if(!(b[0]&0xc0)){m->dx=(int)b[1]-((b[0]&16)?256:0);m->dy=-((int)b[2]-((b[0]&32)?256:0));}
        if(p->size==4){m->wheel=p->device_id==4?((b[3]&8)?(int)(b[3]&15)-16:(b[3]&15)):(signed char)b[3];if(p->device_id==4)m->buttons|=(b[3]&0x30)>>1;}
        return 1;
    }
    int w=p->wmode?((b[0]&0x30)>>2)|((b[0]&4)>>1)|((b[3]&4)>>2):4;
    m->buttons=b[0]&3;if(p->clickpad&&((b[0]^b[3])&1))m->buttons|=1;
    if(w==3&&p->passthrough){
        m->buttons=b[1]&7;
        if(!(b[1]&0xc0)){m->dx=(int)b[4]-((b[1]&16)?256:0);m->dy=-((int)b[5]-((b[1]&32)?256:0));}
        return 1;
    }
    if(w==2||w==3)return 0; /* AGM packets are not ordinary contacts. */
    int x=((b[3]&0x10)<<8)|((b[1]&15)<<8)|b[4],y=((b[3]&0x20)<<7)|((b[1]&0xf0)<<4)|b[5];
    int contact=p->contact?b[2]>=25:b[2]>30;
    if((p->palm&&w>=12)||b[2]>=200){contact=0;p->tap_possible=0;}
    if(!contact){
        m->tap=p->contact&&p->tap_possible&&now-p->started<=200&&!m->buttons;
        p->contact=0;p->remainder_x=p->remainder_y=p->scroll=0;return 1;
    }
    if(!p->contact){p->contact=1;p->last_x=p->start_x=x;p->last_y=p->start_y=y;p->started=now;p->tap_possible=!m->buttons;return 1;}
    int dx=x-p->last_x,dy=p->last_y-y;p->last_x=x;p->last_y=y;
    if(dx>800||dx<-800||dy>800||dy<-800){p->tap_possible=0;return 1;}
    if(x-p->start_x>120||p->start_x-x>120||y-p->start_y>120||p->start_y-y>120||m->buttons)p->tap_possible=0;
    if(p->multifinger&&w<=1){p->tap_possible=0;p->scroll+=dy;m->wheel=p->scroll/120;p->scroll%=120;p->remainder_x=p->remainder_y=0;}
    else {p->remainder_x+=dx;p->remainder_y+=dy;m->dx=p->remainder_x/12;m->dy=p->remainder_y/12;p->remainder_x%=12;p->remainder_y%=12;}
    return 1;
}
#endif
