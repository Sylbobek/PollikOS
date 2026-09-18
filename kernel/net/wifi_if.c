#include "wifi_if.h"
/* No supported radio driver exists in this kernel. Never advertise a fake link. */
int wifi_init(NetworkInterface *iface) {
    if (iface) {
        memset(iface, 0, sizeof(*iface));
        iface->name = "wlan0";
        iface->type = IF_TYPE_WIFI;
    }
    serial("WIFI: no supported hardware driver\n");
    return 0;
}
void wifi_set_state(int connected, const char *ssid) { (void)connected; (void)ssid; }
const char *wifi_get_ssid(void) { return ""; }
int wifi_is_connected(void) { return 0; }
