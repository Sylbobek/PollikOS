/* Process, working directory, monotonic time and blocking sleep wrappers. */
#include <pollikos/process.h>
#include <sys/wait.h>
#include <signal.h>
#include <pollikos/time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
long pollikos_getpid(void) { return __pollikos_syscall0(USER_GETPID); }
long pollikos_chdir(const char *path) { return __pollikos_syscall1(USER_CHDIR, (uint64_t)(uintptr_t)path); }
long pollikos_getcwd(char *buffer, unsigned long capacity) {
    return __pollikos_syscall2(USER_GETCWD, (uint64_t)(uintptr_t)buffer, capacity);
}
void pollikos_exit(int status) { _exit(status); }
pid_t getpid(void) { return (int)pollikos_getpid(); }
pid_t getpgid(pid_t pid) {
    int64_t result = __pollikos_syscall1(USER_GETPGID, (uint64_t)(int64_t)pid);
    return result < 0 ? (pid_t)__pollikos_fail(result) : (pid_t)result;
}
pid_t getpgrp(void) { return getpgid(0); }
int setpgid(pid_t pid, pid_t pgid) {
    int64_t result = __pollikos_syscall2(USER_SETPGID, (uint64_t)(int64_t)pid,
                                         (uint64_t)(int64_t)pgid);
    return result < 0 ? (int)__pollikos_fail(result) : 0;
}
int chdir(const char *path) {
    long result = pollikos_chdir(path);
    return result < 0 ? (int)__pollikos_fail(result) : 0;
}
char *getcwd(char *buffer, size_t size) {
    long result = pollikos_getcwd(buffer, (unsigned long)size);
    if (result < 0) { __pollikos_fail(result); return NULL; }
    return buffer;
}
uint64_t __pollikos_clocks_per_second(void) { return 100; }
long pollikos_clock_ticks(void) { return __pollikos_syscall1(USER_CLOCK, USER_CLOCK_TICKS); }
long pollikos_monotonic_ms(void) { return __pollikos_syscall1(USER_CLOCK, USER_CLOCK_MS); }
long pollikos_sleep_ms(unsigned long milliseconds) {
    return __pollikos_syscall1(USER_SLEEP, milliseconds);
}
int sleep_ms(unsigned int milliseconds) {
    long result = pollikos_sleep_ms(milliseconds);
    return result < 0 ? (int)__pollikos_fail(result) : 0;
}
long pollikos_spawn(const char *path, const char *const argv[], const char *const envp[]) {
    const char *const *environment = envp ? envp : (const char *const *)environ;
    return __pollikos_syscall3(USER_SPAWN, (uint64_t)(uintptr_t)path,
                               (uint64_t)(uintptr_t)argv, (uint64_t)(uintptr_t)environment);
}
long pollikos_spawn_group(const char *path, const char *const argv[], const char *const envp[], pid_t pgid) {
    const char *const *environment = envp ? envp : (const char *const *)environ;
    return __pollikos_syscall4(USER_SPAWN_GROUP, (uint64_t)(uintptr_t)path,
        (uint64_t)(uintptr_t)argv, (uint64_t)(uintptr_t)environment,
        (uint64_t)(int64_t)pgid);
}
static int contains_slash(const char *text) {
    for (; *text; ++text) if (*text == '/') return 1;
    return 0;
}
static long spawnp_internal(const char *file, const char *const argv[], const char *const envp[],
                            int with_group, pid_t pgid);
long pollikos_spawnp(const char *file, const char *const argv[], const char *const envp[]) {
    return spawnp_internal(file, argv, envp, 0, 0);
}
static long spawnp_internal(const char *file, const char *const argv[], const char *const envp[],
                            int with_group, pid_t pgid) {
    if (!file || !*file) { errno = EINVAL; return -1; }
    if (contains_slash(file)) return with_group ? pollikos_spawn_group(file, argv, envp, pgid)
                                                  : pollikos_spawn(file, argv, envp);
    const char *path = getenv("PATH");
    if (!path || !*path) path = "/bin";
    char candidate[USER_PATH_MAX];
    size_t file_length = strlen(file);
    while (*path) {
        const char *end = path;
        while (*end && *end != ':') ++end;
        size_t directory_length = (size_t)(end - path);
        /* Empty PATH components are skipped instead of meaning the cwd. */
        if (directory_length && directory_length + 1 + file_length + 1 <= sizeof(candidate)) {
            memcpy(candidate, path, directory_length);
            candidate[directory_length] = '/';
            memcpy(candidate + directory_length + 1, file, file_length + 1);
            long result = with_group ? pollikos_spawn_group(candidate, argv, envp, pgid)
                                     : pollikos_spawn(candidate, argv, envp);
            if (result >= 0) return result;
            if (result != -(long)USER_ENOENT) return result;
        }
        path = *end ? end + 1 : end;
    }
    errno = ENOENT;
    return -(long)USER_ENOENT;
}
long pollikos_spawnp_group(const char *file, const char *const argv[], const char *const envp[], pid_t pgid) {
    return spawnp_internal(file, argv, envp, 1, pgid);
}
long pollikos_waitpid(long pid, pollikos_wait_t *status) {
    return __pollikos_syscall3(USER_WAITPID, (uint64_t)pid, (uint64_t)(uintptr_t)status, 0);
}
pid_t waitpid(pid_t pid, int *status, int options) {
    if (options & ~WNOHANG) { (void)__pollikos_fail(-(int64_t)USER_EINVAL); return -1; }
    int64_t selector = pid;
    if (pid == 0) {
        pid_t group = getpgrp();
        if (group < 0) return -1;
        selector = -(int64_t)group;
    }
    pollikos_wait_t result_status;
    int64_t result = __pollikos_syscall3(USER_WAITPID, (uint64_t)selector,
        (uint64_t)(uintptr_t)&result_status, (uint64_t)(unsigned)options);
    if (result < 0) { (void)__pollikos_fail(result); return -1; }
    if (result == 0 || !status) return (pid_t)result;
    if (result_status.kind == POLLIKOS_WAIT_EXITED) {
        *status = ((unsigned)result_status.code & 0xffu) << 8;
    } else if (result_status.kind == POLLIKOS_WAIT_KILLED) {
        int signal = result_status.code;
        if (signal <= 0 || signal >= NSIG) signal = SIGKILL;
        *status = signal & 0x7f;
    } else {
        switch (result_status.code) {
        case 0: *status = SIGFPE; break;
        case 1: case 3: *status = SIGTRAP; break;
        case 6: *status = SIGILL; break;
        default: *status = SIGSEGV; break;
        }
    }
    return (pid_t)result;
}
int pollikos_run(const char *path, const char *const argv[], int *exit_code) {
    long child = pollikos_spawn(path, argv, 0);
    if (child < 0) { (void)__pollikos_fail(child); return -1; }
    pollikos_wait_t status;
    long result = pollikos_waitpid(child, &status);
    if (result < 0) { (void)__pollikos_fail(result); return -1; }
    if (exit_code)
        *exit_code = status.kind == POLLIKOS_WAIT_EXITED ? status.code : -1;
    return 0;
}
long pollikos_kill(long pid, int reason) {
    return __pollikos_syscall2(USER_KILL, (uint64_t)pid, (uint64_t)(uint32_t)reason);
}
long pollikos_foreground(long pid) {
    return __pollikos_syscall1(USER_FOREGROUND, (uint64_t)pid);
}

long pollikos_spawn_rights(const char *path,const char *const argv[],const char *const envp[],unsigned rights) {
    return __pollikos_syscall4(USER_SPAWN_RIGHTS,(uint64_t)(uintptr_t)path,(uint64_t)(uintptr_t)argv,(uint64_t)(uintptr_t)envp,rights);
}
