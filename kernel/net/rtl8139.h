#ifndef POLLIK_RTL8139_H
#define POLLIK_RTL8139_H

#include "net_if.h"

int rtl8139_init(NetworkInterface *iface);
int rtl8139_send_frame(NetworkInterface *iface, const u8 *frame, int len);
void rtl8139_poll(NetworkInterface *iface);
int rtl8139_is_link_up(NetworkInterface *iface);

#endif
