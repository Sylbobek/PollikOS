/* x86_64 console authentication using the existing i386 account.db wire format.
 * The data disk is never formatted or reset here. */
#include <stdint.h>
#include <stddef.h>
#include <bearssl.h>
#include "auth64.h"
#include "tty.h"
#include "scheduler.h"
#include "../../pollikfs.h"
#include "../../vfs.h"
#include "../../hal.h"

#define ACCOUNT_MAGIC UINT32_C(0x31524341)
#define ACCOUNT_VERSION 1u
#define ACCOUNT_KDF_ROUNDS 8192u
#define ACCOUNT_PATH "/etc/account.db"
#define ACCOUNT_PENDING "/etc/account.db.pending"
#define INSTALL_MARKER "/etc/pollikos-installed"

typedef struct __attribute__((packed)) {
    u32 magic;
    u32 version;
    u32 rounds;
    char username[32];
    u8 salt[16];
    u8 password_hash[32];
    u8 reserved[36];
} Account64;
_Static_assert(sizeof(Account64)==128,"shared account.db wire layout");

extern void kernel64_debug_bytes(const char *data,size_t length);

static size_t text_length(const char *text) {
    size_t n=0;
    while (text[n]) ++n;
    return n;
}
static void output(const char *text) { kernel64_debug_bytes(text,text_length(text)); }
static void zero(void *memory,size_t length) {
    volatile u8 *p=(volatile u8 *)memory;
    while (length--) *p++=0;
}
static int equal_secret(const u8 *a,const u8 *b,size_t length) {
    u8 difference=0;
    for (size_t i=0;i<length;i++) difference|=a[i]^b[i];
    return difference==0;
}
static void password_kdf(const char *password,const u8 salt[16],u32 rounds,u8 result[32]) {
    br_sha256_context context;
    size_t length=text_length(password);
    br_sha256_init(&context);
    br_sha256_update(&context,salt,16);
    br_sha256_update(&context,password,length);
    br_sha256_out(&context,result);
    for (u32 round=1;round<rounds;round++) {
        br_sha256_init(&context);
        br_sha256_update(&context,result,32);
        br_sha256_update(&context,salt,16);
        br_sha256_update(&context,password,length);
        br_sha256_out(&context,result);
    }
    zero(&context,sizeof(context));
}
static int read_line(const char *prompt,char *buffer,size_t capacity,int secret) {
    size_t length=0;
    if (!capacity) return 0;
    zero(buffer,capacity);
    output(prompt);
    for (;;) {
        u8 key;
        if (!tty64_pop(&key,1)) {
            hal_cpu_idle_once_disabled();
            continue;
        }
        if (key=='\r' || key=='\n') {
            output("\r\n");
            buffer[length]=0;
            return 1;
        }
        if ((key==8 || key==127) && length) {
            buffer[--length]=0;
            if (!secret) output("\b \b");
            continue;
        }
        if (key>=32 && key<127 && length+1<capacity) {
            buffer[length++]=(char)key;
            if (!secret) kernel64_debug_bytes((const char *)&key,1);
            else output("*");
        }
    }
}
static int valid_username(const char *name) {
    size_t length=0;
    while (name[length]) {
        char c=name[length++];
        if (!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-')) return 0;
        if (length>31) return 0;
    }
    return length>=2;
}
static int account_load(Account64 *account) {
    vfs_stat_t status;
    if (vfs_stat(ACCOUNT_PATH,&status)<0)
        return pollikfs_error()==VFS_NOT_FOUND ? 0 : -1;
    if (status.type!=VFS_FILE || status.size!=sizeof(*account)) return -1;
    int fd=vfs_open(ACCOUNT_PATH,O_RDONLY);
    if (fd<0) return -1;
    int read=vfs_read(fd,account,sizeof(*account));
    int closed=vfs_close(fd);
    if (read!=(int)sizeof(*account)||closed<0 || account->magic!=ACCOUNT_MAGIC ||
        account->version!=ACCOUNT_VERSION || account->rounds<1024 ||
        account->rounds>1000000 || !account->username[0] || account->username[31]) return -1;
    return 1;
}
static void make_salt(Account64 *account) {
    br_sha256_context context;
    u8 digest[32],rtc[8];
    u64 tsc=(u64)hal_read_tsc();
    for (u8 i=0;i<8;i++) {
        hal_port_write8(0x70, i);
        rtc[i]=hal_port_read8(0x71);
    }
    u64 ticks=scheduler64_ticks();
    br_sha256_init(&context);
    br_sha256_update(&context,&tsc,sizeof(tsc));
    br_sha256_update(&context,&ticks,sizeof(ticks));
    br_sha256_update(&context,rtc,sizeof(rtc));
    br_sha256_update(&context,account->username,sizeof(account->username));
    br_sha256_out(&context,digest);
    for (unsigned i=0;i<sizeof(account->salt);i++) account->salt[i]=digest[i];
    zero(digest,sizeof(digest));
    zero(&context,sizeof(context));
}
static int write_exact(const char *path,const void *data,u32 length) {
    int fd=vfs_open(path,O_WRONLY|O_CREAT|O_TRUNC);
    if (fd<0) return 0;
    int written=vfs_write(fd,data,length);
    int closed=vfs_close(fd);
    return written==(int)length && closed==0;
}
static int create_account(Account64 *account,const char *username,const char *password) {
    zero(account,sizeof(*account));
    account->magic=ACCOUNT_MAGIC;
    account->version=ACCOUNT_VERSION;
    account->rounds=ACCOUNT_KDF_ROUNDS;
    for (size_t i=0;username[i];i++) account->username[i]=username[i];
    (void)vfs_mkdir("/etc");
    (void)vfs_mkdir("/home");
    char home[38]="/home/";
    size_t at=6;
    for (size_t i=0;username[i];i++) home[at++]=username[i];
    home[at]=0;
    if (vfs_mkdir(home)<0) {
        vfs_stat_t status;
        if (vfs_stat(home,&status)<0 || status.type!=VFS_DIR) return 0;
    }
    make_salt(account);
    password_kdf(password,account->salt,account->rounds,account->password_hash);
    if (!write_exact(ACCOUNT_PENDING,account,sizeof(*account)) ||
        vfs_rename(ACCOUNT_PENDING,ACCOUNT_PATH)<0) {
        (void)vfs_unlink(ACCOUNT_PENDING);
        zero(account,sizeof(*account));
        return 0;
    }
    static const char marker[]="PollikOS installation complete\n";
    (void)write_exact(INSTALL_MARKER,marker,sizeof(marker)-1);
    return 1;
}
static int setup_account(Account64 *account) {
    char username[32],password[64],confirmation[64];
    for (;;) {
        read_line("Create account name: ",username,sizeof(username),0);
        if (valid_username(username)) break;
        output("Use 2-31 lowercase letters, numbers, '-' or '_'.\r\n");
    }
    for (;;) {
        for (;;) {
            read_line("Create password (6-63 characters): ",password,sizeof(password),1);
            if (text_length(password)>=6) break;
            output("Password must contain at least 6 characters.\r\n");
        }
        read_line("Confirm password: ",confirmation,sizeof(confirmation),1);
        if (equal_secret((const u8 *)password,(const u8 *)confirmation,sizeof(password))) break;
        output("Passwords do not match. Try again.\r\n");
        zero(password,sizeof(password));
        zero(confirmation,sizeof(confirmation));
    }
    int saved=create_account(account,username,password);
    zero(password,sizeof(password));
    zero(confirmation,sizeof(confirmation));
    zero(username,sizeof(username));
    if (!saved) { output("Could not save the account; data was not formatted.\r\n"); return 0; }
    output("Account created.\r\n");
    return 1;
}
int auth64_login(void) {
    Account64 account;
    if (!pollikfs_mounted()) {
        output("[AUTH64] Data filesystem unavailable; login denied. No formatting was performed.\r\n");
        return 0;
    }
    int loaded=account_load(&account);
    if (loaded<0) {
        output("[AUTH64] Account database is invalid; login denied. Data was left untouched.\r\n");
        return 0;
    }
    if (!loaded) {
        output("[AUTH64] First run: create a local account.\r\n");
        int created=setup_account(&account);
        zero(&account,sizeof(account));
        return created;
    }
    output("[AUTH64] Sign in to PollikOS.\r\n");
    for (unsigned attempt=0;attempt<5;attempt++) {
        char username[32],password[64];
        u8 candidate[32];
        read_line("Username: ",username,sizeof(username),0);
        read_line("Password: ",password,sizeof(password),1);
        password_kdf(password,account.salt,account.rounds,candidate);
        int accepted=equal_secret((const u8 *)username,(const u8 *)account.username,sizeof(username)) &&
                     equal_secret(candidate,account.password_hash,sizeof(candidate));
        zero(candidate,sizeof(candidate));
        zero(password,sizeof(password));
        zero(username,sizeof(username));
        if (accepted) {
            output("[AUTH64] Sign-in successful.\r\n");
            zero(&account,sizeof(account));
            return 1;
        }
        output("Sign-in failed.\r\n");
    }
    output("[AUTH64] Too many failed attempts; restarting the kernel is required.\r\n");
    zero(&account,sizeof(account));
    return 0;
}
