#ifndef POLLIK_SECURITY_H
#define POLLIK_SECURITY_H
#include <stdint.h>
/* Kernel-owned credentials. No syscall can supply or replace this record. */
typedef struct { uint32_t uid, gid; uint64_t session; uint32_t capabilities; } Credentials;
enum { CAP_FILE_READ=1, CAP_FILE_WRITE=2, CAP_NETWORK=4, CAP_WINDOW=8,
       CAP_SESSION=16, CAP_DEVICE=32, CAP_ADMIN=64, CAP_USER_DEFAULT=31, CAP_ADMIN_ALL=127 };
int security_has(const Credentials *credentials,unsigned capabilities);
enum { SESSION_NONE, SESSION_ACTIVE, SESSION_LOCKED, SESSION_LOGOUT, SESSION_PASSWORD, SESSION_ELEVATE, SESSION_FACTORY_RESET };
enum { ACCESS_READ=1, ACCESS_WRITE=2 };
const Credentials *security_current(void); /* architecture-owned caller context */
Credentials security_user_credentials(void);
int security_credentials_live(const Credentials *credentials);
int security_credentials_runnable(const Credentials *credentials);
int security_path_allowed(const Credentials *credentials, const char *path, unsigned access);
unsigned security_path_user_access(const char *path);
uint64_t security_session_id(void);
int security_session_state(void);
const char *security_username(void);
void security_session_begin(const char *username);
int security_session_request(const Credentials *caller, unsigned state);
void security_session_resume(void);
void security_session_end(void);
#endif
