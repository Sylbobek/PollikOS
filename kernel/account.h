#ifndef POLLIK_ACCOUNT_H
#define POLLIK_ACCOUNT_H
#include <stdint.h>
#include <stddef.h>
#define ACCOUNT_MAGIC 0x31524341u
#define ACCOUNT_VERSION 2u
#define ACCOUNT_PATH "/etc/account.db"
#define INSTALL_MARKER "/etc/pollikos-installed"
#define ACCOUNT_MEMORY_KIB 19456u
#define ACCOUNT_PASSES 2u
typedef struct __attribute__((packed)) {
    uint32_t magic, version, rounds;
    char username[32];
    uint8_t salt[16], password_hash[32];
    uint32_t algorithm, memory_kib, lanes;
    uint8_t reserved[24];
} AccountRecord;
_Static_assert(sizeof(AccountRecord)==128,"account v1/v2 wire layout");
void account_wipe(void *memory,size_t length);
int account_equal(const void *a,const void *b,size_t length);
int account_valid_username(const char *name);
int account_load(AccountRecord *account); /* 1 valid, 0 first run, -1 error */
int account_create(AccountRecord *account,const char *name,const char *password);
int account_verify(AccountRecord *account,const char *password); /* migrates v1 when storage permits */
int account_change(AccountRecord *account,const char *old_password,const char *new_password);
void account_entropy_event(uint64_t timing);
/* Native platform allocation, never host-side hashing. */
void *account_work_alloc(size_t bytes);
void account_work_free(void *memory,size_t bytes);
uint64_t account_platform_time(void);
int account_platform_random(uint32_t *value);
#endif
