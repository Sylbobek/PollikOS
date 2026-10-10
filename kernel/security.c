#include "security.h"
static struct { uint64_t id, next; unsigned state; char username[32]; } session;
static int prefix(const char *path, const char *root) {
    unsigned i=0; while (root[i] && root[i]==path[i]) ++i;
    return !root[i] && (!path[i] || path[i]=='/');
}
/* The policy sees canonical components even when the caller uses ../ or //.
 * PollikFS has no links, mounts or hard-link syscall at this checkpoint. */
static int canonical(const char *path, char out[128]) {
    if (!path || path[0]!='/') return 0;
    unsigned used=1, at=1; out[0]='/';
    while (path[at]) {
        while (path[at]=='/') ++at;
        unsigned start=at; while(path[at] && path[at]!='/') ++at;
        unsigned length=at-start;
        if (!length || (length==1 && path[start]=='.')) continue;
        if (length==2 && path[start]=='.' && path[start+1]=='.') {
            while(used>1 && out[used-1]!='/') --used;
            if(used>1) --used;
        } else {
            if (used>1) { if(used+1>=128) return 0; out[used++]='/'; }
            if(length>=128-used) return 0;
            for(unsigned i=0;i<length;i++) out[used++]=path[start+i];
        }
    }
    out[used]=0; return 1;
}
int security_credentials_live(const Credentials *c) {
    return c && (!c->uid || (c->uid==1 &&
        ((!c->session && !session.next && session.state==SESSION_NONE) ||
         (c->session && c->session==session.id && session.state!=SESSION_NONE && session.state!=SESSION_LOGOUT))));
}
int security_credentials_runnable(const Credentials *c) {
    return c && (!c->uid || (security_credentials_live(c) &&
        (session.state==SESSION_ACTIVE || (!c->session && !session.next))));
}
Credentials security_user_credentials(void) { return (Credentials){1,1,session.id,CAP_USER_DEFAULT}; }
int security_has(const Credentials *c,unsigned rights) {
    return c && (!c->uid || (security_credentials_runnable(c) && (c->capabilities&rights)==rights));
}
unsigned security_path_user_access(const char *path) {
    char clean[128];
    if(!canonical(path,clean)) return 0;
    /* Public configuration stays readable. Credential records and all their
     * pending/backup suffixes are private to the account service. */
    const char *secret="/etc/account.db";
    unsigned i=0; while(secret[i] && secret[i]==clean[i]) ++i;
    if(!secret[i]) return 0;
    if(prefix(clean,"/home") || prefix(clean,"/tmp")) return ACCESS_READ|ACCESS_WRITE;
    /* Existing native TinyCC rebuilds its developer-owned sysroot here. */
    return ACCESS_READ | ((prefix(clean,"/usr/src") || prefix(clean,"/usr/lib")) ? ACCESS_WRITE : 0);
}
int security_path_allowed(const Credentials *c,const char *path,unsigned access) {
    char clean[128];
    if(!c || !canonical(path,clean)) return 0;
    unsigned rights=(access&ACCESS_READ?CAP_FILE_READ:0)|(access&ACCESS_WRITE?CAP_FILE_WRITE:0);
    return security_has(c,rights) && (!c->uid || (c->capabilities&CAP_ADMIN) || (security_path_user_access(clean)&access)==access);
}
uint64_t security_session_id(void) { return session.id; }
int security_session_state(void) { return (int)session.state; }
const char *security_username(void) { return session.username; }
void security_session_begin(const char *username) {
    session.id=++session.next; if(!session.id) session.id=++session.next;
    for(unsigned i=0;i<32;i++) session.username[i]=0;
    for(unsigned i=0;username && username[i] && i<31;i++) session.username[i]=username[i];
    session.state=SESSION_ACTIVE;
}
int security_session_request(const Credentials *c,unsigned state) {
    if(!c || !c->session || !security_has(c,CAP_SESSION) || c->session!=session.id ||
       session.state!=SESSION_ACTIVE ||
       (state!=SESSION_LOCKED && state!=SESSION_LOGOUT && state!=SESSION_PASSWORD && state!=SESSION_ELEVATE && state!=SESSION_FACTORY_RESET)) return 0;
    if(state==SESSION_FACTORY_RESET && !security_has(c,CAP_ADMIN|CAP_FILE_WRITE))return 0;
    session.state=state; return 1;
}
void security_session_resume(void) {
    if(session.state==SESSION_LOCKED || session.state==SESSION_PASSWORD || session.state==SESSION_ELEVATE) session.state=SESSION_ACTIVE;
}
void security_session_end(void) { session.id=0; session.state=SESSION_NONE; }
