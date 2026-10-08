/* Actual shared VFS/PollikFS/account code over a disposable disk image.
 * Native test adapters supply sectors, caller identity and KDF workspace. */
#define POLLIK_X64 1
#define SELFTEST 1
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../kernel/security.c"
#include "../kernel/pollikfs.c"
#include "../kernel/vfs.c"
#include "../kernel/account.c"
static Credentials caller;
static vfs_file_t *fds[VFS_MAX_FDS];
static uint8_t *disk;
static size_t disk_size;
static int workspace_failure;
static unsigned workspace_count;
typedef struct { u32 lba;u8 bytes[512]; } WriteRecord;
static WriteRecord writes[256];
static unsigned trace,count_writes;
u32 ticks;
const Credentials *security_current(void) { return &caller; }
int process_get_current_pid(void) { return 0; }
vfs_file_t **process_get_current_file_slot(unsigned fd) { return fd<VFS_MAX_FDS?&fds[fd]:NULL; }
size_t process64_capacity(void) { return 4; }
AddressSpace *vmm64_kernel(void) { abort(); }
VmResult vmm64_alloc_page(AddressSpace *s,virt_addr_t v,unsigned f) { (void)s;(void)v;(void)f;abort(); }
VmResult vmm64_unmap(AddressSpace *s,virt_addr_t v,int f,phys_addr_t *m) { (void)s;(void)v;(void)f;(void)m;abort(); }
void memory_log(const char *s) { (void)s; }
void memory_hex(uint64_t x) { (void)x; }
void memory_context_check(void) {}
void *fs_journal_alloc(size_t bytes) { return calloc(1,bytes); }
void memory_require(int ok,const char *message) { if(!ok) { puts(message); abort(); } }
void klog(const char *c,const char *l,const char *m) { (void)c;(void)l;(void)m; }
int ata_read_sector(u32 lba,void *buffer) {
    if((uint64_t)lba*512+512>disk_size) return 0;
    memcpy(buffer,disk+(size_t)lba*512,512); return 1;
}
int ata_write_sector(u32 lba,const void *buffer) {
    if((uint64_t)lba*512+512>disk_size) return 0;
    if(trace) { if(count_writes>=256) abort();writes[count_writes].lba=lba;memcpy(writes[count_writes++].bytes,buffer,512); }
    memcpy(disk+(size_t)lba*512,buffer,512); return 1;
}
int ata_flush(void) { return 1; }
void *account_work_alloc(size_t n) { if(workspace_failure) return NULL; void *p=malloc(n); if(p)++workspace_count; return p; }
void account_work_free(void *p,size_t n) { (void)n; free(p); --workspace_count; }
uint64_t account_platform_time(void) { static uint64_t clock; return ++clock; }
int account_platform_random(uint32_t *v) { (void)v; return 0; }
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void put(const char *path,const void *data,u32 n) {
    int fd=vfs_open(path,O_WRONLY|O_CREAT|O_TRUNC); CHECK(fd>=3);
    CHECK(vfs_write(fd,data,n)==(int)n); CHECK(vfs_close(fd)==0);
}
static void argon_vector(void) {
    /* RFC 9106 section 5.3, independently published Argon2id vector. */
    uint8_t pass[32],salt_bytes[16],key[32],ad[12],hash[32];
    memset(pass,1,32);memset(salt_bytes,2,16);memset(key,3,32);memset(ad,4,12);
    uint8_t golden[32]={0x0d,0x64,0x0d,0xf5,0x8d,0x78,0x76,0x6c,0x08,0xc0,0x37,0xa3,0x4a,0x8b,0x53,0xc9,0xd0,0x1e,0xf0,0x45,0x2d,0x75,0xb6,0x5e,0xb5,0x25,0x20,0xe9,0x6b,0x01,0xe6,0x59};
    uint64_t work[32*128];
    crypto_argon2(hash,32,work,(crypto_argon2_config){CRYPTO_ARGON2_ID,32,3,4},
        (crypto_argon2_inputs){pass,salt_bytes,32,16},(crypto_argon2_extras){key,ad,8,12});
    CHECK(account_equal(hash,golden,32));
}
static int contents(const char *path,const char *expected) {
    int fd=vfs_open(path,O_RDONLY);if(fd<0) return 0;
    char text[8]={0};int n=vfs_read(fd,text,sizeof(text));vfs_close(fd);
    return n==(int)strlen(expected) && !memcmp(text,expected,(size_t)n);
}
static void crash_transactions(void) {
    put("/home/crash-a","aaaa",4);put("/home/crash-b","bbbb",4);
    u8 *base=malloc(disk_size),*frozen=malloc(disk_size);CHECK(base && frozen);
    memcpy(base,disk,disk_size);trace=1;count_writes=0;
    CHECK(vfs_rename_replace("/home/crash-a","/home/crash-b")==0);trace=0;
    unsigned records=count_writes,commit=0;
    for(unsigned n=0;n<=records;n++) {
        memcpy(disk,base,disk_size);
        for(unsigned i=0;i<n;i++) memcpy(disk+(size_t)writes[i].lba*512,writes[i].bytes,512);
        vfs_init();CHECK(pollikfs_mounted() && !pollikfs_readonly());
        int old=contents("/home/crash-a","aaaa") && contents("/home/crash-b","bbbb");
        int next=!contents("/home/crash-a","aaaa") && contents("/home/crash-b","aaaa");
        CHECK(old || next);
        if(n<records && writes[n].lba==0 && le32(writes[n].bytes+8)==1) commit=n+1;
    }
    CHECK(commit);
    memcpy(disk,base,disk_size);
    for(unsigned i=0;i<commit;i++) memcpy(disk+(size_t)writes[i].lba*512,writes[i].bytes,512);
    disk[512+7]^=0x80;memcpy(frozen,disk,disk_size);
    vfs_init();CHECK(pollikfs_mounted() && pollikfs_readonly());
    CHECK(!memcmp(frozen,disk,disk_size));CHECK(vfs_unlink("/home/crash-a")<0);
    memcpy(disk,base,disk_size);disk[32768+1024+1000/8]^=1u<<(1000%8);
    memcpy(frozen,disk,disk_size);vfs_init();CHECK(pollikfs_mounted() && pollikfs_readonly());
    CHECK(!memcmp(frozen,disk,disk_size));
    memcpy(disk,base,disk_size);vfs_init();CHECK(!pollikfs_readonly());
    free(base);free(frozen);
    printf("PASS: %u sector-write crash prefixes recover atomic rename; corrupt journal/bitmap mount read-only without writes\n",records+1);
}
int main(int argc,char **argv) {
    if(argc==4 && !strcmp(argv[1],"--verify-account")) {
        AccountRecord a; FILE *record=fopen(argv[2],"rb"); CHECK(record);
        CHECK(fread(&a,1,sizeof(a),record)==sizeof(a)); fclose(record);
        CHECK(a.version==2 && valid(&a) && account_verify(&a,argv[3]));
        CHECK(workspace_count==0); account_wipe(&a,sizeof(a));
        puts("PASS: guest-created Argon2id account matches the RFC-verified native implementation");
        return 0;
    }
    CHECK(argc==2); FILE *image=fopen(argv[1],"rb"); CHECK(image);
    CHECK(fseek(image,0,SEEK_END)==0); disk_size=(size_t)ftell(image); rewind(image);
    disk=malloc(disk_size); CHECK(disk && fread(disk,1,disk_size,image)==disk_size); fclose(image);
    /* Preallocate the native descriptor pool; real VFS allocation stays used. */
    g_system_file_count=4*(USER_FD_LIMIT-3);
    g_system_files=calloc(g_system_file_count,sizeof(*g_system_files));
    g_free_file_indices=calloc(g_system_file_count,sizeof(*g_free_file_indices));
    CHECK(g_system_files && g_free_file_indices); vfs_init(); CHECK(pollikfs_mounted());
    argon_vector();
    put("/home/range.txt","safe",4);
    int fd=vfs_open("/home/range.txt",O_RDWR); CHECK(fd>=3);
    vfs_stat_t before,after; CHECK(vfs_fstat(fd,&before)==0);
    uint8_t *snapshot=malloc(disk_size); CHECK(snapshot); memcpy(snapshot,disk,disk_size);
    const u32 bad_positions[]={0x7fffffff,POLLIK2_MAX_FILE,POLLIK2_MAX_FILE-1};
    for(unsigned i=0;i<3;i++) {
        CHECK(vfs_seek(fd,(int)bad_positions[i],SEEK_SET)>=0);
        CHECK(vfs_write(fd,"xx",2)==-1); CHECK(pollikfs_error()==VFS_NOSPACE);
        CHECK(vfs_fstat(fd,&after)==0 && before.size==after.size);
        CHECK(memcmp(snapshot,disk,disk_size)==0);
    }
    CHECK(vfs_seek(fd,0x7fffffff,SEEK_SET)>=0);
    CHECK(pollikfs_write(fds[fd],"x",0)==0 && memcmp(snapshot,disk,disk_size)==0);
    CHECK(vfs_seek(fd,1024*1024,SEEK_SET)>=0);
    pollikfs_fail_after(0); CHECK(vfs_write(fd,"x",1)<0); pollikfs_fail_after(-1);
    CHECK(vfs_fstat(fd,&after)==0 && after.size==4); CHECK(vfs_close(fd)==0);
    AccountRecord a; CHECK(account_load(&a)==0);
    CHECK(account_create(&a,"tester","secret123")); CHECK(a.version==2);
    CHECK(account_load(&a)==1 && !account_verify(&a,"badpass"));
    CHECK(account_verify(&a,"secret123"));
    workspace_failure=1; CHECK(!account_verify(&a,"secret123")); workspace_failure=0;
    CHECK(account_change(&a,"secret123","changed123"));
    CHECK(!account_verify(&a,"secret123") && account_verify(&a,"changed123"));
    CHECK(workspace_count==0);
    /* A root-opened credential fd remains inaccessible after inheritance. */
    fd=vfs_open(ACCOUNT_PATH,O_RDONLY); CHECK(fd>=3);
    security_session_begin("tester"); caller=security_user_credentials(); Credentials old=caller;
    uint8_t bytes[128]; CHECK(vfs_read(fd,bytes,128)<0 && pollikfs_error()==VFS_DENIED); CHECK(vfs_close(fd)==0);
    CHECK(vfs_open("/home/../etc/account.db",O_RDONLY)<0);
    CHECK(vfs_stat("//etc/./account.db",&after)<0);
    CHECK(vfs_unlink(ACCOUNT_PATH)<0 && vfs_rename(ACCOUNT_PATH,"/home/stolen")<0);
    CHECK(vfs_open("/bin/replace",O_WRONLY|O_CREAT)<0);
    Credentials administrator=caller;administrator.capabilities=CAP_ADMIN_ALL;
    CHECK(security_path_allowed(&administrator,"/etc/account.db",ACCESS_READ));
    CHECK(security_path_allowed(&administrator,"/bin/replace",ACCESS_WRITE));
    put("/home/user.txt","ok",2);
    fd=vfs_open("/home/append-created",O_WRONLY|O_CREAT|O_APPEND);
    if(fd<0) fprintf(stderr,"append create: fs_error=%d readonly=%d caps=%u\n",pollikfs_error(),pollikfs_readonly(),caller.capabilities);
    CHECK(fd>=3);CHECK(vfs_close(fd)==0);
    CHECK(security_session_request(&caller,SESSION_LOCKED));
    CHECK(!security_credentials_runnable(&caller)); CHECK(vfs_open("/home/user.txt",O_RDONLY)<0);
    CHECK(!security_path_allowed(&administrator,"/etc/account.db",ACCESS_READ));
    security_session_resume(); CHECK(security_credentials_runnable(&caller));
    security_session_end(); security_session_begin("tester");
    CHECK(!security_credentials_live(&old)); caller=security_user_credentials();
    CHECK(!security_path_allowed(&administrator,"/bin/replace",ACCESS_WRITE));
    CHECK(!security_session_request(&old,SESSION_LOGOUT));
    security_session_end(); caller=(Credentials){0};
    /* Legacy v1 golden bytes are supplied by the Python hashlib fixture. */
    uint8_t legacy[128]; image=fopen("build/security-legacy.bin","rb"); CHECK(image);
    CHECK(fread(legacy,1,128,image)==128); fclose(image); put(ACCOUNT_PATH,legacy,128);
    CHECK(account_load(&a)==1 && a.version==1);
    CHECK(!account_verify(&a,"wrong123") && a.version==1);
    /* Authentication must survive a deferred upgrade, without changing the
     * legacy record or allowing writes to a protected/read-only volume. */
    memcpy(snapshot,disk,disk_size);fs_journal_force_readonly();
    CHECK(account_verify(&a,"legacy123") && a.version==1);
    CHECK(!account_verify(&a,"wrong123"));
    CHECK(!account_change(&a,"legacy123","changed123"));
    CHECK(pollikfs_readonly() && !memcmp(snapshot,disk,disk_size));
    vfs_init();CHECK(!pollikfs_readonly());
    workspace_failure=1;
    CHECK(account_verify(&a,"legacy123") && a.version==1);
    CHECK(!memcmp(snapshot,disk,disk_size));workspace_failure=0;
    CHECK(account_verify(&a,"legacy123") && a.version==2);
    CHECK(account_load(&a)==1 && a.version==2 && account_verify(&a,"legacy123"));
    CHECK(vfs_unlink(ACCOUNT_PATH)==0); CHECK(account_load(&a)==-1);
    CHECK(workspace_count==0 && vfs_debug_handles()==0);
    crash_transactions();
    puts("PASS: PollikFS range/zero/ENOSPC; VFS rights and inherited fd; session lock/revocation; RFC Argon2id; legacy migration, password change, OOM and deleted-account fail-closed");
    free(snapshot);free(disk);free(g_system_files);free(g_free_file_indices);return 0;
}
