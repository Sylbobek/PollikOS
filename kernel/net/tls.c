#include "tls.h"
#include "net_manager.h"
#include "net_util.h"
#include "../hal.h"
#ifndef POLLIK_X64
#include "../mem.h"
#endif
#include "../include/bearssl/bearssl.h" // IWYU pragma: keep

#include "../certs/anchors.h"
#define NUM_TRUST_ANCHORS (sizeof(TRUST_ANCHORS) / sizeof(TRUST_ANCHORS[0]))

struct TlsSocket {
    TcpSocket *tcp_sock;
    br_ssl_client_context sc;
    br_x509_minimal_context xc;
#ifdef POLLIK_X64
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    u32 started_ticks;
    int active, ready, failed, eof;
#else
    br_sslio_context ioc;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    int timeout_ticks;
    int handshake_done;
#endif
};

#ifndef POLLIK_X64
static int sock_read(void *ctx, unsigned char *buf, size_t len) {
    TlsSocket *s = (TlsSocket *)ctx;
    if (!s || !s->tcp_sock)
        return -1;

    u32 start = ticks;
    while (!tcp_is_eof(s->tcp_sock)) {
        int r = tcp_read(s->tcp_sock, buf, (int)len);
        if (r > 0)
            return r;
        net_manager_poll();
        net_service_wait();

        if (ticks - start > (u32)s->timeout_ticks)
            break;
    }
    return -1;
}

static int sock_write(void *ctx, const unsigned char *buf, size_t len) {
    TlsSocket *s = (TlsSocket *)ctx;
    if (!s || !s->tcp_sock)
        return -1;

    int w = tcp_send(s->tcp_sock, buf, (int)len);
    return w > 0 ? w : -1;
}
#endif

static int tls_platform_entropy(unsigned char *out) {
    u32 a=1,b,c,d;
    hal_cpuid(a, 0, &a, &b, &c, &d);
    if (!(c & (1u<<30))) return 0;
    for (int i=0;i<8;i++) {
        u32 v=0; unsigned char ok=0;
        for(int j=0;j<10 && !ok;j++)
            ok=(unsigned char)hal_rdrand32(&v);
        if(!ok)return 0;
        memcpy(out+i*4,&v,4);
    }
    return 1;
}
static u8 rtc(u8 reg) { outb(0x70,reg); return inb(0x71); }
static int bcd(u8 v,int binary) {return binary?v:(v>>4)*10+(v&15);}
static int leap(int y) { return y%4==0 && (y%100!=0 || y%400==0); }
static int tls_platform_time(u32 *days,u32 *seconds) {
    u8 a[7], b[7]; const u8 regs[7]={0,2,4,7,8,9,0x0b};
    for(int tries=0;tries<100;tries++) {
        if(rtc(0x0a)&128)continue;
        for(int i=0;i<7;i++)a[i]=rtc(regs[i]);
        if(rtc(0x0a)&128)continue;
        for(int i=0;i<7;i++)b[i]=rtc(regs[i]);
        if(memcmp(a,b,7))continue;
        int bin=a[6]&4, sec=bcd(a[0],bin),min=bcd(a[1],bin),h=bcd(a[2]&127,bin);
        if(!(a[6]&2))h=(h%12)+((a[2]&128)?12:0);
        int day=bcd(a[3],bin),m=bcd(a[4],bin),y=2000+bcd(a[5],bin);
        static const int ml[]={31,28,31,30,31,30,31,31,30,31,30,31};
        if(m<1||m>12||day<1||day>ml[m-1]+(m==2&&leap(y))||h>23||min>59||sec>59)return 0;
        u32 n=0;for(int j=0;j<y;j++)n+=365+leap(j);
        for(int j=1;j<m;j++)n+=ml[j-1]+(j==2&&leap(y));
        *days=n+day-1;*seconds=h*3600+min*60+sec;return 1;
    }
    return 0;
}
void tls_init(void) {
}

#ifdef POLLIK_X64
static TlsSocket tls64;

int tls64_start(TcpSocket *tcp_sock, const char *hostname) {
    if (!tcp_sock || !hostname || !hostname[0]) return 0;
    memset(&tls64, 0, sizeof(tls64));
    tls64.tcp_sock = tcp_sock;
    tls64.started_ticks = ticks;
    br_ssl_client_init_full(&tls64.sc, &tls64.xc, TRUST_ANCHORS, NUM_TRUST_ANCHORS);
    br_ssl_engine_set_buffer(&tls64.sc.eng, tls64.iobuf, sizeof(tls64.iobuf), 1);
    unsigned char entropy[32];
    u32 days, seconds;
    if (!tls_platform_entropy(entropy) || !tls_platform_time(&days, &seconds)) {
        serial("TLS64: secure entropy or valid UTC RTC unavailable\n");
        memset(&tls64, 0, sizeof(tls64));
        return 0;
    }
    br_ssl_engine_inject_entropy(&tls64.sc.eng, entropy, sizeof(entropy));
    memset(entropy, 0, sizeof(entropy));
    br_x509_minimal_set_time(&tls64.xc, days, seconds);
    br_ssl_engine_set_versions(&tls64.sc.eng, BR_TLS12, BR_TLS12);
    u8 dummy_ip[4];
    const char *sni_name = net_parse_ip(hostname, dummy_ip) ? 0 : hostname;
    if (!br_ssl_client_reset(&tls64.sc, sni_name, 0)) {
        memset(&tls64, 0, sizeof(tls64));
        return 0;
    }
    tls64.active = 1;
    serial("TLS64: certificate-validated handshake started\n");
    return 1;
}

void tls64_poll(void) {
    if (!tls64.active || tls64.failed || tls64.eof) return;
    if (!tls64.ready && ticks - tls64.started_ticks > 3000u) {
        serial("TLS64: handshake timed out\n");
        tls64.failed = 1;
        return;
    }
    for (int budget = 0; budget < 8; ++budget) {
        unsigned state = br_ssl_engine_current_state(&tls64.sc.eng);
        if (state & BR_SSL_CLOSED) {
            int error = br_ssl_engine_last_error(&tls64.sc.eng);
            if (error) {
                char error_text[12]; number(error_text, (u32)error);
                serial("TLS64: record processing failed, code="); serial(error_text); serial("\n");
                tls64.failed = 1;
            } else tls64.eof = 1;
            return;
        }
        if ((state & BR_SSL_SENDAPP) && !tls64.ready) {
            serial("TLS64: certificate-validated handshake complete\n");
            tls64.ready = 1;
        }
        if ((state & BR_SSL_SENDREC) != 0) {
            size_t available = 0;
            unsigned char *record = br_ssl_engine_sendrec_buf(&tls64.sc.eng, &available);
            if (!record || !available) return;
            int amount = (int)available;
            if (amount > 1460) amount = 1460;
            int sent = tcp_send_nonblocking(tls64.tcp_sock, record, amount);
            if (sent <= 0) {
                /* SENDREC and RECVREC can be ready together. Keep draining
                 * inbound TLS records while TCP waits for the prior ACK. */
                if (!(state & BR_SSL_RECVREC)) return;
            } else {
                br_ssl_engine_sendrec_ack(&tls64.sc.eng, (size_t)sent);
                continue;
            }
        }
        if ((state & BR_SSL_RECVREC) != 0) {
            size_t available = 0;
            unsigned char *record = br_ssl_engine_recvrec_buf(&tls64.sc.eng, &available);
            if (!record || !available) return;
            int amount = (int)available;
            if (amount > 1460) amount = 1460;
            int received = tcp_read(tls64.tcp_sock, record, amount);
            if (received > 0) {
                br_ssl_engine_recvrec_ack(&tls64.sc.eng, (size_t)received);
                continue;
            }
            if (tcp_has_error(tls64.tcp_sock) || tcp_is_eof(tls64.tcp_sock))
                tls64.failed = 1;
            return;
        }
        return;
    }
}

int tls64_ready(void) { return tls64.active && tls64.ready && !tls64.failed; }
int tls64_failed(void) { return tls64.failed; }
int tls64_eof(void) { return tls64.eof; }

int tls64_write(const u8 *data, int len) {
    if (!tls64_ready() || !data || len <= 0) return 0;
    size_t available = 0;
    unsigned char *buffer = br_ssl_engine_sendapp_buf(&tls64.sc.eng, &available);
    if (!buffer || !available) return 0;
    if ((size_t)len > available) len = (int)available;
    memcpy(buffer, data, (size_t)len);
    br_ssl_engine_sendapp_ack(&tls64.sc.eng, (size_t)len);
    br_ssl_engine_flush(&tls64.sc.eng, 0);
    return len;
}

int tls64_read(u8 *buf, int max_len) {
    if (!tls64_ready() || !buf || max_len <= 0) return 0;
    size_t available = 0;
    unsigned char *buffer = br_ssl_engine_recvapp_buf(&tls64.sc.eng, &available);
    if (!buffer || !available) return 0;
    if ((size_t)max_len > available) max_len = (int)available;
    memcpy(buf, buffer, (size_t)max_len);
    br_ssl_engine_recvapp_ack(&tls64.sc.eng, (size_t)max_len);
    return max_len;
}

void tls64_abort(void) { memset(&tls64, 0, sizeof(tls64)); }

#else
TlsSocket *tls_connect(TcpSocket *tcp_sock, const char *hostname, int timeout_ticks) {
    if (!tcp_sock || !hostname)
        return 0;

    TlsSocket *s = (TlsSocket *)kmalloc(sizeof(TlsSocket));
    if (!s) {
        serial("TLS: kmalloc failed for TlsSocket\n");
        return 0;
    }
    memset(s, 0, sizeof(*s));
    s->tcp_sock = tcp_sock;
    s->timeout_ticks = timeout_ticks > 0 ? timeout_ticks : 400; /* 4s default */

    serial("TLS: Initializing BearSSL client context for ");
    serial(hostname);
    serial("...\n");

    br_ssl_client_init_full(&s->sc, &s->xc, TRUST_ANCHORS, NUM_TRUST_ANCHORS);
    br_ssl_engine_set_buffer(&s->sc.eng, s->iobuf, sizeof(s->iobuf), 1);
    unsigned char entropy[32];
    u32 days, seconds;
    if (!tls_platform_entropy(entropy) || !tls_platform_time(&days, &seconds)) {
        serial("TLS: secure entropy or valid UTC RTC unavailable\n");
        kfree(s); return 0;
    }
    br_ssl_engine_inject_entropy(&s->sc.eng, entropy, sizeof(entropy));
    memset(entropy, 0, sizeof(entropy));
    br_x509_minimal_set_time(&s->xc, days, seconds);
    br_ssl_engine_set_versions(&s->sc.eng, BR_TLS12, BR_TLS12);
    u8 dummy_ip[4];
    const char *sni_name = net_parse_ip(hostname, dummy_ip) ? 0 : hostname;
    if (!br_ssl_client_reset(&s->sc, sni_name, 0)) { kfree(s); return 0; }

    br_sslio_init(&s->ioc, &s->sc.eng, sock_read, s, sock_write, s);

    /* flush drives the handshake until application data may be sent. State is a bitmask. */
    if (br_sslio_flush(&s->ioc) < 0) {
        serial("TLS: handshake I/O failed\n");
    }

    if (br_ssl_engine_current_state(&s->sc.eng) == BR_SSL_CLOSED) {
        int err = br_ssl_engine_last_error(&s->sc.eng);
        serial("TLS: Handshake failed error code: ");
        char ebuf[12];
        number(ebuf, (u32)err);
        serial(ebuf);
        serial("\n");
        kfree(s);
        return 0;
    }

    s->handshake_done = 1;
    serial("TLS: Handshake successful! Encrypted channel ready.\n");
    return s;
}

int tls_send(TlsSocket *s, const u8 *data, int len) {
    if (!s || !s->handshake_done || !data || len <= 0)
        return 0;

    int res = br_sslio_write_all(&s->ioc, data, (size_t)len);
    if (res < 0)
        return 0;
    br_sslio_flush(&s->ioc);
    return len;
}

int tls_read(TlsSocket *s, u8 *buf, int max_len) {
    if (!s || !s->handshake_done || !buf || max_len <= 0)
        return 0;

    int r = br_sslio_read(&s->ioc, buf, (size_t)max_len);
    return r > 0 ? r : 0;
}

int tls_is_eof(TlsSocket *s) {
    if (!s || !s->handshake_done)
        return 1;
    return br_ssl_engine_current_state(&s->sc.eng) == BR_SSL_CLOSED;
}

void tls_close(TlsSocket *s) {
    if (!s)
        return;
    if (s->handshake_done) {
        br_sslio_close(&s->ioc);
    }
    if (s->tcp_sock) {
        tcp_close(s->tcp_sock);
        s->tcp_sock = 0;
    }
    kfree(s);
}
#endif
