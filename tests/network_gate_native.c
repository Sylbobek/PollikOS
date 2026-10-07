#include <stdio.h>
#include <string.h>
#define POLLIK_X64 1
#include "../kernel/net/rtl8139.c"
static int enabled=1,writes;
int net_manager_enabled(void){return enabled;}
void serial(const char *s){(void)s;}
void net_on_frame_received(NetworkInterface *i,const u8 *f,int n){(void)i;(void)f;(void)n;}
void mac_to_string(const u8 *m,char *s){(void)m;strcpy(s,"test");}
void hal_port_write8(unsigned short p,unsigned char v){(void)p;(void)v;writes++;}
void hal_port_write16(unsigned short p,unsigned short v){(void)p;(void)v;writes++;}
void hal_port_write32(unsigned short p,unsigned int v){(void)p;(void)v;writes++;}
unsigned char hal_port_read8(unsigned short p){(void)p;return 1;}
unsigned short hal_port_read16(unsigned short p){(void)p;return 0;}
unsigned int hal_port_read32(unsigned short p){(void)p;return 0x2000;}
int main(void){
 NetworkInterface iface={0};u8 frame[60]={0};io=0x1000;
 enabled=0;int n=rtl8139_send_frame(&iface,frame,60);
 if(n||writes||iface.tx_packets){printf("FAIL disabled cached-interface send: result=%d port_writes=%d tx=%u\n",n,writes,iface.tx_packets);return 1;}
 enabled=1;if(!rtl8139_send_frame(&iface,frame,60)||iface.tx_packets!=1)return 2;
 printf("PASS Ethernet gate: disabled cached interface writes=0 tx=0; enabled tx=1\n");return 0;
}
