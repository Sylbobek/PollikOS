#include "network.h"
#include "scheduler.h"
#include "../../net/net_manager.h"
#include "../../net/net_util.h"
#include "../../net/dns.h"
#include "../../net/tls.h"
enum { STREAM_FREE,STREAM_LINK,STREAM_DNS,STREAM_CONNECT,STREAM_TLS,STREAM_READY,STREAM_ERROR };
typedef struct {
    uint64_t owner,handle,deadline;
    unsigned phase,secure;
    char host[254];u16 port;u8 ip[4];
    TcpSocket *tcp;TlsSocket *tls;
} Stream64;
static Stream64 streams[8];
static Stream64 *dns_owner;
static uint64_t next_handle=1;
static int initialized;
static void release(Stream64 *s) {
    if(dns_owner==s) dns_owner=0;
    if(s->tls) tls64_abort(s->tls);
    if(s->tcp) tcp_abort(s->tcp);
    *s=(Stream64){0};
}
static void fail(Stream64 *s) {
    uint64_t owner=s->owner,h=s->handle;release(s);
    s->owner=owner;s->handle=h;s->phase=STREAM_ERROR;
}
static void poll_stream(Stream64 *s) {
    if(s->phase==STREAM_FREE || s->phase==STREAM_ERROR) return;
    if(scheduler64_ticks()>s->deadline) { fail(s);return; }
    if(s->tcp && tcp_has_error(s->tcp)) { fail(s);return; }
    NetworkInterface *iface=net_manager_get_active_iface();
    if(s->phase==STREAM_LINK) {
        if(!iface || !network64_connected() || dns_owner) return;
        int result=dns_resolve_start(iface,s->host,s->ip);
        if(result<0) { fail(s);return; }
        s->phase=result?STREAM_CONNECT:STREAM_DNS;if(!result) dns_owner=s;
    }
    if(s->phase==STREAM_DNS) {
        if(dns_owner!=s) return;
        int result=dns_resolve_poll(s->ip);if(!result) return;
        dns_owner=0;if(result<0) { fail(s);return; }s->phase=STREAM_CONNECT;
    }
    if(s->phase==STREAM_CONNECT) {
        if(!s->tcp) s->tcp=tcp_connect_start(iface,s->ip,s->port);
        if(!s->tcp || tcp_has_error(s->tcp)) { fail(s);return; }
        if(!tcp_is_connected(s->tcp)) return;
        if(!s->secure) s->phase=STREAM_READY;
        else { s->tls=tls64_start(s->tcp,s->host);if(!s->tls) { fail(s);return; }s->phase=STREAM_TLS; }
    }
    if(s->tls) {
        tls64_poll(s->tls);
        if(tls64_failed(s->tls)) fail(s);
        else if(tls64_ready(s->tls)) s->phase=STREAM_READY;
    }
}
void network64_init(void) { if(!initialized) { initialized=1;net_manager_init(); } }
int network64_connected(void) { return initialized && net_manager_is_connected(); }
void network64_poll(void) {
    extern void audio64_poll(void);extern void audio64_stream_poll(void);
    if(!initialized) return;
    audio64_poll();audio64_stream_poll();net_manager_poll();
    for(unsigned i=0;i<8;i++) poll_stream(&streams[i]);
}
void network64_set_airplane(int enabled) {
    if(enabled) for(unsigned i=0;i<8;i++) if(streams[i].phase) fail(&streams[i]);
    net_manager_set_enabled(!enabled);
}
void network64_owner_cleanup(uint64_t pid) {
    for(unsigned i=0;i<8;i++) if(streams[i].owner==pid) release(&streams[i]);
}
static Stream64 *find(uint64_t pid,uint64_t handle) {
    for(unsigned i=0;i<8;i++) if(streams[i].phase && streams[i].owner==pid && streams[i].handle==handle) return &streams[i];
    return 0;
}
int64_t network64_dispatch(Process64 *p,UserFrame *f) {
    if(!security_has(&p->credentials,CAP_NETWORK)) return -USER_EPERM;
    if(f->rax==USER_STREAM_OPEN) {
        if(!initialized || !net_manager_enabled()) return -USER_EIO;
        if(!f->rsi || f->rsi>65535 || f->rdx>1) return -USER_EINVAL;
        char host[254];
        UserCopyResult copied=copy_string_from_user64(&p->space,host,f->rdi,sizeof(host));
        if(copied!=USER_COPY_OK) return copied==USER_COPY_TOO_LONG?-USER_ENAMETOOLONG:-USER_EFAULT;
        if(!host[0]) return -USER_EINVAL;
        u8 literal[4];
        if(f->rdx && net_parse_ip(host,literal)) return -USER_ENOTSUP;
        for(unsigned i=0;host[i];i++) if((u8)host[i]<=32 || (u8)host[i]>=127 || host[i]=='/' || host[i]=='\\' || host[i]==':' || host[i]=='@') return -USER_EINVAL;
        Stream64 *s=0;for(unsigned i=0;i<8;i++) if(!streams[i].phase) { s=&streams[i];break; }
        if(!s) return -USER_EMFILE;
        *s=(Stream64){.owner=p->pid,.handle=next_handle++,.deadline=scheduler64_ticks()+3000,.phase=STREAM_LINK,.secure=(unsigned)f->rdx,.port=(u16)f->rsi};
        if(!s->handle) s->handle=next_handle++;
        memcpy(s->host,host,sizeof(host));return (int64_t)s->handle;
    }
    Stream64 *s=find(p->pid,f->rdi);if(!s) return -USER_EBADF;
    if(f->rax==USER_STREAM_CLOSE) { release(s);return 0; }
    int eof=s->tls?tls64_eof(s->tls):(s->tcp && tcp_is_eof(s->tcp));
    if(f->rax==USER_STREAM_STATUS) return (s->phase==STREAM_READY?1:0)|(eof?2:0)|(s->phase==STREAM_ERROR?4:0);
    if(s->phase==STREAM_ERROR) return -USER_EIO;
    if(s->phase!=STREAM_READY) return -USER_EAGAIN;
    if(!f->rdx || f->rdx>4096) return -USER_E2BIG;
    u8 buffer[4096];int n;
    if(f->rax==USER_STREAM_WRITE) {
        if(copy_from_user64(&p->space,buffer,f->rsi,f->rdx)!=USER_COPY_OK) return -USER_EFAULT;
        n=s->tls?tls64_write(s->tls,buffer,(int)f->rdx):tcp_send_nonblocking(s->tcp,buffer,(int)f->rdx);
    } else {
        if(user_range_check(&p->space,f->rsi,f->rdx,1)!=USER_COPY_OK) return -USER_EFAULT;
        n=s->tls?tls64_read(s->tls,buffer,(int)f->rdx):tcp_read(s->tcp,buffer,(int)f->rdx);
        if(n>0 && copy_to_user64(&p->space,f->rsi,buffer,(size_t)n)!=USER_COPY_OK) return -USER_EFAULT;
    }
    if(n>0) { s->deadline=scheduler64_ticks()+3000;return n; }
    return eof && f->rax==USER_STREAM_READ?0:-USER_EAGAIN;
}
