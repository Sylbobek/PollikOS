#include "devices.h"
#include "audio_stream.h"
#include "user_abi.h"
#include "network.h"
#include "console_fb.h"
#include "../../audio.h"
#include "../../net/net_manager.h"
int64_t devices64_control(uint64_t op,uint64_t value){
    switch(op){
    case USER_DEVICE_CAPS:return (net_manager_get_ethernet_iface()?1:0)|
        (net_manager_get_wifi_iface()?2:0)|(audio_is_available()?8:0)|16|
        (console_fb_width()?32:0);
    case USER_DEVICE_CONNECTED:return network64_connected();
    case USER_DEVICE_AIRPLANE_GET:return !net_manager_enabled();
    case USER_DEVICE_AIRPLANE_SET:
        if(value>1)return -USER_EINVAL;network64_set_airplane((int)value);return 0;
    case USER_DEVICE_VOLUME_GET:return audio_get_volume();
    case USER_DEVICE_VOLUME_SET:
        if(value>100)return -USER_EINVAL;audio_set_volume((u8)value);return 0;
    case USER_DEVICE_OUTPUT_MASK:return 1|(audio_is_available()?2:0);
    case USER_DEVICE_OUTPUT_GET:return audio_output();
    case USER_DEVICE_OUTPUT_SET:
        if(value>1)return -USER_EINVAL;
        if(audio64_stream_active())return -USER_EAGAIN;
        return audio_select_output((int)value)?0:-USER_ENOTSUP;
    case USER_DEVICE_TEST_TONE:
        if(audio64_stream_active())return -USER_EAGAIN;
        audio_play_tone(880,20);return 0;
    case USER_DEVICE_FB_WIDTH:return console_fb_width();
    case USER_DEVICE_FB_HEIGHT:return console_fb_height();
    case USER_DEVICE_FB_PITCH:return console_fb_pitch();
    case USER_DEVICE_FB_BPP:return console_fb_bpp();
    case USER_DEVICE_DISK_BYTES:return (int64_t)fs64_disk_bytes();
    case USER_DEVICE_TX_PACKETS:case USER_DEVICE_RX_PACKETS:{
        NetworkInterface *iface=net_manager_get_ethernet_iface();if(!iface)return 0;
        return op==USER_DEVICE_TX_PACKETS?iface->tx_packets:iface->rx_packets;
    }
    /* No USB host controller, WLAN/BT driver or radio stack exists yet.
     * Unsupported operations never pretend to toggle or discover a radio. */
    case USER_DEVICE_WIFI_SET:case USER_DEVICE_BT_SET:
        if(value>1)return -USER_EINVAL;
        return -USER_ENOTSUP;
    case USER_DEVICE_WIFI_SCAN:case USER_DEVICE_BT_SCAN:return -USER_ENOTSUP;
    default:return -USER_EINVAL;
    }
}
