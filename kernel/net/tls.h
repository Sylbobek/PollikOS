#ifndef POLLIK_TLS_H
#define POLLIK_TLS_H

#include "tcp.h"

typedef struct TlsSocket TlsSocket;

void tls_init(void);
TlsSocket *tls_connect(TcpSocket *tcp_sock, const char *hostname, int timeout_ticks);
int tls_send(TlsSocket *tls_sock, const u8 *data, int len);
int tls_read(TlsSocket *tls_sock, u8 *buf, int max_len);
int tls_is_eof(TlsSocket *tls_sock);
void tls_close(TlsSocket *tls_sock);

#endif
