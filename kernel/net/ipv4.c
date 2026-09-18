#include "ipv4.h"
#include "arp.h"
#include "net_util.h"

/* Forward declarations for higher layer handlers */
void icmp_on_packet(NetworkInterface *iface, const u8 *src_ip, const u8 *payload, int len);
void udp_on_packet(NetworkInterface *iface, const u8 *src_ip, const u8 *dst_ip, const u8 *payload, int len);
void tcp_on_packet(NetworkInterface *iface, const u8 *src_ip, const u8 *dst_ip, const u8 *payload, int len);

static u16 ip_packet_id = 0x1000;

int ipv4_send(NetworkInterface *iface, u8 protocol, const u8 *dst_ip, const u8 *payload, int payload_len) {
    if (!iface || !iface->send_frame || !dst_ip || !payload || payload_len < 0)
        return 0;

    int total_ip_len = 20 + payload_len;
    if (total_ip_len > iface->mtu)
        return 0;

    u8 dst_mac[6];
    int is_broadcast = (dst_ip[0] == 255 && dst_ip[1] == 255 && dst_ip[2] == 255 && dst_ip[3] == 255);

    if (is_broadcast) {
        memset(dst_mac, 0xff, 6);
    } else {
        /* Determine next hop IP: local subnet vs gateway */
        const u8 *next_hop_ip = dst_ip;
        int on_subnet = 1;
        for (int i = 0; i < 4; i++) {
            if ((dst_ip[i] & iface->netmask[i]) != (iface->ip[i] & iface->netmask[i])) {
                on_subnet = 0;
                break;
            }
        }
        if (!on_subnet) {
            next_hop_ip = iface->gateway;
        }

        if (!arp_lookup(next_hop_ip, dst_mac)) {
            /* Not in cache, send ARP request */
            arp_request(iface, next_hop_ip);
            return 0;
        }
    }

    u8 frame[1536];
    memset(frame, 0, sizeof(frame));

    /* Ethernet header */
    memcpy(frame, dst_mac, 6);
    memcpy(frame + 6, iface->mac, 6);
    net_put16(frame + 12, 0x0800); /* EtherType: IPv4 */

    /* IPv4 header */
    u8 *ip_hdr = frame + 14;
    ip_hdr[0] = 0x45;              /* Version 4, IHL 5 (20 bytes) */
    ip_hdr[1] = 0;                 /* DSCP/ECN */
    net_put16(ip_hdr + 2, (u16)total_ip_len);
    net_put16(ip_hdr + 4, ++ip_packet_id);
    net_put16(ip_hdr + 6, 0x4000); /* Don't fragment */
    ip_hdr[8] = 64;                /* TTL */
    ip_hdr[9] = protocol;
    net_put16(ip_hdr + 10, 0);     /* Checksum placeholder */
    memcpy(ip_hdr + 12, iface->ip, 4);
    memcpy(ip_hdr + 16, dst_ip, 4);

    net_put16(ip_hdr + 10, net_checksum(ip_hdr, 20));

    /* Payload */
    memcpy(frame + 14 + 20, payload, (unsigned)payload_len);

    int total_frame_len = 14 + total_ip_len;
    if (total_frame_len < 60)
        total_frame_len = 60;

    return iface->send_frame(iface, frame, total_frame_len);
}

void ipv4_on_packet(NetworkInterface *iface, const u8 *frame, int len) {
    if (len < 14 + 20)
        return;

    const u8 *ip_hdr = frame + 14;
    u8 ver_ihl = ip_hdr[0];
    if ((ver_ihl >> 4) != 4)
        return; /* Not IPv4 */

    int ihl = (ver_ihl & 0x0f) * 4;
    if (ihl < 20 || len < 14 + ihl)
        return;

    u16 total_len = net_be16(ip_hdr + 2);
    if (total_len < ihl || len < 14 + total_len)
        return;

    if (net_checksum(ip_hdr, ihl) != 0)
        return; /* Corrupt IPv4 header */

    if (net_be16(ip_hdr + 6) & 0x3fff) return; /* No fragment reassembly yet. */
    const u8 *src_ip = ip_hdr + 12;
    const u8 *dst_ip = ip_hdr + 16;

    /* Verify packet is addressed to our IP or broadcast */
    int is_for_us = net_same(dst_ip, iface->ip, 4);
    int is_bcast = (dst_ip[0] == 255 && dst_ip[1] == 255 && dst_ip[2] == 255 && dst_ip[3] == 255);
    int is_zero = (iface->ip[0] == 0 && iface->ip[1] == 0 && iface->ip[2] == 0 && iface->ip[3] == 0);

    if (!is_for_us && !is_bcast && !is_zero)
        return;

    u8 protocol = ip_hdr[9];
    const u8 *payload = ip_hdr + ihl;
    int payload_len = total_len - ihl;

    if (protocol == IPV4_PROTO_ICMP) {
        icmp_on_packet(iface, src_ip, payload, payload_len);
    } else if (protocol == IPV4_PROTO_UDP) {
        udp_on_packet(iface, src_ip, dst_ip, payload, payload_len);
    } else if (protocol == IPV4_PROTO_TCP) {
        tcp_on_packet(iface, src_ip, dst_ip, payload, payload_len);
    }
}
