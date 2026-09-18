#include "tls.h"
#include "net_manager.h"
#include "net_util.h"
#include "../mem.h"
#include "../include/bearssl/bearssl.h"

#include "../certs/anchors.h"
#define NUM_TRUST_ANCHORS (sizeof(TRUST_ANCHORS) / sizeof(TRUST_ANCHORS[0]))

struct TlsSocket {
    TcpSocket *tcp_sock;
    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    br_sslio_context ioc;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    int timeout_ticks;
    int handshake_done;
};

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

static int tls_platform_entropy(unsigned char *out) {
    u32 a=1,b,c,d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    if (!(c & (1u<<30))) return 0;
    for (int i=0;i<8;i++) {
        u32 v=0; unsigned char ok=0;
        for(int j=0;j<10 && !ok;j++)
            __asm__ volatile("rdrand %0; setc %1" : "=r"(v), "=qm"(ok));
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
