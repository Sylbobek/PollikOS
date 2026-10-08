#include "tls.h"
#include "net_manager.h"
#include "net_util.h"
#include "../hal.h"
#ifdef POLLIK_X64
#include "../arch/x86_64/paging.h"
#endif
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
#define TLS64_BASE (MM_KERNEL_START+UINT64_C(0x3c000000))
static unsigned tls_slots;
_Static_assert(sizeof(TlsSocket)<=65536,"TLS context slot");
static TlsSocket *tls_allocate(void) {
    unsigned slot=0;while(slot<8 && (tls_slots&(1u<<slot))) ++slot;
    if(slot==8) return 0;
    uintptr_t base=TLS64_BASE+slot*65536;size_t pages=(sizeof(TlsSocket)+4095)/4096,mapped=0;
    while(mapped<pages && vmm64_alloc_page(vmm64_kernel(),base+mapped*4096,VM_WRITE)==VM_OK) ++mapped;
    if(mapped!=pages) { while(mapped) { --mapped;memory_require(vmm64_unmap(vmm64_kernel(),base+mapped*4096,1,0)==VM_OK,"TLS rollback"); }return 0; }
    tls_slots|=1u<<slot;return (TlsSocket *)base;
}

TlsSocket *tls64_start(TcpSocket *tcp_sock, const char *hostname) {
    if (!tcp_sock || !hostname || !hostname[0]) return 0;
    TlsSocket *tls_sock=tls_allocate();if(!tls_sock) return 0;
    memset(tls_sock,0,sizeof(*tls_sock));
    tls_sock->tcp_sock = tcp_sock;
    tls_sock->started_ticks = ticks;
    br_ssl_client_init_full(&tls_sock->sc, &tls_sock->xc, TRUST_ANCHORS, NUM_TRUST_ANCHORS);
    br_ssl_engine_set_buffer(&tls_sock->sc.eng, tls_sock->iobuf, sizeof(tls_sock->iobuf), 1);
    unsigned char entropy[32];
    u32 days, seconds;
    if (!tls_platform_entropy(entropy) || !tls_platform_time(&days, &seconds)) {
        serial("TLS64: secure entropy or valid UTC RTC unavailable\n");
        tls64_abort(tls_sock);
        return 0;
    }
    br_ssl_engine_inject_entropy(&tls_sock->sc.eng, entropy, sizeof(entropy));
    memset(entropy, 0, sizeof(entropy));
    br_x509_minimal_set_time(&tls_sock->xc, days, seconds);
    br_ssl_engine_set_versions(&tls_sock->sc.eng, BR_TLS12, BR_TLS12);
    u8 dummy_ip[4];
    /* BearSSL's DNS-name verifier does not implement IP SAN matching. Do not
     * disable hostname validation to make numeric-IP HTTPS appear supported. */
    if(net_parse_ip(hostname,dummy_ip)) { tls64_abort(tls_sock);return 0; }
    const char *sni_name = hostname;
    if (!br_ssl_client_reset(&tls_sock->sc, sni_name, 0)) {
        tls64_abort(tls_sock);
        return 0;
    }
    tls_sock->active = 1;
    serial("TLS64: certificate-validated handshake started\n");
    return tls_sock;
}

void tls64_poll(TlsSocket *tls_sock) {
    if (!tls_sock || !tls_sock->active || tls_sock->failed || tls_sock->eof) return;
    if (!tls_sock->ready && ticks - tls_sock->started_ticks > 3000u) {
        serial("TLS64: handshake timed out\n");
        tls_sock->failed = 1;
        return;
    }
    for (int budget = 0; budget < 8; ++budget) {
        unsigned state = br_ssl_engine_current_state(&tls_sock->sc.eng);
        if (state & BR_SSL_CLOSED) {
            int error = br_ssl_engine_last_error(&tls_sock->sc.eng);
            if (error) {
                char error_text[12]; number(error_text, (u32)error);
                serial("TLS64: record processing failed, code="); serial(error_text); serial("\n");
                tls_sock->failed = 1;
            } else tls_sock->eof = 1;
            return;
        }
        if ((state & BR_SSL_SENDAPP) && !tls_sock->ready) {
            serial("TLS64: certificate-validated handshake complete\n");
            tls_sock->ready = 1;
        }
        if ((state & BR_SSL_SENDREC) != 0) {
            size_t available = 0;
            unsigned char *record = br_ssl_engine_sendrec_buf(&tls_sock->sc.eng, &available);
            if (!record || !available) return;
            int amount = (int)available;
            if (amount > 1460) amount = 1460;
            int sent = tcp_send_nonblocking(tls_sock->tcp_sock, record, amount);
            if (sent <= 0) {
                /* SENDREC and RECVREC can be ready together. Keep draining
                 * inbound TLS records while TCP waits for the prior ACK. */
                if (!(state & BR_SSL_RECVREC)) return;
            } else {
                br_ssl_engine_sendrec_ack(&tls_sock->sc.eng, (size_t)sent);
                continue;
            }
        }
        if ((state & BR_SSL_RECVREC) != 0) {
            size_t available = 0;
            unsigned char *record = br_ssl_engine_recvrec_buf(&tls_sock->sc.eng, &available);
            if (!record || !available) return;
            int amount = (int)available;
            if (amount > 1460) amount = 1460;
            int received = tcp_read(tls_sock->tcp_sock, record, amount);
            if (received > 0) {
                br_ssl_engine_recvrec_ack(&tls_sock->sc.eng, (size_t)received);
                continue;
            }
            if (tcp_has_error(tls_sock->tcp_sock) || tcp_is_eof(tls_sock->tcp_sock))
                tls_sock->failed = 1;
            return;
        }
        return;
    }
}

int tls64_ready(TlsSocket *tls_sock) { return tls_sock && tls_sock->active && tls_sock->ready && !tls_sock->failed; }
int tls64_failed(TlsSocket *tls_sock) { return !tls_sock || tls_sock->failed; }
int tls64_eof(TlsSocket *tls_sock) { return tls_sock && tls_sock->eof; }

int tls64_write(TlsSocket *tls_sock,const u8 *data, int len) {
    if (!tls64_ready(tls_sock) || !data || len <= 0) return 0;
    size_t available = 0;
    unsigned char *buffer = br_ssl_engine_sendapp_buf(&tls_sock->sc.eng, &available);
    if (!buffer || !available) return 0;
    if ((size_t)len > available) len = (int)available;
    memcpy(buffer, data, (size_t)len);
    br_ssl_engine_sendapp_ack(&tls_sock->sc.eng, (size_t)len);
    br_ssl_engine_flush(&tls_sock->sc.eng, 0);
    return len;
}

int tls64_read(TlsSocket *tls_sock,u8 *buf, int max_len) {
    if (!tls64_ready(tls_sock) || !buf || max_len <= 0) return 0;
    size_t available = 0;
    unsigned char *buffer = br_ssl_engine_recvapp_buf(&tls_sock->sc.eng, &available);
    if (!buffer || !available) return 0;
    if ((size_t)max_len > available) max_len = (int)available;
    memcpy(buf, buffer, (size_t)max_len);
    br_ssl_engine_recvapp_ack(&tls_sock->sc.eng, (size_t)max_len);
    return max_len;
}

void tls64_abort(TlsSocket *tls_sock) {
    if(!tls_sock) return;
    uintptr_t base=(uintptr_t)tls_sock;unsigned slot=(unsigned)((base-TLS64_BASE)/65536);
    memory_require(slot<8 && (tls_slots&(1u<<slot)),"TLS context owner");
    memset(tls_sock,0,sizeof(*tls_sock));
    for(size_t i=0;i<(sizeof(*tls_sock)+4095)/4096;i++) memory_require(vmm64_unmap(vmm64_kernel(),base+i*4096,1,0)==VM_OK,"TLS cleanup");
    tls_slots&=~(1u<<slot);
}

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
