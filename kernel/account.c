#include "account.h"
#include "klog.h"
#include "vfs.h"
#include "pollikfs.h"
#include "include/bearssl/bearssl.h"
#include "../third_party/monocypher/monocypher.h"
#define ACCOUNT_PENDING "/etc/account.db.pending"
static uint8_t entropy_pool[32];
static uint64_t entropy_counter;
void account_wipe(void *memory,size_t length) { volatile uint8_t *p=memory; while(length--) *p++=0; }
int account_equal(const void *a,const void *b,size_t length) {
    const uint8_t *x=a,*y=b; uint8_t difference=0;
    for(size_t i=0;i<length;i++) difference|=x[i]^y[i];
    return !difference;
}
static size_t length(const char *p) { size_t n=0; while(n<64 && p[n]) ++n; return n; }
int account_valid_username(const char *name) {
    size_t n=0; while(n<32 && name[n]) ++n; if(n<2 || n>31) return 0;
    for(size_t i=0;i<n;i++) { char c=name[i]; if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-')) return 0; }
    return 1;
}
void account_entropy_event(uint64_t timing) {
    br_sha256_context ctx; uint64_t counter=++entropy_counter;
    br_sha256_init(&ctx); br_sha256_update(&ctx,entropy_pool,32);
    br_sha256_update(&ctx,&timing,sizeof(timing)); br_sha256_update(&ctx,&counter,sizeof(counter));
    br_sha256_out(&ctx,entropy_pool); account_wipe(&ctx,sizeof(ctx));
}
static void salt(uint8_t result[16]) {
    /* Hardware randomness is mixed with the timing pool when available.
     * On old CPUs/TCG the fallback provides per-creation salts; its entropy
     * depends on input timing and is not claimed to be hardware-grade. */
    for(unsigned i=0;i<8;i++) { uint32_t value; if(account_platform_random(&value)) account_entropy_event(value); }
    account_entropy_event(account_platform_time());
    memcpy(result,entropy_pool,16); account_entropy_event(account_platform_time());
}
static int kdf(const AccountRecord *a,const char *password,uint8_t out[32]) {
    size_t n=length(password); if(n>63) return 0;
    if(a->version==1) {
        br_sha256_context ctx;
        br_sha256_init(&ctx); br_sha256_update(&ctx,a->salt,16); br_sha256_update(&ctx,password,n); br_sha256_out(&ctx,out);
        for(uint32_t i=1;i<a->rounds;i++) { br_sha256_init(&ctx); br_sha256_update(&ctx,out,32); br_sha256_update(&ctx,a->salt,16); br_sha256_update(&ctx,password,n); br_sha256_out(&ctx,out); }
        account_wipe(&ctx,sizeof(ctx)); return 1;
    }
    size_t bytes=(size_t)a->memory_kib*1024;
    void *work=account_work_alloc(bytes); if(!work) { KLOG_ERROR(KLOG_CAT_BOOT,"Account Argon2 workspace allocation failed");return 0; }
    crypto_argon2_config config={CRYPTO_ARGON2_ID,a->memory_kib,a->rounds,1};
    crypto_argon2_inputs inputs={(const uint8_t *)password,a->salt,(uint32_t)n,16};
    crypto_argon2(out,32,work,config,inputs,crypto_argon2_no_extras);
    account_work_free(work,bytes); return 1;
}
static int valid(const AccountRecord *a) {
    if(a->magic!=ACCOUNT_MAGIC || a->username[31] || !account_valid_username(a->username)) return 0;
    if(a->version==1) return a->rounds>=1024 && a->rounds<=1000000;
    return a->version==2 && a->algorithm==CRYPTO_ARGON2_ID && a->memory_kib==ACCOUNT_MEMORY_KIB &&
           a->rounds==ACCOUNT_PASSES && a->lanes==1;
}
int account_load(AccountRecord *a) {
    vfs_stat_t st; account_wipe(a,sizeof(*a));
    if(vfs_stat(ACCOUNT_PATH,&st)<0) {
        if(pollikfs_error()!=VFS_NOT_FOUND) return -1;
        /* Deleting a configured account must not silently reset the machine. */
        if(vfs_stat(INSTALL_MARKER,&st)==0) return -1;
        return pollikfs_error()==VFS_NOT_FOUND ? 0 : -1;
    }
    if(st.type!=VFS_FILE || st.size!=sizeof(*a)) return -1;
    int fd=vfs_open(ACCOUNT_PATH,O_RDONLY); if(fd<0) return -1;
    int got=vfs_read(fd,a,sizeof(*a)), closed=vfs_close(fd);
    if(got!=(int)sizeof(*a) || closed<0 || !valid(a)) { account_wipe(a,sizeof(*a)); return -1; }
    return 1;
}
static int write_record(const AccountRecord *a) {
    int fd=vfs_open(ACCOUNT_PENDING,O_WRONLY|O_CREAT|O_TRUNC); if(fd<0) { KLOG_ERROR(KLOG_CAT_BOOT,"Account temporary record open failed");return 0; }
    int written=vfs_write(fd,a,sizeof(*a)), closed=vfs_close(fd);
    if(written!=(int)sizeof(*a) || closed<0) { KLOG_ERROR(KLOG_CAT_BOOT,"Account temporary record write/close failed");(void)vfs_unlink(ACCOUNT_PENDING); return 0; }
    /* Verify bytes before replacing the existing credential record. */
    AccountRecord check; fd=vfs_open(ACCOUNT_PENDING,O_RDONLY); if(fd<0) return 0;
    int got=vfs_read(fd,&check,sizeof(check)); closed=vfs_close(fd);
    int ok=got==(int)sizeof(check) && !closed && account_equal(a,&check,sizeof(check));
    account_wipe(&check,sizeof(check));
    if(!ok || vfs_rename_replace(ACCOUNT_PENDING,ACCOUNT_PATH)<0) { KLOG_ERROR(KLOG_CAT_BOOT,ok?"Account record rename failed":"Account record verification failed");(void)vfs_unlink(ACCOUNT_PENDING); return 0; }
    return 1;
}
static int upgrade(AccountRecord *a,const char *password) {
    AccountRecord next=*a;
    next.version=2; next.rounds=ACCOUNT_PASSES; next.algorithm=CRYPTO_ARGON2_ID;
    next.memory_kib=ACCOUNT_MEMORY_KIB; next.lanes=1; memset(next.reserved,0,sizeof(next.reserved));
    salt(next.salt);
    int ok=kdf(&next,password,next.password_hash) && write_record(&next);
    if(ok) *a=next; account_wipe(&next,sizeof(next)); return ok;
}
int account_create(AccountRecord *a,const char *name,const char *password) {
    if(!account_valid_username(name) || length(password)<6 || length(password)>63) return 0;
    AccountRecord existing; int loaded=account_load(&existing); account_wipe(&existing,sizeof(existing));
    if(loaded!=0) return 0;
    AccountRecord next; memset(&next,0,sizeof(next)); next.magic=ACCOUNT_MAGIC;
    for(size_t i=0;name[i];i++) next.username[i]=name[i];
    (void)vfs_mkdir("/etc"); (void)vfs_mkdir("/home");
    char home[38]="/home/"; size_t at=6;
    for(size_t i=0;name[i];i++) home[at++]=name[i]; home[at]=0;
    vfs_stat_t st;
    if(vfs_mkdir(home)<0 && (vfs_stat(home,&st)<0 || st.type!=VFS_DIR)) {
        KLOG_ERROR(KLOG_CAT_BOOT,"Account home directory creation failed");return 0;
    }
    int ok=upgrade(&next,password);
    if(ok) {
        *a=next;
        static const char marker[]="PollikOS installation complete\n";
        int fd=vfs_open(INSTALL_MARKER,O_WRONLY|O_CREAT|O_TRUNC);
        if(fd>=0) { (void)vfs_write(fd,marker,sizeof(marker)-1); (void)vfs_close(fd); }
    }
    account_wipe(&next,sizeof(next)); return ok;
}
int account_verify(AccountRecord *a,const char *password) {
    uint8_t candidate[32]; int ok=valid(a) && kdf(a,password,candidate) && account_equal(candidate,a->password_hash,32);
    account_wipe(candidate,sizeof(candidate));
    /* A verified legacy credential remains valid when its storage cannot be
     * updated. Keep the old record intact and retry migration on a later login. */
    if(ok && a->version==1 && (pollikfs_readonly() || !upgrade(a,password)))
        KLOG_WARN(KLOG_CAT_BOOT,"Account verified; legacy hash migration deferred (storage unavailable)");
    return ok;
}
int account_change(AccountRecord *a,const char *old_password,const char *new_password) {
    if(length(new_password)<6 || length(new_password)>63 || !account_verify(a,old_password)) return 0;
    return upgrade(a,new_password);
}
