#ifndef POLLIKOS_SDK_H
#define POLLIKOS_SDK_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned int size_t;

#define NULL ((void*)0)

#define SYS_EXIT   1
#define SYS_READ   3
#define SYS_WRITE  4
#define SYS_OPEN   5
#define SYS_CLOSE  6
#define SYS_SPAWN  11
#define SYS_TIME   13
#define SYS_SEEK   19
#define SYS_GETPID 20
#define SYS_YIELD  24
#define SYS_ALLOC      45
#define SYS_SLEEP      162
#define SYS_WAIT_EVENT 163
#define SYS_POLL_EVENT 164
#define SYS_SEND_IPC   165
#define SYS_RECV_IPC   166
#define SYS_SOCKET     167
#define SYS_CONNECT    168
#define SYS_SEND       169
#define SYS_RECV       170

typedef enum {
    EVENT_NONE = 0,
    EVENT_KEY_DOWN,
    EVENT_KEY_UP,
    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_DOWN,
    EVENT_MOUSE_UP,
    EVENT_MOUSE_WHEEL,
    EVENT_WINDOW_FOCUS,
    EVENT_WINDOW_BLUR,
    EVENT_WINDOW_MOVE,
    EVENT_WINDOW_RESIZE,
    EVENT_WINDOW_CLOSE,
    EVENT_TIMER,
    EVENT_IPC_MESSAGE,
    EVENT_NETWORK
} EventType;

typedef struct {
    EventType type;
    u32 param1;
    u32 param2;
    u32 param3;
    u32 param4;
} SystemEvent;

#define MAX_IPC_MSG_LEN 64

typedef struct {
    int src_pid;
    int dest_pid;
    u32 len;
    u8 data[MAX_IPC_MSG_LEN];
} IpcMessage;

#define O_RDONLY   0x0001
#define O_WRONLY   0x0002
#define O_RDWR     0x0003
#define O_CREAT    0x0100
#define O_TRUNC    0x0200
#define O_APPEND   0x0400

#define SEEK_SET   0
#define SEEK_CUR   1
#define SEEK_END   2

static inline u32 _syscall0(u32 num) {
    u32 ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num) : "memory");
    return ret;
}

static inline u32 _syscall1(u32 num, u32 a1) {
    u32 ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1) : "memory");
    return ret;
}

static inline u32 _syscall2(u32 num, u32 a1, u32 a2) {
    u32 ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2) : "memory");
    return ret;
}

static inline u32 _syscall3(u32 num, u32 a1, u32 a2, u32 a3) {
    u32 ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}

__attribute__((noreturn)) static inline void exit(int code) {
    __asm__ volatile(
        "movl $1, %%eax\n\t"
        "movl %0, %%ebx\n\t"
        "int $0x80\n\t"
        "1: jmp 1b\n\t"
        : : "r"(code) : "eax", "ebx", "memory"
    );
    __builtin_unreachable();
}

static inline int open(const char *path, int flags) {
    return (int)_syscall2(SYS_OPEN, (u32)path, (u32)flags);
}

static inline int close(int fd) {
    return (int)_syscall1(SYS_CLOSE, (u32)fd);
}

static inline int seek(int fd, int offset, int whence) {
    return (int)_syscall3(SYS_SEEK, (u32)fd, (u32)offset, (u32)whence);
}

static inline int spawn(const char *path) {
    return (int)_syscall1(SYS_SPAWN, (u32)path);
}

static inline int write(int fd, const void *buf, u32 count) {
    return (int)_syscall3(SYS_WRITE, (u32)fd, (u32)buf, count);
}

static inline int read(int fd, void *buf, u32 count) {
    return (int)_syscall3(SYS_READ, (u32)fd, (u32)buf, count);
}

static inline int getpid(void) {
    return (int)_syscall0(SYS_GETPID);
}

static inline void yield(void) {
    _syscall0(SYS_YIELD);
}

static inline void sleep(u32 ticks_count) {
    _syscall1(SYS_SLEEP, ticks_count);
}

static inline void *sbrk(u32 bytes) {
    return (void *)_syscall1(SYS_ALLOC, bytes);
}

static inline u32 uptime(void) {
    return _syscall0(SYS_TIME);
}

static inline u32 strlen(const char *s) {
    u32 n = 0;
    while (s[n]) n++;
    return n;
}

static inline void puts(const char *s) {
    write(1, s, strlen(s));
    write(1, "\n", 1);
}

static inline int pollikos_wait_event(SystemEvent *ev) {
    while (1) {
        int res = (int)_syscall1(SYS_POLL_EVENT, (u32)ev);
        if (res > 0) return res;
        _syscall1(SYS_SLEEP, 1);
    }
}

static inline int pollikos_poll_event(SystemEvent *ev) {
    return (int)_syscall1(SYS_POLL_EVENT, (u32)ev);
}

static inline int pollikos_send_ipc(int dest_pid, const void *data, u32 len) {
    return (int)_syscall3(SYS_SEND_IPC, (u32)dest_pid, (u32)data, len);
}

static inline int pollikos_recv_ipc(int *out_src_pid, void *out_buf, u32 max_len) {
    return (int)_syscall3(SYS_RECV_IPC, (u32)out_src_pid, (u32)out_buf, max_len);
}

static inline int pollikos_socket(int domain, int type, int proto) {
    return (int)_syscall3(SYS_SOCKET, (u32)domain, (u32)type, (u32)proto);
}

static inline int pollikos_connect(int sock, const u8 *ip, u16 port) {
    return (int)_syscall3(SYS_CONNECT, (u32)sock, (u32)ip, (u32)port);
}

static inline int pollikos_send(int sock, const void *buf, u32 len) {
    return (int)_syscall3(SYS_SEND, (u32)sock, (u32)buf, len);
}

static inline int pollikos_recv(int sock, void *buf, u32 max_len) {
    return (int)_syscall3(SYS_RECV, (u32)sock, (u32)buf, max_len);
}

int main(void);

void _start(void) {
    int code = main();
    exit(code);
}

#endif
