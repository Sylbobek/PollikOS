#ifndef POLLIK_TCP_H
#define POLLIK_TCP_H

#include "net_if.h"

typedef struct TcpSocket TcpSocket;

void tcp_init(void);
TcpSocket *tcp_connect(NetworkInterface *iface, const u8 *remote_ip, u16 remote_port, int timeout_ticks);
TcpSocket *tcp_connect_start(NetworkInterface *iface, const u8 *remote_ip, u16 remote_port);
int tcp_send(TcpSocket *sock, const u8 *data, int len);
int tcp_send_nonblocking(TcpSocket *sock, const u8 *data, int len);
int tcp_read(TcpSocket *sock, u8 *buf, int max_len);
int tcp_is_connected(TcpSocket *sock);
int tcp_is_eof(TcpSocket *sock);
int tcp_has_error(TcpSocket *sock);
void tcp_abort(TcpSocket *sock);
void tcp_close(TcpSocket *sock);
void tcp_poll_all(void);
void tcp_on_packet(NetworkInterface *iface, const u8 *src_ip, const u8 *dst_ip, const u8 *payload, int len);

#endif
