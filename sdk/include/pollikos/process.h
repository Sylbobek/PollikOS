#ifndef POLLIKOS_PROCESS_H
#define POLLIKOS_PROCESS_H
#include <stddef.h>
#include <pollikos/syscall.h>
#include <pollikos/types.h>
/* Raw PollikOS calls return the nonnegative result or a negative USER_E* code.
 * POSIX-style wrappers return -1 and set errno. Process creation is spawn-only
 * (no fork): spawn builds a fresh ELF64 process and returns its PID. */
long pollikos_getpid(void);
long pollikos_chdir(const char *path);
long pollikos_getcwd(char *buffer, unsigned long capacity);
void pollikos_exit(int status) __attribute__((noreturn));
pid_t getpid(void);
pid_t getpgid(pid_t pid);
pid_t getpgrp(void);
int setpgid(pid_t pid, pid_t pgid);
int chdir(const char *path);
/* getcwd returns the buffer or NULL with errno ERANGE/EFAULT/EIO. */
char *getcwd(char *buffer, size_t size);
/* Typed, versioned termination record returned by waitpid. */
typedef struct pollikos_wait {
    uint32_t version, kind;
    int32_t code;      /* exit status, fault vector or kill reason */
    uint32_t reserved;
    uint64_t address;  /* fault address for kind FAULTED, else zero */
} pollikos_wait_t;
#define POLLIKOS_WAIT_VERSION USER_WAIT_VERSION
#define POLLIKOS_WAIT_EXITED USER_WAIT_KIND_EXITED
#define POLLIKOS_WAIT_FAULTED USER_WAIT_KIND_FAULTED
#define POLLIKOS_WAIT_KILLED USER_WAIT_KIND_KILLED
/* spawn: explicit argv must be NULL-terminated (or NULL for none); envp NULL
 * inherits the caller's current environment, otherwise it must be
 * NULL-terminated. Returns the child PID or a negative USER_E* code. */
long pollikos_spawn(const char *path, const char *const argv[], const char *const envp[]);
/* Atomically place the child in pgid before it becomes runnable. A zero pgid
 * makes the child a new process-group leader. */
long pollikos_spawn_group(const char *path, const char *const argv[], const char *const envp[], pid_t pgid);
/* spawnp: path-based if the name contains '/', otherwise search PATH. */
long pollikos_spawnp(const char *file, const char *const argv[], const char *const envp[]);
long pollikos_spawnp_group(const char *file, const char *const argv[], const char *const envp[], pid_t pgid);
/* PollikOS typed wait record. pid 0 and -1 mean any child; pid<-1 selects
 * children in process group -pid. This raw API returns negative USER_E* codes. */
long pollikos_waitpid(long pid, pollikos_wait_t *status);
/* Convenience: spawn, wait and return the child's exit code (or -1). */
int pollikos_run(const char *path, const char *const argv[], int *exit_code);
/* Minimal process control: kill a direct child (no signals; reason is an
 * integer), and declare which child owns Ctrl+C on the console. */
long pollikos_kill(long pid, int reason);
long pollikos_foreground(long pid);
long pollikos_spawn_rights(const char *path,const char *const argv[],const char *const envp[],unsigned rights);
#endif
