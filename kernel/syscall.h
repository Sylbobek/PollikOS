#ifndef POLLIK_SYSCALL_H
#define POLLIK_SYSCALL_H

#define SYS_EXIT       1
#define SYS_FORK       2
#define SYS_READ       3
#define SYS_WRITE      4
#define SYS_OPEN       5
#define SYS_CLOSE      6
#define SYS_SPAWN      11
#define SYS_EXEC       12
#define SYS_TIME       13
#define SYS_SEEK       19
#define SYS_GETPID     20
#define SYS_YIELD      24
#define SYS_ALLOC      45
#define SYS_FREE       91
#define SYS_SLEEP      162
#define SYS_WAIT_EVENT 163
#define SYS_POLL_EVENT 164
#define SYS_SEND_IPC   165
#define SYS_RECV_IPC   166
#define SYS_SOCKET     167
#define SYS_CONNECT    168
#define SYS_SEND       169
#define SYS_RECV       170
#define SYS_ABI_INFO   171

/* Standard POSIX-style Error Codes */
#define EPERM   1
#define ENOENT  2
#define EIO     5
#define EBADF   9
#define EAGAIN  11
#define ENOMEM  12
#define EFAULT  14
#define EINVAL  22
#define ENOSYS  38

void syscall_init(void);
void *syscall_dispatch(void *frame_ptr);
void syscall_close_process_sockets(int pid);

#endif
