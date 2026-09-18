#include "udp.h"
#include "ipv4.h"
#include "net_util.h"

#define MAX_UDP_SOCKETS 8

typedef struct {
    u16 port;
    udp_callback_t callback;
    int active;
} UdpSocket;

static UdpSocket udp_sockets[MAX_UDP_SOCKETS];

void udp_init(void) {
    memset(udp_sockets, 0, sizeof(udp_sockets));
}

int udp_bind(u16 local_port, udp_callback_t cb) {
    for (int i = 0; i < MAX_UDP_SOCKETS; i++) {
        if (!udp_sockets[i].active || udp_sockets[i].port == local_port) {
            udp_sockets[i].port = local_port;
            udp_sockets[i].callback = cb;
            udp_sockets[i].active = 1;
            return 1;
        }
    }
    return 0;
}

void udp_unbind(u16 local_port) {
    for (int i = 0; i < MAX_UDP_SOCKETS; i++) {
        if (udp_sockets[i].active && udp_sockets[i].port == local_port) {
            udp_sockets[i].active = 0;
            udp_sockets[i].callback = 0;
            break;
        }
    }
}

int udp_send(NetworkInterface *iface, const u8 *dst_ip, u16 src_port, u16 dst_port, const u8 *data, int len) {
    if (!iface || !dst_ip || len < 0 || len > 1472)
        return 0;

    int udp_len = 8 + len;
    u8 packet[1500];
    net_put16(packet + 0, src_port);
    net_put16(packet + 2, dst_port);
    net_put16(packet + 4, (u16)udp_len);
    net_put16(packet + 6, 0); /* Optional checksum in IPv4 */

    if (data && len > 0)
        memcpy(packet + 8, data, (unsigned)len);

    /* Compute UDP checksum if sending */
    u16 csum = net_tcp_udp_checksum(iface->ip, dst_ip, IPV4_PROTO_UDP, packet, udp_len);
    net_put16(packet + 6, csum == 0 ? 0xffff : csum);

    return ipv4_send(iface, IPV4_PROTO_UDP, dst_ip, packet, udp_len);
}

void udp_on_packet(NetworkInterface *iface, const u8 *src_ip, const u8 *dst_ip, const u8 *payload, int len) {

    if (len < 8)
        return;

    u16 src_port = net_be16(payload + 0);
    u16 dst_port = net_be16(payload + 2);
    u16 udp_len = net_be16(payload + 4);

    if (udp_len < 8 || len < udp_len)
        return;

    if (net_be16(payload+6) && net_tcp_udp_checksum(src_ip,dst_ip,IPV4_PROTO_UDP,payload,udp_len))return;
    const u8 *data = payload + 8;
    int data_len = udp_len - 8;

    for (int i = 0; i < MAX_UDP_SOCKETS; i++) {
        if (udp_sockets[i].active && udp_sockets[i].port == dst_port && udp_sockets[i].callback) {
            udp_sockets[i].callback(iface, src_ip, src_port, data, data_len);
            break;
        }
    }
}
