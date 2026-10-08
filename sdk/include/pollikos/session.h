#ifndef POLLIKOS_SESSION_H
#define POLLIKOS_SESSION_H
#include <pollikos/abi_numbers.h>
/* ELEVATE opens the kernel-owned password prompt. Success authorizes one
 * SPAWN_RIGHTS with USER_CAP_ADMIN_ALL; the caller keeps its existing rights.
 * RIGHTS reads the current process's capabilities. */
long pollikos_session_control(unsigned operation);
long pollikos_session_elevate(const char *application_path);
#endif
