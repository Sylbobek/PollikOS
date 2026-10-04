#include <signal.h>
#include <errno.h>
#include <pollikos/process.h>
#include <pollikos/syscall.h>

extern void __pollikos_sigreturn(void);

static int valid_signal(int signal) {
    return (signal >= SIGHUP && signal <= SIGTERM) || signal == SIGCHLD ||
        signal == SIGCONT || signal == SIGSTOP;
}
int sigemptyset(sigset_t *set) {
    if (!set) { errno = EFAULT; return -1; }
    *set = 0;
    return 0;
}
int sigfillset(sigset_t *set) {
    if (!set) { errno = EFAULT; return -1; }
    *set = UINT64_MAX;
    return 0;
}
int sigaddset(sigset_t *set, int signal_number) {
    if (!set || !valid_signal(signal_number)) { errno = EINVAL; return -1; }
    *set |= UINT64_C(1) << signal_number;
    return 0;
}
int sigdelset(sigset_t *set, int signal_number) {
    if (!set || !valid_signal(signal_number)) { errno = EINVAL; return -1; }
    *set &= ~(UINT64_C(1) << signal_number);
    return 0;
}
int sigismember(const sigset_t *set, int signal_number) {
    if (!set || !valid_signal(signal_number)) { errno = EINVAL; return -1; }
    return (*set & (UINT64_C(1) << signal_number)) != 0;
}
int sigaction(int signal_number, const struct sigaction *action, struct sigaction *old_action) {
    if (action && action->sa_handler != SIG_DFL && action->sa_handler != SIG_IGN &&
        !action->sa_restorer) {
        struct sigaction adjusted = *action;
        adjusted.sa_restorer = __pollikos_sigreturn;
        int64_t result = __pollikos_syscall3(USER_SIGACTION, (uint64_t)(unsigned)signal_number,
            (uint64_t)(uintptr_t)&adjusted, (uint64_t)(uintptr_t)old_action);
        return result < 0 ? (int)__pollikos_fail(result) : 0;
    }
    int64_t result = __pollikos_syscall3(USER_SIGACTION, (uint64_t)(unsigned)signal_number,
        (uint64_t)(uintptr_t)action, (uint64_t)(uintptr_t)old_action);
    return result < 0 ? (int)__pollikos_fail(result) : 0;
}
int sigprocmask(int how, const sigset_t *set, sigset_t *old_set) {
    int64_t result = __pollikos_syscall3(USER_SIGPROCMASK, (uint64_t)(unsigned)how,
        (uint64_t)(uintptr_t)set, (uint64_t)(uintptr_t)old_set);
    return result < 0 ? (int)__pollikos_fail(result) : 0;
}
int sigpending(sigset_t *set) {
    int64_t result = __pollikos_syscall1(USER_SIGPENDING, (uint64_t)(uintptr_t)set);
    return result < 0 ? (int)__pollikos_fail(result) : 0;
}
sighandler_t signal(int signal_number, sighandler_t handler) {
    struct sigaction action = {handler, 0, 0, 0}, old_action;
    if (sigaction(signal_number, &action, &old_action) < 0) return SIG_ERR;
    return old_action.sa_handler;
}
int kill(pid_t pid, int signal_number) {
    int64_t result = __pollikos_syscall2(USER_SIGNAL_SEND, (uint64_t)(int64_t)pid,
                                         (uint64_t)(unsigned)signal_number);
    return result < 0 ? (int)__pollikos_fail(result) : 0;
}
int raise(int signal_number) {
    return kill((pid_t)pollikos_getpid(), signal_number);
}
