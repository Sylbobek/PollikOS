#include "tcp.h"
#include "ipv4.h"
#include "net_util.h"
#ifndef POLLIK_X64
#include "../mem.h"
#endif

#define MAX_TCP_SOCKETS 4
#define TCP_RX_BUFFER_SIZE 32768
#define TCP_MSS 1460

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

typedef enum {
    TCP_STATE_CLOSED = 0,
    TCP_STATE_SYN_SENT,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT_1,
    TCP_STATE_FIN_WAIT_2,
    TCP_STATE_CLOSE_WAIT,
    TCP_STATE_LAST_ACK,
    TCP_STATE_TIME_WAIT
} TcpState;

struct TcpSocket {
    int active;
    TcpState state;
    NetworkInterface *iface;
    u8 remote_ip[4];
    u16 local_port;
    u16 remote_port;
    u32 snd_una;
    u32 snd_nxt;
    u32 rcv_nxt;
    u16 rcv_wnd;

    u32 last_sent_tick;
    u8 last_sent_packet[1500];
    int last_sent_len;
    int retrans_count;

    u8 *rx_buf;
    u32 rx_head;
    u32 rx_tail;
    int eof;
    int error;
};

static TcpSocket sockets[MAX_TCP_SOCKETS];
#ifdef POLLIK_X64
/* The x86_64 kernel has no general heap yet. Give each bounded TCP socket a
 * fixed receive ring so packet paths never borrow userspace memory. */
static u8 tcp_rx_pool[MAX_TCP_SOCKETS][TCP_RX_BUFFER_SIZE];
#endif
static u16 next_ephemeral_port = 49200;
static u32 initial_seq = 0x20260913;

static void tcp_send_segment(TcpSocket *s, u8 flags, const u8 *data, int data_len) {
    if (!s || !s->iface)
        return;

    int tcp_hdr_len = 20;
    int opt_len = (flags & TCP_FLAG_SYN) ? 4 : 0;
    int total_tcp_len = tcp_hdr_len + opt_len + data_len;

    u8 packet[1500];
    memset(packet, 0, (unsigned)total_tcp_len);

    net_put16(packet + 0, s->local_port);
    net_put16(packet + 2, s->remote_port);
    net_put32(packet + 4, s->snd_nxt);
    net_put32(packet + 8, s->rcv_nxt);
    packet[12] = (u8)(((tcp_hdr_len + opt_len) / 4) << 4);
    packet[13] = flags;
    net_put16(packet + 14, s->rcv_wnd);
    net_put16(packet + 16, 0); /* Checksum placeholder */
    net_put16(packet + 18, 0); /* Urgent pointer */

    if (opt_len == 4) {
        /* MSS Option: 1460 */
        packet[20] = 2; /* Kind */
        packet[21] = 4; /* Length */
        net_put16(packet + 22, TCP_MSS);
    }

    if (data && data_len > 0)
        memcpy(packet + tcp_hdr_len + opt_len, data, (unsigned)data_len);

    u16 csum = net_tcp_udp_checksum(s->iface->ip, s->remote_ip, IPV4_PROTO_TCP, packet, total_tcp_len);
    net_put16(packet + 16, csum == 0 ? 0xffff : csum);

    /* Store for retransmission if packet contains payload or SYN/FIN */
    if (data_len > 0 || (flags & (TCP_FLAG_SYN | TCP_FLAG_FIN))) {
        memcpy(s->last_sent_packet, packet, (unsigned)total_tcp_len);
        s->last_sent_len = total_tcp_len;
        s->last_sent_tick = ticks;
    }

    ipv4_send(s->iface, IPV4_PROTO_TCP, s->remote_ip, packet, total_tcp_len);
}

void tcp_init(void) {
    memset(sockets, 0, sizeof(sockets));
}

TcpSocket *tcp_connect_start(NetworkInterface *iface, const u8 *remote_ip, u16 remote_port) {
    if (!iface || !remote_ip)
        return 0;

    int slot = -1;
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (!sockets[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        serial("TCP: No free sockets\n");
        return 0;
    }

    TcpSocket *s = &sockets[slot];
    memset(s, 0, sizeof(*s));
    s->active = 1;
    s->iface = iface;
    memcpy(s->remote_ip, remote_ip, 4);
    s->remote_port = remote_port;

    next_ephemeral_port++;
    if (next_ephemeral_port < 49152 || next_ephemeral_port > 65000)
        next_ephemeral_port = 49152;
    s->local_port = next_ephemeral_port;

    initial_seq += 10007;
    s->snd_nxt = initial_seq;
    s->snd_una = initial_seq;
    s->rcv_nxt = 0;
    s->rcv_wnd = 16384;
    s->state = TCP_STATE_SYN_SENT;
    s->retrans_count = 0;

#ifdef POLLIK_X64
    s->rx_buf = tcp_rx_pool[slot];
#else
    s->rx_buf = (u8 *)kmalloc(TCP_RX_BUFFER_SIZE);
#endif
    if (!s->rx_buf) {
        s->active = 0;
        return 0;
    }
    s->rx_head = 0;
    s->rx_tail = 0;
    s->eof = 0;
    s->error = 0;

    serial("TCP: Connecting to port ");
    char pbuf[12];
    number(pbuf, remote_port);
    serial(pbuf);
    serial("...\n");

    /* Send SYN. The asynchronous API leaves polling to the kernel timer. */
    tcp_send_segment(s, TCP_FLAG_SYN, 0, 0);
    s->snd_nxt++; /* SYN consumes 1 sequence number */

    return s;
}

TcpSocket *tcp_connect(NetworkInterface *iface, const u8 *remote_ip, u16 remote_port, int timeout_ticks) {
    TcpSocket *s = tcp_connect_start(iface, remote_ip, remote_port);
    if (!s) return 0;

    u32 start = ticks;
    while (s->state == TCP_STATE_SYN_SENT && (ticks - start < (u32)timeout_ticks)) {
        if (iface->poll)
            iface->poll(iface);
        tcp_poll_all();
        net_service_wait();
    }

    if (s->state == TCP_STATE_ESTABLISHED) {
        serial("TCP: Connection ESTABLISHED\n");
        return s;
    }

    serial("TCP: Connect failed/timeout\n");
    tcp_close(s);
    return 0;
}

int tcp_send(TcpSocket *s, const u8 *data, int len) {
    if (!s || !s->active || s->state != TCP_STATE_ESTABLISHED || !data || len <= 0)
        return 0;

    int sent = 0;
    while (sent < len) {
        int chunk = len - sent;
        if (chunk > TCP_MSS)
            chunk = TCP_MSS;

        tcp_send_segment(s, TCP_FLAG_ACK | TCP_FLAG_PSH, data + sent, chunk);
        s->snd_nxt += (u32)chunk;
        sent += chunk;

        /* Wait briefly for ACK with poll */
        u32 t0 = ticks;
        while (s->snd_una != s->snd_nxt && !s->error && (ticks - t0 < 500)) {
            if (s->iface->poll)
                s->iface->poll(s->iface);
            tcp_poll_all();
            net_service_wait();
        }
        if (s->snd_una != s->snd_nxt || s->error) return 0;
    }
    return sent;
}

int tcp_send_nonblocking(TcpSocket *s, const u8 *data, int len) {
    if (!s || !s->active || s->state != TCP_STATE_ESTABLISHED || !data || len <= 0 ||
        len > TCP_MSS || s->snd_una != s->snd_nxt)
        return 0;
    tcp_send_segment(s, TCP_FLAG_ACK | TCP_FLAG_PSH, data, len);
    s->snd_nxt += (u32)len;
    return len;
}

int tcp_read(TcpSocket *s, u8 *buf, int max_len) {
    if (!s || !s->active || !s->rx_buf || !buf || max_len <= 0)
        return 0;

    int read_bytes = 0;
    while (read_bytes < max_len && s->rx_tail != s->rx_head) {
        buf[read_bytes++] = s->rx_buf[s->rx_tail];
        s->rx_tail = (s->rx_tail + 1) % TCP_RX_BUFFER_SIZE;
    }
    return read_bytes;
}

int tcp_is_connected(TcpSocket *s) {
    return s && s->active && (s->state == TCP_STATE_ESTABLISHED || s->state == TCP_STATE_CLOSE_WAIT);
}

int tcp_is_eof(TcpSocket *s) {
    if (!s || !s->active)
        return 1;
    return (s->eof || s->error) && (s->rx_tail == s->rx_head);
}

int tcp_has_error(TcpSocket *s) {
    return !s || !s->active || s->error;
}

void tcp_abort(TcpSocket *s) {
    if (!s || !s->active) return;
    if (s->rx_buf) {
#ifndef POLLIK_X64
        kfree(s->rx_buf);
#endif
        s->rx_buf = 0;
    }
    s->active = 0;
    s->state = TCP_STATE_CLOSED;
}

void tcp_close(TcpSocket *s) {
    if (!s || !s->active)
        return;

    if (s->state == TCP_STATE_ESTABLISHED || s->state == TCP_STATE_CLOSE_WAIT) {
        tcp_send_segment(s, TCP_FLAG_ACK | TCP_FLAG_FIN, 0, 0);
        s->snd_nxt++;
        s->state = TCP_STATE_FIN_WAIT_1;
        u32 t0 = ticks;
        while (s->state != TCP_STATE_CLOSED && (ticks - t0 < 50)) {
            if (s->iface && s->iface->poll)
                s->iface->poll(s->iface);
            net_service_wait();
        }
    }

    if (s->rx_buf) {
#ifndef POLLIK_X64
        kfree(s->rx_buf);
#endif
        s->rx_buf = 0;
    }
    s->active = 0;
    s->state = TCP_STATE_CLOSED;
}

void tcp_poll_all(void) {
    u32 now = ticks;
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        TcpSocket *s = &sockets[i];
        if (!s->active)
            continue;

        /* Retransmission check */
        if (s->snd_una < s->snd_nxt && s->last_sent_len > 0) {
            if (now - s->last_sent_tick > 60) { /* ~600ms */
                if (s->retrans_count < 5) {
                    s->retrans_count++;
                    s->last_sent_tick = now;
                    if (s->iface && s->iface->send_frame) {
                        ipv4_send(s->iface, IPV4_PROTO_TCP, s->remote_ip, s->last_sent_packet, s->last_sent_len);
                        serial("TCP: Retransmission segment\n");
                    }
                } else {
                    serial("TCP: Retransmission failed, aborting connection\n");
                    s->error = 1;
                    s->state = TCP_STATE_CLOSED;
                }
            }
        }
    }
}

void tcp_on_packet(NetworkInterface *iface, const u8 *src_ip, const u8 *dst_ip, const u8 *payload, int len) {
    if (len >= 20 && net_tcp_udp_checksum(src_ip, dst_ip, IPV4_PROTO_TCP, payload, len)) return;
    if (len < 20)
        return;

    u16 src_port = net_be16(payload + 0);
    u16 dst_port = net_be16(payload + 2);
    u32 seq = net_be32(payload + 4);
    u32 ack = net_be32(payload + 8);
    int hdr_len = ((payload[12] >> 4) & 15) * 4;
    u8 flags = payload[13];

    if (hdr_len < 20 || len < hdr_len)
        return;

    TcpSocket *s = 0;
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (sockets[i].active && sockets[i].iface == iface && sockets[i].local_port == dst_port &&
            sockets[i].remote_port == src_port && net_same(sockets[i].remote_ip, src_ip, 4)) {
            s = &sockets[i];
            break;
        }
    }
    if (!s)
        return;

    /* Handle RST */
    if (flags & TCP_FLAG_RST) {
        serial("TCP: RST received\n");
        s->error = 1;
        s->state = TCP_STATE_CLOSED;
        return;
    }

    /* Process ACK */
    if (flags & TCP_FLAG_ACK) {
        if (ack >= s->snd_una && ack <= s->snd_nxt) {
            s->snd_una = ack;
            s->retrans_count = 0;
        }
    }

    if (s->state == TCP_STATE_SYN_SENT) {
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) && ack == s->snd_nxt) {
            s->rcv_nxt = seq + 1;
            s->snd_una = ack;
            s->state = TCP_STATE_ESTABLISHED;
            /* Send ACK */
            tcp_send_segment(s, TCP_FLAG_ACK, 0, 0);
        }
        return;
    }

    if (s->state == TCP_STATE_ESTABLISHED || s->state == TCP_STATE_FIN_WAIT_1 || s->state == TCP_STATE_FIN_WAIT_2) {
        int data_len = len - hdr_len;
        if (data_len > 0 && seq == s->rcv_nxt) {
            u32 used = (s->rx_head + TCP_RX_BUFFER_SIZE - s->rx_tail) % TCP_RX_BUFFER_SIZE;
            if ((u32)data_len > TCP_RX_BUFFER_SIZE - 1 - used) {
                tcp_send_segment(s, TCP_FLAG_ACK, 0, 0);
                return;
            }
            const u8 *data = payload + hdr_len;
            for (int i = 0; i < data_len; i++) {
                u32 next_head = (s->rx_head + 1) % TCP_RX_BUFFER_SIZE;
                if (next_head != s->rx_tail) {
                    s->rx_buf[s->rx_head] = data[i];
                    s->rx_head = next_head;
                }
            }
            s->rcv_nxt += (u32)data_len;
            /* Send ACK */
            tcp_send_segment(s, TCP_FLAG_ACK, 0, 0);
        }

        if (data_len > 0 && seq != s->rcv_nxt - (u32)data_len) tcp_send_segment(s, TCP_FLAG_ACK, 0, 0);
        if ((flags & TCP_FLAG_FIN) && seq + (u32)data_len == s->rcv_nxt) {
            s->rcv_nxt++;
            s->eof = 1;
            tcp_send_segment(s, TCP_FLAG_ACK, 0, 0);
            if (s->state == TCP_STATE_ESTABLISHED) {
                s->state = TCP_STATE_CLOSE_WAIT;
            } else if (s->state == TCP_STATE_FIN_WAIT_1 || s->state == TCP_STATE_FIN_WAIT_2) {
                s->state = TCP_STATE_CLOSED;
            }
        }
    }
}
