#ifndef POLLIK_WIFI_IF_H
#define POLLIK_WIFI_IF_H

#include "net_if.h"

int wifi_init(NetworkInterface *iface);
void wifi_set_state(int connected, const char *ssid);
const char *wifi_get_ssid(void);
int wifi_is_connected(void);

#endif
