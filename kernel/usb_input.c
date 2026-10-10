#include <libpayload.h>
#include <usb/usb.h>
#include "usb_input.h"
#include "hal.h"
#include "../common/hid_pointer.h"
extern int usb_platform_start(void);
extern unsigned usb_platform_millis(void);
extern void serial(const char *text);
extern hci_t *usb_hcs;
extern int usb_initialize(void);
static int initialized,polling;
static UsbPointerEvent pointer_queue[128];static unsigned pointer_head,pointer_count;
static unsigned char key_queue[128];static unsigned key_head,key_count;
typedef struct {endpoint_t *ep;void *queue;HidPointerLayout layout;unsigned interface,size,keyboard,buttons,started;u8 keys[8];int contact,last_x,last_y,last_id,start_x,start_y,tap,rem_x,rem_y,scroll;} HidInterface;
typedef struct HidDevice {HidInterface interfaces[8];unsigned count;struct HidDevice *next;} HidDevice;
static HidDevice *hid_devices;
static struct {hci_t *controller;int (*control)(usbdev_t *,direction_t,int,void *,int,u8 *);} controls[16];static unsigned control_count;
static void pointer_event(UsbPointerEvent event){
    if(pointer_count==128){pointer_head=(pointer_head+1)%128;pointer_count--;}
    pointer_queue[(pointer_head+pointer_count)%128]=event;pointer_count++;
}
static unsigned pointer_buttons(void){unsigned buttons=0;for(HidDevice *d=hid_devices;d;d=d->next)for(unsigned i=0;i<d->count;i++)buttons|=d->interfaces[i].buttons;return buttons;}
static void key_byte(u8 scan){if(key_count==128){key_head=(key_head+1)%128;key_count--;}key_queue[(key_head+key_count)%128]=scan;key_count++;}
/* HID usage IDs map to the existing scan-code decoder, including modifiers. */
static unsigned key_scan(unsigned usage){
    static const u8 letters[]={30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};
    if(usage>=4&&usage<=29)return letters[usage-4];if(usage>=30&&usage<=38)return usage-28;if(usage==39)return 11;
    switch(usage){case 40:return 28;case 41:return 1;case 42:return 14;case 43:return 15;case 44:return 57;case 45:return 12;case 46:return 13;case 47:return 26;case 48:return 27;case 49:return 43;case 51:return 39;case 52:return 40;case 53:return 41;case 54:return 51;case 55:return 52;case 56:return 53;case 57:return 58;
    case 79:return 0x14d;case 80:return 0x14b;case 81:return 0x150;case 82:return 0x148;case 74:return 0x147;case 77:return 0x14f;case 76:return 0x153;default:if(usage>=58&&usage<=67)return usage+1;if(usage==68)return 87;if(usage==69)return 88;return 0;}
}
static void key_event(unsigned scan,int released){if(!scan)return;if(scan&0x100)key_byte(0xe0);key_byte((u8)((scan&127)|(released?128:0)));}
static void keyboard_report(HidInterface *iface,const u8 *data){
    for(unsigned i=2;i<8;i++)if(data[i]>=1&&data[i]<=3)return; /* Boot rollover is not a release. */
    static const unsigned modifiers[]={29,42,56,0x15b,0x11d,54,0x138,0x15c};
    for(unsigned b=0;b<8;b++)if((data[0]^iface->keys[0])&(1u<<b))key_event(modifiers[b],!(data[0]&(1u<<b)));
    for(unsigned i=2;i<8;i++)if(iface->keys[i]>3){int found=0;for(unsigned j=2;j<8;j++)if(data[j]==iface->keys[i])found=1;if(!found)key_event(key_scan(iface->keys[i]),1);}
    for(unsigned i=2;i<8;i++)if(data[i]>3){int found=0;for(unsigned j=2;j<8;j++)if(iface->keys[j]==data[i])found=1;if(!found)key_event(key_scan(data[i]),0);}
    memcpy(iface->keys,data,8);
}
static void hid_poll(usbdev_t *dev){
    HidDevice *hid=dev->data;if(!hid)return;
    for(unsigned i=0;i<hid->count;i++){HidInterface *iface=&hid->interfaces[i];u8 *data;
        for(unsigned batch=0;batch<32&&(data=dev->controller->poll_intr_queue(iface->queue));batch++){
            if(iface->keyboard){keyboard_report(iface,data);continue;}
            HidPointerReport r;if(!hid_pointer_decode(&iface->layout,data,iface->size,&r))continue;
            iface->buttons=r.buttons;
            UsbPointerEvent event={.dx=r.x,.dy=r.y,.wheel=r.wheel,.buttons=r.buttons,.absolute=r.absolute,.x=r.x,.y=r.y,.max_x=r.max_x,.max_y=r.max_y};
            if(iface->layout.digitizer){event.absolute=0;event.dx=event.dy=0;
                if(!r.tip||!r.confidence){
                    if(!r.tip&&r.confidence&&iface->contact&&iface->tap&&usb_platform_millis()-iface->started<=200&&!r.buttons){UsbPointerEvent tap=event;tap.buttons=pointer_buttons()|1;pointer_event(tap);}
                    iface->contact=0;iface->rem_x=iface->rem_y=iface->scroll=0;
                }
                else if(!iface->contact||iface->last_id!=r.contact_id){iface->contact=1;iface->last_id=r.contact_id;iface->last_x=iface->start_x=r.x;iface->last_y=iface->start_y=r.y;iface->started=usb_platform_millis();iface->tap=r.contacts<2&&!r.buttons;}
                else {int dx=r.x-iface->last_x,dy=r.y-iface->last_y;iface->last_x=r.x;iface->last_y=r.y;
                    if(dx>r.max_x/4||dx<-r.max_x/4||dy>r.max_y/4||dy<-r.max_y/4){iface->tap=0;iface->rem_x=iface->rem_y=iface->scroll=0;continue;}
                    if(r.x-iface->start_x>r.max_x/50||iface->start_x-r.x>r.max_x/50||r.y-iface->start_y>r.max_y/50||iface->start_y-r.y>r.max_y/50||r.buttons)iface->tap=0;
                    if(r.contacts>=2){iface->tap=0;iface->scroll+=dy*40;event.wheel=iface->scroll/r.max_y;iface->scroll%=r.max_y;iface->rem_x=iface->rem_y=0;}
                    else{iface->rem_x+=dx*400;iface->rem_y+=dy*400;event.dx=iface->rem_x/r.max_x;event.dy=iface->rem_y/r.max_y;iface->rem_x%=r.max_x;iface->rem_y%=r.max_y;}
                }
            }
            event.buttons=pointer_buttons();pointer_event(event);
        }
    }
}
static void hid_destroy(usbdev_t *dev){HidDevice *hid=dev->data;if(!hid)return;
    for(unsigned i=0;i<hid->count;i++){HidInterface *iface=&hid->interfaces[i];if(iface->keyboard){u8 empty[8]={0};keyboard_report(iface,empty);}dev->controller->destroy_intr_queue(iface->ep,iface->queue);}
    HidDevice **link=&hid_devices;while(*link&&*link!=hid)link=&(*link)->next;if(*link)*link=hid->next;
    pointer_event((UsbPointerEvent){.buttons=pointer_buttons()});usb_lp_free(hid);dev->data=NULL;
}
void usb_hid_init(usbdev_t *dev){
    HidDevice *hid=usb_lp_malloc(sizeof(*hid));if(!hid)return;
    unsigned total=dev->configuration->wTotalLength;u8 *base=(u8 *)dev->configuration;interface_descriptor_t *intf=NULL;unsigned report_size=0;
    for(unsigned at=dev->configuration->bLength;at+2<=total&&base[at]>=2&&base[at]<=total-at;at+=base[at]){
        u8 *desc=base+at;
        if(desc[1]==DT_INTF){intf=desc[0]>=sizeof(*intf)?(void *)desc:NULL;report_size=0;}
        if(!intf||intf->bInterfaceClass!=3||intf->bAlternateSetting)continue;
        if(desc[1]==0x21&&desc[0]>=9&&desc[6]==0x22)report_size=desc[7]|(unsigned)desc[8]<<8;
        if(desc[1]!=DT_ENDP||desc[0]<7||!(desc[2]&0x80)||(desc[3]&3)!=3||hid->count==8)continue;
        endpoint_t *ep=NULL;for(int e=1;e<dev->num_endp;e++)if(dev->endpoints[e].endpoint==desc[2])ep=&dev->endpoints[e];if(!ep||ep->maxpacketsize<=0||ep->maxpacketsize>128)continue;
        HidInterface *iface=&hid->interfaces[hid->count];memset(iface,0,sizeof(*iface));iface->ep=ep;iface->interface=intf->bInterfaceNumber;iface->size=(unsigned)ep->maxpacketsize;
        iface->keyboard=intf->bInterfaceSubClass==1&&intf->bInterfaceProtocol==1;
        if(iface->keyboard){if(iface->size<8)continue;}
        else {if(!report_size||report_size>4096)continue;u8 *report=usb_lp_malloc(report_size);if(!report)continue;
            dev_req_t req={.bmRequestType=0x81,.bRequest=GET_DESCRIPTOR,.wValue=0x2200,.wIndex=iface->interface,.wLength=report_size};
            int got=dev->controller->control(dev,IN,sizeof(req),&req,(int)report_size,report);
            int supported=got>0&&hid_pointer_parse(&iface->layout,report,(unsigned)got);
            if(!supported){usb_lp_printf("USB HID interface %u report rejected (%d/%u bytes)\n",iface->interface,got,report_size);
#ifdef USB_DEBUG
                for(int b=0;b<got&&b<128;b++)usb_lp_printf("%02x ",report[b]);serial("\n");
#endif
            }
            usb_lp_free(report);if(!supported)continue;
        }
        dev_req_t mode={.bmRequestType=0x21,.bRequest=0x0b,.wValue=iface->keyboard?0:1,.wIndex=iface->interface};
        if(intf->bInterfaceSubClass==1)(void)dev->controller->control(dev,OUT,sizeof(mode),&mode,0,NULL);
        dev_req_t idle={.bmRequestType=0x21,.bRequest=0x0a,.wIndex=iface->interface};(void)dev->controller->control(dev,OUT,sizeof(idle),&idle,0,NULL);
        iface->queue=dev->controller->create_intr_queue(ep,(int)iface->size,16,8);
        if(!iface->queue){serial("USB HID interrupt queue unavailable\n");continue;}
        hid->count++;serial(iface->keyboard?"USB HID keyboard ready\n":iface->layout.digitizer?"USB HID touchpad ready\n":"USB HID pointer ready\n");
    }
    if(!hid->count){usb_lp_free(hid);return;}hid->next=hid_devices;hid_devices=hid;dev->data=hid;dev->poll=hid_poll;dev->destroy=hid_destroy;
}
static int control_bounce(usbdev_t *dev,direction_t dir,int req_len,void *req,int data_len,u8 *data){
    int (*original)(usbdev_t *,direction_t,int,void *,int,u8 *)=NULL;for(unsigned i=0;i<control_count;i++)if(controls[i].controller==dev->controller)original=controls[i].control;
    if(!original||req_len<0||req_len>64||data_len<0||data_len>65536)return -1;
    void *rq=usb_lp_malloc((size_t)req_len);u8 *buf=data_len?usb_lp_malloc((size_t)data_len):NULL;
    if(!rq||(data_len&&!buf)){usb_lp_free(rq);usb_lp_free(buf);return -1;}
    memcpy(rq,req,(size_t)req_len);if(data_len&&dir!=IN)memcpy(buf,data,(size_t)data_len);
    int result=original(dev,dir,req_len,rq,data_len,buf);if(result>0&&dir==IN&&data){int count=result<data_len?result:data_len;memcpy(data,buf,(size_t)count);}
    usb_lp_free(rq);usb_lp_free(buf);return result;
}
void usb_input_init(void){
    if(initialized)return;initialized=1;
    /* Avoid reserving DMA RAM on machines without any USB host controller. */
    int found=0;for(unsigned bus=0;bus<256&&!found;bus++)for(unsigned dev=0;dev<32&&!found;dev++)for(unsigned fn=0;fn<8;fn++){pcidev_t p=PCI_DEV(bus,dev,fn);if((pci_read_config32(p,8)>>16)==0x0c03){found=1;break;}if(!fn&&pci_read_config16(p,0)==0xffff)break;}
    if(!found||!usb_platform_start())return;
    usb_initialize();
    for(hci_t *c=usb_hcs;c&&control_count<16;c=c->next){controls[control_count].controller=c;controls[control_count++].control=c->control;c->control=control_bounce;}
    serial("USB host controllers initialized\n");
}
void usb_input_poll(void){if(!initialized||polling)return;polling=1;usb_poll();polling=0;}
int usb_input_read(UsbPointerEvent *out){if(!pointer_count)return 0;*out=pointer_queue[pointer_head];pointer_head=(pointer_head+1)%128;pointer_count--;return 1;}
int usb_input_read_key(u8 *scan){if(!key_count)return 0;*scan=key_queue[key_head];key_head=(key_head+1)%128;key_count--;return 1;}
