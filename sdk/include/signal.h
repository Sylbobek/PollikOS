#ifndef POLLIKOS_SIGNAL_H
#define POLLIKOS_SIGNAL_H
#include <stdint.h>
#include <sys/types.h>

typedef uint64_t sigset_t;
typedef void (*sighandler_t)(int);

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define NSIG 32

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

struct sigaction {
    sighandler_t sa_handler;
    sigset_t sa_mask;
    unsigned long sa_flags; /* zero is the only supported flag set */
    void (*sa_restorer)(void); /* filled by the SDK when omitted */
};

int sigaction(int signal, const struct sigaction *action, struct sigaction *old_action);
int sigprocmask(int how, const sigset_t *set, sigset_t *old_set);
int sigpending(sigset_t *set);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signal);
int sigdelset(sigset_t *set, int signal);
int sigismember(const sigset_t *set, int signal);
sighandler_t signal(int signal, sighandler_t handler);
int kill(pid_t pid, int signal);
int raise(int signal);

#endif
