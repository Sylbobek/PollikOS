#ifndef POLLIK_HID_POINTER_H
#define POLLIK_HID_POINTER_H
/* Bounded HID short-item/report decoder; no VID/PID-specific report layouts. */
typedef struct {unsigned offset,size,usage,page,id,group,relative;int minimum,maximum;} HidPointerField;
typedef struct {HidPointerField fields[64];unsigned count;int has_pointer,digitizer;} HidPointerLayout;
typedef struct {int x,y,wheel,max_x,max_y,absolute,tip,confidence,contacts,contact_id;unsigned buttons;} HidPointerReport;
static inline int hid_signed(unsigned v,unsigned bytes){if(bytes==1)return (signed char)v;if(bytes==2)return (short)v;return (int)v;}
static inline int hid_pointer_parse(HidPointerLayout *layout,const unsigned char *data,unsigned bytes){
    *layout=(HidPointerLayout){0};
    struct Global {unsigned page,size,count,id;int minimum,maximum;} g={0},saved[4];unsigned depth=0;
    unsigned ids[16]={0},offsets[16]={0},nids=1,current=0,usages[64],nusage=0,first=0,last=0;
    unsigned group=0,groups=0,collection[16],collection_depth=0;
    for(unsigned at=0;at<bytes;){unsigned prefix=data[at++];if(prefix==0xfe)return 0;
        unsigned size=prefix&3;if(size==3)size=4;if(size>bytes-at)return 0;
        unsigned value=0;for(unsigned i=0;i<size;i++)value|=(unsigned)data[at++]<<(i*8);
        unsigned type=(prefix>>2)&3,tag=prefix>>4;
        if(type==1){switch(tag){
            case 0:g.page=value;break;case 1:g.minimum=hid_signed(value,size);break;
            case 2:g.maximum=g.minimum<0?hid_signed(value,size):(int)value;break;
            case 7:if(value>32)return 0;g.size=value;break;
            case 8:if(!value||value>255)return 0;g.id=value;current=0;while(current<nids&&ids[current]!=value)current++;if(current==nids){if(nids==16)return 0;ids[nids++]=value;}break;
            case 9:if(value>64)return 0;g.count=value;break;
            case 10:if(depth==4)return 0;saved[depth++]=g;break;
            case 11:if(!depth)return 0;g=saved[--depth];current=0;while(current<nids&&ids[current]!=g.id)current++;break;
        }}else if(type==2){if(tag==0){if(nusage==64)return 0;usages[nusage++]=value;}else if(tag==1)first=value;else if(tag==2)last=value;
        }else if(type==0){
            if(tag==10){if(collection_depth==16)return 0;collection[collection_depth++]=group;if(g.page==13&&nusage&&usages[0]==0x22){if(groups==15)return 0;group=++groups;layout->digitizer=1;}}
            else if(tag==12){if(!collection_depth)return 0;group=collection[--collection_depth];}
            else if(tag==8){
                if(!g.size||g.count*g.size>2048-offsets[current])return 0;
                for(unsigned i=0;i<g.count;i++){
                    unsigned usage=i<nusage?usages[i]:(last>=first&&first+i<=last?first+i:0),page=g.page;
                    if(usage>65535){page=usage>>16;usage&=65535;}
                    int relevant=(page==1&&(usage==0x30||usage==0x31||usage==0x38))||(page==9&&usage>=1&&usage<=16)||
                        (page==13&&(usage==0x42||usage==0x47||usage==0x51||usage==0x54));
                    if(!(value&1)&&(value&2)&&relevant){if(layout->count==64)return 0;
                        HidPointerField *f=&layout->fields[layout->count++];*f=(HidPointerField){offsets[current]+i*g.size,g.size,usage,page,g.id,group,(value&4)!=0,g.minimum,g.maximum};
                        if(page==1&&usage==0x30)layout->has_pointer=1;
                        if(page==13)layout->digitizer=1;
                    }
                }offsets[current]+=g.count*g.size;
            }
            nusage=first=last=0;
        }
    }return !depth&&!collection_depth&&layout->has_pointer;
}
static inline int hid_pointer_decode(const HidPointerLayout *layout,const unsigned char *data,unsigned bytes,HidPointerReport *r){
    *r=(HidPointerReport){.confidence=1};int values[64];unsigned valid[64],chosen=0,first_group=16;int has_x=0,has_y=0;
    for(unsigned i=0;i<layout->count;i++){const HidPointerField *f=&layout->fields[i];unsigned offset=f->offset+(f->id?8:0);
        valid[i]=(!f->id||(bytes&&data[0]==f->id))&&offset+f->size<=bytes*8;if(!valid[i])continue;
        unsigned v=0;for(unsigned b=0;b<f->size;b++)v|=((data[(offset+b)/8]>>((offset+b)&7))&1u)<<b;
        values[i]=f->minimum<0&&f->size<32&&(v&(1u<<(f->size-1)))?(int)(v|(~0u<<f->size)):(int)v;
        if(f->page==1&&f->usage==0x30&&f->group<first_group)first_group=f->group;
        if(f->page==13&&f->usage==0x42&&values[i]&&(!chosen||f->group<chosen))chosen=f->group;
        if(f->page==13&&f->usage==0x54)r->contacts=values[i];
    }
    if(!chosen&&first_group<16)chosen=first_group; /* Lift reports still carry the previous finger's coordinates. */
    for(unsigned i=0;i<layout->count;i++)if(valid[i]){const HidPointerField *f=&layout->fields[i];int v=values[i];
        if(f->page==9&&v)r->buttons|=1u<<(f->usage-1);
        if(f->group!=chosen)continue;
        if(f->page==1&&(f->usage==0x30||f->usage==0x31)&&!f->relative){
            long long range=(long long)f->maximum-f->minimum;
            if(range<=0||range>10000000||v<f->minimum||v>f->maximum)return 0;
        }
        if(f->page==1&&f->usage==0x30&&!has_x){r->x=v;r->absolute=!f->relative;r->max_x=f->relative?0:f->maximum-f->minimum; if(r->absolute)r->x-=f->minimum;has_x=1;}
        if(f->page==1&&f->usage==0x31&&!has_y){r->y=v;r->max_y=f->relative?0:f->maximum-f->minimum;if(!f->relative)r->y-=f->minimum;has_y=1;}
        if(f->page==1&&f->usage==0x38)r->wheel=v;
        if(f->page==13){if(f->usage==0x42)r->tip=v;if(f->usage==0x47)r->confidence=v;if(f->usage==0x51)r->contact_id=v;}
    }
    if(!has_x||!has_y)return 0;
    if(!r->absolute&&(r->x<-32767||r->x>32767||r->y<-32767||r->y>32767))return 0;
    if(r->wheel<-32767||r->wheel>32767)return 0;
    if(r->absolute&&(r->max_x<=0||r->max_y<=0||r->max_x>10000000||r->max_y>10000000))return 0;
    return 1;
}
#endif
