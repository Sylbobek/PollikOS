/* Native URL-resolution regression: compiles the real kernel/net/http.c and
 * exercises http_resolve_url shared by links, CSS, scripts, images, forms and
 * redirects. Network transport is stubbed; this is NOT a transport test. */
#include "../kernel/net/http.h"
#include "../kernel/net/net_manager.h"
#include "../kernel/net/tcp.h"
#include "../kernel/net/tls.h"
#include "../kernel/net/net_util.h"
extern int printf(const char *, ...);
extern void *malloc(__SIZE_TYPE__);
extern void free(void *);
extern void exit(int);
volatile u32 ticks;
static int failures;
static int str_same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (int)(u8)*a - (int)(u8)*b; }
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)
#define EQ(base, ref, want) do { char out[512]; int ok = http_resolve_url((base), (ref), out, sizeof(out)); \
    if (!ok || str_same(out, (want)) != 0) { printf("FAIL line %d: %s + %s -> %s (want %s)\n", __LINE__, (base), (ref), ok?out:"<fail>", (want)); ++failures; } } while (0)
void *kmalloc(u32 n) { return malloc(n); }
void kfree(void *p) { free(p); }
void *krealloc(void *p, u32 n) { (void)n; return p; }
void serial(const char *s) { (void)s; }
void number(char *s, u32 n) { int k=0; char b[12]; do {b[k++]='0'+n%10;n/=10;}while(n);int i=0;while(k)s[i++]=b[--k];s[i]=0; }
void net_manager_poll(void) {}
int net_manager_resolve_host(const char *h, u8 *ip) { (void)h;(void)ip;return 0; }
TcpSocket *net_manager_connect_tcp(const u8 *ip, u16 port) { (void)ip;(void)port;return 0; }
void tcp_close(TcpSocket *s) { (void)s; }
void tls_close(TlsSocket *s) { (void)s; }
TlsSocket *tls_connect(TcpSocket *s, const char *h, int t) { (void)s;(void)h;(void)t;return 0; }
int tls_send(TlsSocket *s, const u8 *d, int n) { (void)s;(void)d;(void)n;return 0; }
int tcp_send(TcpSocket *s, const u8 *d, int n) { (void)s;(void)d;(void)n;return 0; }
int tls_read(TlsSocket *s, u8 *b, int n) { (void)s;(void)b;(void)n;return 0; }
int tcp_read(TcpSocket *s, u8 *b, int n) { (void)s;(void)b;(void)n;return 0; }
int tls_is_eof(TlsSocket *s) { (void)s;return 1; }
int tcp_is_eof(TcpSocket *s) { (void)s;return 1; }
void net_service_wait(void) {}

int main(void) {
    /* Absolute references replace the base. */
    EQ("https://a.example/dir/page", "https://b.example/x", "https://b.example/x");
    EQ("https://a.example/dir/page", "http://b.example/x?q=1#f", "http://b.example/x?q=1#f");
    /* Root-relative, relative, ./, ../ against a directory base. */
    EQ("https://a.example/dir/page", "/abc", "https://a.example/abc");
    EQ("https://a.example/dir/page", "abc", "https://a.example/dir/abc");
    EQ("https://a.example/dir/page", "./abc", "https://a.example/dir/abc");
    EQ("https://a.example/dir/sub/page", "../abc", "https://a.example/dir/abc");
    EQ("https://a.example/dir/sub/page", "../../abc", "https://a.example/abc");
    EQ("https://a.example/dir/sub/page", "../../../abc", "https://a.example/abc");
    EQ("https://a.example/a/b/c/d", "e", "https://a.example/a/b/c/e");
    /* Protocol-relative keeps the scheme. */
    EQ("https://a.example/dir/page", "//b.example/p", "https://b.example/p");
    /* Query-only and fragment-only keep the path. */
    EQ("https://a.example/dir/page?x=1", "?y=2", "https://a.example/dir/page?y=2");
    EQ("https://a.example/dir/page?x=1", "#frag", "https://a.example/dir/page?x=1#frag");
    /* Base with no trailing slash: last segment is a file. */
    EQ("https://a.example/dir/page.html", "other.html", "https://a.example/dir/other.html");
    EQ("https://a.example/", "x", "https://a.example/x");
    EQ("https://a.example", "x", "https://a.example/x");
    /* Ports and hosts are preserved. */
    EQ("http://a.example:8080/dir/p", "q", "http://a.example:8080/dir/q");
    /* A path with dot segments in the reference is normalised. */
    EQ("https://a.example/dir/p", "a/../b/./c", "https://a.example/dir/b/c");
    /* Empty reference resolves to the base itself. */
    EQ("https://a.example/dir/p?q=1", "", "https://a.example/dir/p?q=1");
    /* Capacity rejection must not overflow. */
    { char small[8]; CHECK(http_resolve_url("https://a.example/x", "https://b.example/very/long/path", small, sizeof(small)) == 0); }
    if (failures) { printf("FAILED: %d URL cases\n", failures); return 1; }
    printf("PASS: shared URL resolver (absolute/relative/root/dot/query/fragment/protocol-relative/port)\n");
    return 0;
}
