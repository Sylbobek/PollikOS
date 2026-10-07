#include "network.h"
#include "user_abi.h"
#include "../../net/net_manager.h"
#include "../../net/dns.h"
#include "../../net/net_util.h"
#include "../../net/tls.h"

#define HTTP_HOST_MAX 254
#define HTTP_PATH_MAX 768
#define HTTP_HEADER_MAX 2048
#define HTTP_REQUEST_MAX 1400
#define HTTP_BODY_STASH 1024

typedef enum {
    HTTP_IDLE, HTTP_WAIT_LINK, HTTP_DNS, HTTP_CONNECT, HTTP_TLS, HTTP_SEND, HTTP_HEADERS,
    HTTP_BODY, HTTP_DONE, HTTP_ERROR
} HttpPhase;
typedef struct {
    HttpPhase phase;
    uint64_t owner;
    uint64_t handle;
    NetworkInterface *iface;
    TcpSocket *socket;
    char host[HTTP_HOST_MAX];
    char path[HTTP_PATH_MAX];
    u16 port;
    u8 ip[4];
    char request[HTTP_REQUEST_MAX];
    int request_len;
    int request_offset;
    int secure, tls_started;
    char headers[HTTP_HEADER_MAX];
    int header_len;
    u8 body_stash[HTTP_BODY_STASH];
    unsigned stash_at, stash_end;
    int chunked, chunk_state, chunk_line_len, chunk_crlf_at;
    char chunk_line[24];
    uint64_t chunk_remaining, body_remaining;
    int has_content_length;
} HttpRequest;

static int initialized;
static HttpRequest http;
static uint64_t next_http_handle = 1;
static void http_transport_close(void);

static int ascii_lower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
static int slice_equal(const char *a, unsigned n, const char *b) {
    unsigned i = 0;
    while (i < n && b[i] && ascii_lower((unsigned char)a[i]) == ascii_lower((unsigned char)b[i])) ++i;
    return i == n && b[i] == 0;
}
static int append_text(char *out, int cap, int *used, const char *text) {
    while (*text) {
        if (*used + 1 >= cap) return 0;
        out[(*used)++] = *text++;
    }
    out[*used] = 0;
    return 1;
}
static int append_port(char *out, int cap, int *used, u16 port) {
    char digits[5]; int n = 0;
    do { digits[n++] = (char)('0' + port % 10); port = (u16)(port / 10); } while (port && n < 5);
    while (n) {
        if (*used + 1 >= cap) return 0;
        out[(*used)++] = digits[--n];
    }
    out[*used] = 0;
    return 1;
}
static int http_parse_url(const char *url) {
    if (!url || !url[0] || strlen(url) < 7) return 0;
    static const char http_prefix[] = "http://";
    static const char https_prefix[] = "https://";
    unsigned prefix_len;
    if (slice_equal(url, 8, https_prefix)) {
        http.secure = 1;
        prefix_len = 8;
    } else if (slice_equal(url, 7, http_prefix)) {
        http.secure = 0;
        prefix_len = 7;
    } else return 0;
    const char *p = url + prefix_len;
    unsigned host_len = 0;
    while (p[host_len] && p[host_len] != '/' && p[host_len] != '?' && p[host_len] != '#' && p[host_len] != ':') {
        unsigned char c = (unsigned char)p[host_len];
        if (c <= 32 || c >= 127 || c == '@') return 0;
        ++host_len;
    }
    if (!host_len || host_len >= sizeof(http.host)) return 0;
    memcpy(http.host, p, host_len); http.host[host_len] = 0;
    p += host_len;
    http.port = http.secure ? 443 : 80;
    if (*p == ':') {
        ++p;
        if (*p < '0' || *p > '9') return 0;
        unsigned port = 0;
        while (*p >= '0' && *p <= '9') {
            port = port * 10u + (unsigned)(*p++ - '0');
            if (port > 65535) return 0;
        }
        if (!port) return 0;
        http.port = (u16)port;
    }
    if (*p == '#') p += strlen(p);
    unsigned path_len = 0;
    if (*p == '?') {
        http.path[path_len++] = '/';
    } else if (*p && *p != '/') return 0;
    while (*p && *p != '#') {
        unsigned char c = (unsigned char)*p++;
        if (c < 32 || c == 127 || c == ' ' || path_len + 1 >= sizeof(http.path)) return 0;
        http.path[path_len++] = (char)c;
    }
    if (!path_len) http.path[path_len++] = '/';
    http.path[path_len] = 0;
    return 1;
}
static void http_fail(void) {
    serial("HTTP64: request failed in phase ");
    char phase[12]; number(phase,(unsigned)http.phase); serial(phase); serial("\n");
    http_transport_close();
    http.phase = HTTP_ERROR;
}
static int http_build_request(void) {
    int used = 0;
    http.request[0] = 0;
    if (!append_text(http.request, sizeof(http.request), &used, "GET ") ||
        !append_text(http.request, sizeof(http.request), &used, http.path) ||
        !append_text(http.request, sizeof(http.request), &used, " HTTP/1.1\r\nHost: ") ||
        !append_text(http.request, sizeof(http.request), &used, http.host)) return 0;
    u16 default_port = http.secure ? 443 : 80;
    if (http.port != default_port && (!append_text(http.request, sizeof(http.request), &used, ":") ||
                            !append_port(http.request, sizeof(http.request), &used, http.port))) return 0;
    if (!append_text(http.request, sizeof(http.request), &used,
        "\r\nUser-Agent: PollikOS/1.0\r\nAccept: text/html, text/css, application/javascript, */*\r\nConnection: close\r\n\r\n"))
        return 0;
    http.request_len = used;
    return used <= 1460;
}
static int http_parse_headers(void) {
    const char *p = http.headers;
    const char *end = http.headers + http.header_len;
    while (p < end && *p != '\n') ++p;
    if (p == end) return 0;
    ++p;
    while (p < end) {
        const char *line = p;
        while (p < end && *p != '\r' && *p != '\n') ++p;
        unsigned length = (unsigned)(p-line);
        while (p < end && (*p == '\r' || *p == '\n')) ++p;
        if (!length) break;
        const char *colon = line;
        while (colon < line+length && *colon != ':') ++colon;
        if (colon == line+length) continue;
        unsigned key_len = (unsigned)(colon-line);
        const char *value = colon+1, *value_end = line+length;
        while (value < value_end && (*value == ' ' || *value == '\t')) ++value;
        while (value_end > value && (value_end[-1] == ' ' || value_end[-1] == '\t')) --value_end;
        if (slice_equal(line,key_len,"content-length")) {
            uint64_t n = 0; int valid = value < value_end;
            for (const char *q=value; q<value_end; ++q) {
                if (*q<'0' || *q>'9') { valid=0; break; }
                n=n*10u+(unsigned)(*q-'0');
                if (n > UINT32_MAX) { valid=0; break; }
            }
            if (!valid) return 0;
            http.body_remaining=n; http.has_content_length=1;
        } else if (slice_equal(line,key_len,"transfer-encoding")) {
            for (const char *q=value; q+7<=value_end; ++q)
                if (slice_equal(q,7,"chunked")) { http.chunked=1; break; }
        }
    }
    return 1;
}
static int http_header_byte(unsigned char c) {
    if (http.header_len >= (int)sizeof(http.headers)-1) return 0;
    http.headers[http.header_len++] = (char)c;
    http.headers[http.header_len] = 0;
    if (http.header_len >= 4 && http.headers[http.header_len-4]=='\r' &&
        http.headers[http.header_len-3]=='\n' && http.headers[http.header_len-2]=='\r' &&
        http.headers[http.header_len-1]=='\n') return 1;
    return -1;
}
static void http_transport_close(void) {
    if (http.tls_started) tls64_abort();
    http.tls_started = 0;
    if (http.socket) tcp_abort(http.socket);
    http.socket = 0;
}
static void http_poll(void) {
    if (http.phase == HTTP_WAIT_LINK) {
        if (!net_manager_is_connected()) return;
        http.iface = net_manager_get_active_iface();
        int result = dns_resolve_start(http.iface, http.host, http.ip);
        if (result < 0) { http_fail(); return; }
        http.phase = result ? HTTP_CONNECT : HTTP_DNS;
    }
    if (http.phase == HTTP_DNS) {
        int result = dns_resolve_poll(http.ip);
        if (result < 0) { http_fail(); return; }
        if (!result) return;
        http.phase = HTTP_CONNECT;
    }
    if (http.phase == HTTP_CONNECT) {
        http.socket = tcp_connect_start(http.iface, http.ip, http.port);
        if (!http.socket) { http_fail(); return; }
        http.phase = http.secure ? HTTP_TLS : HTTP_SEND;
        return;
    }
    if (http.phase == HTTP_TLS) {
        if (tcp_has_error(http.socket)) { http_fail(); return; }
        if (!tcp_is_connected(http.socket)) return;
        if (!http.tls_started) {
            if (!tls64_start(http.socket, http.host)) { http_fail(); return; }
            http.tls_started = 1;
            return;
        }
        if (tls64_failed()) { http_fail(); return; }
        if (!tls64_ready()) return;
        http.phase = HTTP_SEND;
    }
    if (http.phase == HTTP_SEND) {
        if (tcp_has_error(http.socket)) { http_fail(); return; }
        if (!tcp_is_connected(http.socket)) return;
        if (!http.request_len && !http_build_request()) { http_fail(); return; }
        int remaining = http.request_len - http.request_offset;
        if (remaining > 0) {
            int sent = http.secure
                ? tls64_write((const u8 *)http.request + http.request_offset, remaining)
                : tcp_send_nonblocking(http.socket, (const u8 *)http.request + http.request_offset,
                                       remaining > 1460 ? 1460 : remaining);
            if (sent <= 0) return;
            http.request_offset += sent;
        }
        if (http.request_offset == http.request_len) http.phase = HTTP_HEADERS;
        else return;
    }
    if (http.phase == HTTP_HEADERS) {
        u8 incoming[HTTP_BODY_STASH];
        int count = http.secure ? tls64_read(incoming,sizeof(incoming))
                                : tcp_read(http.socket,incoming,sizeof(incoming));
        int complete = 0, used = 0;
        while (used < count) {
            int state = http_header_byte(incoming[used++]);
            if (!state) { serial("HTTP64: response headers exceeded the buffer or were malformed\n"); http_fail(); return; }
            if (state > 0) { complete=1; break; }
        }
        if (complete) {
            if (!http_parse_headers()) { serial("HTTP64: invalid response header fields\n"); http_fail(); return; }
            unsigned remainder=(unsigned)(count-used);
            if (remainder) {
                memcpy(http.body_stash,incoming+used,remainder);
                http.stash_at=0; http.stash_end=remainder;
            }
            http.phase=HTTP_BODY;
            serial("HTTP64: response headers received\n");
        } else if (!count && (http.secure ? tls64_failed() || tls64_eof()
                                          : tcp_has_error(http.socket) || tcp_is_eof(http.socket))) {
            serial("HTTP64: transport closed before response headers completed\n");
            http_fail();
        }
    }
}
static int http_stream_read(u8 *out, int capacity) {
    return http.secure ? tls64_read(out, capacity) : tcp_read(http.socket, out, capacity);
}
static int http_transport_error(void) {
    return http.secure ? tls64_failed() : tcp_has_error(http.socket);
}
static int http_transport_eof(void) {
    return http.secure ? tls64_eof() : tcp_is_eof(http.socket);
}
static int http_raw_byte(u8 *out) {
    if (http.stash_at < http.stash_end) {
        *out=http.body_stash[http.stash_at++];
        if (http.stash_at==http.stash_end) http.stash_at=http.stash_end=0;
        return 1;
    }
    return http_stream_read(out,1);
}
static int http_chunk_read(u8 *out, int capacity) {
    int produced=0;
    while (produced<capacity && http.phase==HTTP_BODY) {
        if (http.chunk_state==3) { http.phase=HTTP_DONE; http_transport_close(); break; }
        if (http.chunk_state==0) {
            u8 c; int n=http_raw_byte(&c);
            if (n<=0) break;
            if (c=='\n') {
                unsigned line_length=(unsigned)http.chunk_line_len;
                if (line_length && http.chunk_line[line_length-1]=='\r') --line_length;
                unsigned digits=0;
                while (digits<line_length && http.chunk_line[digits]!=';') ++digits;
                if (!digits) { http_fail(); break; }
                uint64_t size=0;
                for (unsigned i=0;i<digits;++i) {
                    int d=ascii_lower((unsigned char)http.chunk_line[i]);
                    if (d>='0'&&d<='9') d-='0'; else if (d>='a'&&d<='f') d=d-'a'+10; else { http_fail(); break; }
                    if (size>(UINT32_MAX-(unsigned)d)/16u) { http_fail(); break; }
                    size=size*16u+(unsigned)d;
                }
                if (http.phase==HTTP_ERROR) break;
                http.chunk_line_len=0;
                if (!size) { http.chunk_state=3; http.chunk_line_len=0; }
                else { http.chunk_remaining=size; http.chunk_state=1; }
            } else {
                if (http.chunk_line_len+1 >= (int)sizeof(http.chunk_line)) { http_fail(); break; }
                http.chunk_line[http.chunk_line_len++]=(char)c;
            }
        } else if (http.chunk_state==1) {
            int want=capacity-produced;
            if ((uint64_t)want>http.chunk_remaining) want=(int)http.chunk_remaining;
            int n=0;
            if (http.stash_at<http.stash_end) {
                unsigned ready=http.stash_end-http.stash_at;
                if ((unsigned)want>ready) want=(int)ready;
                memcpy(out+produced,http.body_stash+http.stash_at,(unsigned)want);
                http.stash_at+=(unsigned)want; n=want;
                if (http.stash_at==http.stash_end) http.stash_at=http.stash_end=0;
            } else n=http_stream_read(out+produced,want);
            if (n<=0) break;
            produced+=n; http.chunk_remaining-=(unsigned)n;
            if (!http.chunk_remaining) { http.chunk_state=2; http.chunk_crlf_at=0; }
        } else if (http.chunk_state==2) {
            u8 c; int n=http_raw_byte(&c);
            if (n<=0) break;
            if ((http.chunk_crlf_at==0 && c!='\r') || (http.chunk_crlf_at==1 && c!='\n')) { http_fail(); break; }
            if (++http.chunk_crlf_at==2) http.chunk_state=0;
        } else { /* trailers: consume through the terminating empty line */
            u8 c; int n=http_raw_byte(&c);
            if (n<=0) break;
            if (c=='\n') {
                if (!http.chunk_line_len || (http.chunk_line_len==1 && http.chunk_line[0]=='\r')) {
                    http.chunk_state=3;
                }
                http.chunk_line_len=0;
            } else if (http.chunk_line_len < (int)sizeof(http.chunk_line)-1) {
                http.chunk_line[http.chunk_line_len++]=(char)c;
            } else { http_fail(); break; }
        }
    }
    return produced;
}

void network64_init(void) {
    if (initialized) return;
    initialized = 1;
    net_manager_init();
}
void network64_poll(void) {
    extern void audio64_poll(void);
    extern void audio64_stream_poll(void);
    if (initialized) {
        audio64_poll();
        audio64_stream_poll();
        net_manager_poll();
        if (http.tls_started) tls64_poll();
        http_poll();
    }
}
int network64_connected(void) { return initialized && net_manager_is_connected(); }
void network64_set_airplane(int enabled) {
    if (enabled && net_manager_enabled()) {
        http_transport_close();
        if (http.phase != HTTP_IDLE) http.phase=HTTP_ERROR;
    }
    net_manager_set_enabled(!enabled);
}

int64_t network64_http_open(uint64_t owner_pid, const char *url) {
    if (!initialized || !owner_pid || !url) return -(int64_t)USER_EINVAL;
    if (!net_manager_enabled()) return -(int64_t)USER_EIO;
    if (http.phase!=HTTP_IDLE && http.phase!=HTTP_DONE && http.phase!=HTTP_ERROR)
        return -(int64_t)USER_EAGAIN;
    http_transport_close();
    memset(&http,0,sizeof(http));
    if (!http_parse_url(url) || !http_build_request()) {
        memset(&http,0,sizeof(http));
        return -(int64_t)USER_EINVAL;
    }
    http.phase=HTTP_WAIT_LINK;
    http.owner=owner_pid;
    http.handle=next_http_handle++;
    if (!http.handle) http.handle=next_http_handle++;
    serial("HTTP64: request queued\n");
    return (int64_t)http.handle;
}

int64_t network64_http_read(uint64_t owner_pid, uint64_t handle, u8 *buffer, int capacity) {
    if (!buffer || capacity<=0 || capacity>4096 || http.owner!=owner_pid || http.handle!=handle)
        return -(int64_t)USER_EBADF;
    if (http.phase==HTTP_ERROR) return -(int64_t)USER_EIO;
    if (http.phase!=HTTP_BODY && http.phase!=HTTP_DONE) return -(int64_t)USER_EAGAIN;
    if (http.phase==HTTP_DONE) return 0;
    if (http.chunked) {
        int n=http_chunk_read(buffer,capacity);
        if (n) return n;
        if (http.phase==HTTP_ERROR) return -(int64_t)USER_EIO;
        if (http.phase==HTTP_DONE) return 0;
        if (http_transport_error() || http_transport_eof()) { http_fail(); return -(int64_t)USER_EIO; }
        return -(int64_t)USER_EAGAIN;
    }
    int n=0;
    if (http.stash_at<http.stash_end) {
        unsigned ready=http.stash_end-http.stash_at;
        unsigned want=(unsigned)capacity<ready?(unsigned)capacity:ready;
        memcpy(buffer,http.body_stash+http.stash_at,want);
        http.stash_at+=want; n=(int)want;
        if (http.stash_at==http.stash_end) http.stash_at=http.stash_end=0;
    } else if (http.socket) n=http_stream_read(buffer,capacity);
    if (n>0 && http.has_content_length) {
        if ((uint64_t)n>http.body_remaining) n=(int)http.body_remaining;
        http.body_remaining-=(unsigned)n;
        if (!http.body_remaining) { http.phase=HTTP_DONE; http_transport_close(); }
        return n;
    }
    if (n>0) return n;
    if (http.has_content_length && http.body_remaining==0) {
        http.phase=HTTP_DONE; http_transport_close(); return 0;
    }
    if (http.socket && http_transport_error()) { http_fail(); return -(int64_t)USER_EIO; }
    if (http.socket && http_transport_eof()) {
        if (http.has_content_length && http.body_remaining) { http_fail(); return -(int64_t)USER_EIO; }
        http.phase=HTTP_DONE; http_transport_close(); return 0;
    }
    return -(int64_t)USER_EAGAIN;
}

int64_t network64_http_close(uint64_t owner_pid, uint64_t handle) {
    if (http.owner!=owner_pid || http.handle!=handle) return -(int64_t)USER_EBADF;
    http_transport_close();
    memset(&http,0,sizeof(http));
    return 0;
}
void network64_http_owner_cleanup(uint64_t owner_pid) {
    if (http.owner==owner_pid && http.phase!=HTTP_IDLE) {
        http_transport_close();
        memset(&http,0,sizeof(http));
    }
}
