#include <stdint.h>
#include <string.h>
#include <pollikos/net.h>
#include <pollikos/time.h>
#include <pollikos/process.h>
#define HTTP_HOST_MAX 254
#define HTTP_PATH_MAX 2048
#define HTTP_HEADER_MAX 16384
#define HTTP_REQUEST_MAX 3072
#define HTTP_BODY_STASH 1024

typedef enum {
    HTTP_IDLE, HTTP_WAIT_LINK, HTTP_DNS, HTTP_CONNECT, HTTP_TLS, HTTP_SEND, HTTP_HEADERS,
    HTTP_BODY, HTTP_DONE, HTTP_ERROR
} HttpPhase;
typedef struct {
    HttpPhase phase;
    uint64_t owner;
    uint64_t handle;
    
    long socket;
    uint64_t deadline;
    char host[HTTP_HOST_MAX];
    char path[HTTP_PATH_MAX];
    uint16_t port;
    uint8_t ip[4];
    char request[HTTP_REQUEST_MAX];
    int request_len;
    int request_offset;
    int secure, tls_started;
    char headers[HTTP_HEADER_MAX];
    int header_len;
    uint8_t body_stash[HTTP_BODY_STASH];
    unsigned stash_at, stash_end;
    int chunked, chunk_state, chunk_line_len, chunk_crlf_at;
    char chunk_line[24];
    uint64_t chunk_remaining, body_remaining;
    int has_content_length;
    int status,redirects,headers_ready;
    char url[3072],location[2048];
} HttpRequest;

static HttpRequest requests[4];
static HttpRequest *active;
#define http (*active)
static uint64_t next_http_handle=1;
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
static int append_port(char *out, int cap, int *used, uint16_t port) {
    char digits[5]; int n = 0;
    do { digits[n++] = (char)('0' + port % 10); port = (uint16_t)(port / 10); } while (port && n < 5);
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
        http.port = (uint16_t)port;
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
    uint16_t default_port = http.secure ? 443 : 80;
    if (http.port != default_port && (!append_text(http.request, sizeof(http.request), &used, ":") ||
                            !append_port(http.request, sizeof(http.request), &used, http.port))) return 0;
    if (!append_text(http.request, sizeof(http.request), &used,
        "\r\nUser-Agent: PollikOS-Web/0.2\r\nAccept: text/html, text/css, application/javascript, */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n"))
        return 0;
    http.request_len = used;
    return used <= 1460;
}
static int http_parse_headers(void) {
    if(strncmp(http.headers,"HTTP/1.",7))return 0;
    const char *code=strchr(http.headers,' ');
    if(!code || code[1]<'1'||code[1]>'5'||code[2]<'0'||code[2]>'9'||code[3]<'0'||code[3]>'9')return 0;
    http.status=(code[1]-'0')*100+(code[2]-'0')*10+code[3]-'0';
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
        } else if(slice_equal(line,key_len,"location")) {
            size_t n=(size_t)(value_end-value);if(n>=sizeof(http.location))return 0;
            memcpy(http.location,value,n);http.location[n]=0;
        } else if(slice_equal(line,key_len,"content-encoding") && !slice_equal(value,(unsigned)(value_end-value),"identity")) {
            return 0; /* never pass compressed bytes to HTML/JS as if decoded */
        }
    }
    return 1;
}
static int http_redirect(void) {
    if(http.status!=301 && http.status!=302 && http.status!=303 && http.status!=307 && http.status!=308)return 0;
    if(!http.location[0] || http.redirects>=8)return -1;
    char destination[sizeof(http.url)];
    if(!pollikos_url_resolve(http.url,http.location,destination,sizeof(destination)))return -1;
    uint64_t handle=http.handle;int redirects=http.redirects+1;
    http_transport_close();memset(active,0,sizeof(*active));http.handle=handle;http.redirects=redirects;
    strcpy(http.url,destination);
    if(!http_parse_url(destination) || !http_build_request())return -1;
    long stream=__pollikos_syscall3(USER_STREAM_OPEN,(uint64_t)(uintptr_t)http.host,http.port,http.secure);
    if(stream<0)return -1;
    http.socket=stream;http.phase=HTTP_SEND;http.deadline=(uint64_t)pollikos_monotonic_ms()+30000;return 1;
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
    if(http.socket>0) (void)__pollikos_syscall1(USER_STREAM_CLOSE,(uint64_t)http.socket);
    http.socket=0;
}
static int http_stream_read(uint8_t *out,int capacity) {
    long n=__pollikos_syscall3(USER_STREAM_READ,(uint64_t)http.socket,(uint64_t)(uintptr_t)out,(unsigned)capacity);
    return n==-USER_EAGAIN?0:(int)n;
}
static int http_transport_error(void) {
    long s=__pollikos_syscall1(USER_STREAM_STATUS,(uint64_t)http.socket);
    return s<0 || (s&4);
}
static int http_transport_eof(void) { return __pollikos_syscall1(USER_STREAM_STATUS,(uint64_t)http.socket)&2; }
static void http_poll(void) {
    if(http.phase==HTTP_ERROR || http.phase==HTTP_IDLE || http.phase==HTTP_DONE) return;
    if((uint64_t)pollikos_monotonic_ms()>http.deadline) { http_fail();return; }
    if(http_transport_error()) { http_fail();return; }
    if(http.phase==HTTP_SEND) {
        if(!(__pollikos_syscall1(USER_STREAM_STATUS,(uint64_t)http.socket)&1)) return;
        int remaining=http.request_len-http.request_offset;
        long sent=__pollikos_syscall3(USER_STREAM_WRITE,(uint64_t)http.socket,
            (uint64_t)(uintptr_t)(http.request+http.request_offset),(unsigned)remaining);
        if(sent==-USER_EAGAIN) return;
        if(sent<0) { http_fail();return; }
        http.request_offset+=(int)sent;
        if(http.request_offset<http.request_len) return;
        http.phase=HTTP_HEADERS;
    }
    if(http.phase==HTTP_HEADERS) {
        uint8_t incoming[HTTP_BODY_STASH];int count=http_stream_read(incoming,sizeof(incoming));
        if(count<0) { http_fail();return; }
        int used=0,complete=0;
        while(used<count) {
            int state=http_header_byte(incoming[used++]);
            if(!state) { http_fail();return; }
            if(state>0) { complete=1;break; }
        }
        if(complete) {
            if(!http_parse_headers()) { http_fail();return; }
            unsigned remaining=(unsigned)(count-used);
            if(remaining) { memcpy(http.body_stash,incoming+used,remaining);http.stash_at=0;http.stash_end=remaining; }
            int redirect=http_redirect();if(redirect<0){http_fail();return;}if(redirect)return;
            http.headers_ready=1;http.phase=HTTP_BODY;
        } else if(!count && http_transport_eof()) http_fail();
    }
}
static int http_raw_byte(uint8_t *out) {
    if (http.stash_at < http.stash_end) {
        *out=http.body_stash[http.stash_at++];
        if (http.stash_at==http.stash_end) http.stash_at=http.stash_end=0;
        return 1;
    }
    return http_stream_read(out,1);
}
static int http_chunk_read(uint8_t *out, int capacity) {
    int produced=0;
    while (produced<capacity && http.phase==HTTP_BODY) {
        if (http.chunk_state==3) { http.phase=HTTP_DONE; http_transport_close(); break; }
        if (http.chunk_state==0) {
            uint8_t c; int n=http_raw_byte(&c);
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
            uint8_t c; int n=http_raw_byte(&c);
            if (n<=0) break;
            if ((http.chunk_crlf_at==0 && c!='\r') || (http.chunk_crlf_at==1 && c!='\n')) { http_fail(); break; }
            if (++http.chunk_crlf_at==2) http.chunk_state=0;
        } else { /* trailers: consume through the terminating empty line */
            uint8_t c; int n=http_raw_byte(&c);
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

long pollikos_http_open(const char *url) {
    if(!url) return -USER_EINVAL;
    active=0;for(unsigned i=0;i<4;i++) if(requests[i].phase==HTTP_IDLE) { active=&requests[i];break; }
    if(!active) return -USER_EMFILE;
    memset(active,0,sizeof(*active));
    if(strlen(url)>=sizeof(http.url))return -USER_ENAMETOOLONG;
    strcpy(http.url,url);
    if(!http_parse_url(url) || !http_build_request()) return -USER_EINVAL;
    long stream=__pollikos_syscall3(USER_STREAM_OPEN,(uint64_t)(uintptr_t)http.host,http.port,http.secure);
    if(stream<0) { memset(active,0,sizeof(*active));return stream; }
    http.socket=stream;http.phase=HTTP_SEND;http.handle=next_http_handle++;
    http.deadline=(uint64_t)pollikos_monotonic_ms()+30000;
    return (long)http.handle;
}
static int select_request(long handle) {
    for(unsigned i=0;i<4;i++) if(requests[i].phase!=HTTP_IDLE && requests[i].handle==(uint64_t)handle) { active=&requests[i];return 1; }
    return 0;
}
long pollikos_http_status(long handle) {if(!select_request(handle))return -USER_EBADF;http_poll();if(http.phase==HTTP_ERROR)return -USER_EIO;return http.headers_ready?http.status:-USER_EAGAIN;}
long pollikos_http_url(long handle,char *buffer,unsigned long capacity) {
    if(!select_request(handle) || !buffer)return -USER_EBADF;
    size_t n=strlen(http.url);if(n>=capacity)return -USER_ENAMETOOLONG;memcpy(buffer,http.url,n+1);return (long)n;
}
long pollikos_http_header(long handle,const char *name,char *buffer,unsigned long capacity) {
    if(!select_request(handle) || !name || !buffer || !capacity)return -USER_EBADF;
    if(!http.headers_ready)return -USER_EAGAIN;
    const char *p=http.headers,*end=p+http.header_len;
    while(p<end && *p!='\n')++p;if(p<end)++p;
    while(p<end){const char *line=p;while(p<end && *p!='\r' && *p!='\n')++p;const char *last=p;
        while(p<end && (*p=='\r'||*p=='\n'))++p;const char *colon=line;while(colon<last && *colon!=':')++colon;
        if(colon<last && slice_equal(line,(unsigned)(colon-line),name)){
            const char *value=colon+1;while(value<last && (*value==' '||*value=='\t'))++value;
            size_t n=(size_t)(last-value);if(n>=capacity)return -USER_ENAMETOOLONG;memcpy(buffer,value,n);buffer[n]=0;return (long)n;}}
    buffer[0]=0;return 0;
}
long pollikos_http_read(long handle,void *destination,unsigned long requested) {
    int capacity=(int)requested; uint8_t *buffer=destination;
    if(requested>4096) return -USER_E2BIG;
    if(!buffer || !capacity || capacity>4096 || !select_request(handle)) return -USER_EBADF;
    http_poll();
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

long pollikos_http_close(long handle) {
    if (!select_request(handle)) return -(int64_t)USER_EBADF;
    http_transport_close();
    memset(&http,0,sizeof(http));
    return 0;
}
