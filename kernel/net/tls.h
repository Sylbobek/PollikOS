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

#ifdef POLLIK_X64
/* Single-request, nonblocking TLS transport used by the x86_64 browser. */
int tls64_start(TcpSocket *tcp_sock, const char *hostname);
void tls64_poll(void);
int tls64_ready(void);
int tls64_failed(void);
int tls64_eof(void);
int tls64_write(const u8 *data, int len);
int tls64_read(u8 *buf, int max_len);
void tls64_abort(void);
#endif

#endif
