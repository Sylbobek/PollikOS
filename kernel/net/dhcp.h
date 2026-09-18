#ifndef POLLIK_DHCP_H
#define POLLIK_DHCP_H

#include "net_if.h"

void dhcp_init(void);
void dhcp_start(NetworkInterface *iface);
void dhcp_poll(NetworkInterface *iface);
int dhcp_is_bound(NetworkInterface *iface);
const char *dhcp_get_status(void);

#endif
