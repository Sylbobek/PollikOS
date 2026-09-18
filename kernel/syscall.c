#include "syscall.h"
#include "vfs.h"
#include "vmm.h"
#include "pmm.h"
#include "klog.h"
#include "process.h"
#include "net/tcp.h"
#include "net/net_manager.h"

typedef ProcessFrame Frame;

/* External process hooks */
extern int process_get_current_pid(void);
extern void process_exit_current(int exit_code);
extern int process_is_current_flat(void);
extern void process_report_legacy(u32 count);
extern int process_get_fault_request(void);
extern uintptr_t process_sbrk(u32 bytes);
extern void process_sleep_current(u32 ticks_count);
extern void process_block_current(void);
extern int process_wait_event(int pid, SystemEvent *out_ev);
extern int process_poll_event(int pid, SystemEvent *out_ev);
extern int process_send_ipc(int src_pid, int dest_pid, const void *data, u32 len);
extern int process_recv_ipc(int pid, int *out_src, void *out_buf, u32 max_len);

#define MAX_USER_SOCKETS 8
static TcpSocket *g_user_sockets[MAX_USER_SOCKETS];

void syscall_init(void) {
    for (int i = 0; i < MAX_USER_SOCKETS; i++)
        g_user_sockets[i] = 0;
    KLOG_INFO(KLOG_CAT_PROC, "Syscall dispatcher initialized (INT 0x80)");
}

void *syscall_dispatch(void *frame_ptr) {
    Frame *f = (Frame *)frame_ptr;
    u32 num = f->eax;

    /* Handle legacy worker reports */
    if (!process_is_current_flat()) {
        if (num == 1) {
            process_report_legacy(f->ebx);
            f->eax = (process_get_fault_request() == process_get_current_pid());
            return f;
        }
    }

    switch (num) {
    case SYS_EXIT: {
        int code = (int)f->ebx;
        process_exit_current(code);
        return process_schedule(f);
    }
    case SYS_OPEN: {
        const char *u_path = (const char *)f->ebx;
        int flags = (int)f->ecx;
        char kpath[VFS_MAX_PATH];
        if (!copy_string_from_user(kpath, u_path, sizeof(kpath))) {
            f->eax = (u32)-EFAULT;
            return f;
        }
        int res = vfs_open(kpath, flags);
        f->eax = (res < 0) ? (u32)-ENOENT : (u32)res;
        return f;
    }
    case SYS_CLOSE: {
        int fd = (int)f->ebx;
        if (fd >= 100 && fd < 100 + MAX_USER_SOCKETS) {
            int s = fd - 100;
            if (g_user_sockets[s] > (TcpSocket *)1) {
                tcp_close(g_user_sockets[s]);
            }
            g_user_sockets[s] = 0;
            f->eax = 0;
            return f;
        }
        int res = vfs_close(fd);
        f->eax = (res < 0) ? (u32)-EBADF : 0;
        return f;
    }
    case SYS_READ: {
        int fd = (int)f->ebx;
        void *u_buf = (void *)f->ecx;
        u32 len = f->edx;

        if (len == 0) {
            f->eax = 0;
            return f;
        }

        if (!user_range_valid(0, u_buf, len, 1)) {
            KLOG_WARN(KLOG_CAT_PROC, "SYS_READ: invalid user buffer pointer");
            f->eax = (u32)-EFAULT;
            return f;
        }

        char kbuf[128];
        u32 total = 0;
        while (total < len) {
            u32 chunk = len - total;
            if (chunk > sizeof(kbuf))
                chunk = sizeof(kbuf);
            int rd = vfs_read(fd, kbuf, chunk);
            if (rd <= 0) {
                if (total == 0) total = (rd < 0) ? (u32)-EIO : 0;
                break;
            }
            copy_to_user((u8 *)u_buf + total, kbuf, (u32)rd);
            total += (u32)rd;
            if ((u32)rd < chunk)
                break;
        }
        f->eax = total;
        return f;
    }
    case SYS_WRITE: {
        int fd = (int)f->ebx;
        const void *u_buf = (const void *)f->ecx;
        u32 len = f->edx;

        if (len == 0) {
            f->eax = 0;
            return f;
        }

        if (!user_range_valid(0, u_buf, len, 0)) {
            KLOG_WARN(KLOG_CAT_PROC, "SYS_WRITE: invalid user buffer pointer");
            f->eax = (u32)-EFAULT;
            return f;
        }

        if (fd == 1 || fd == 2) { /* stdout, stderr */
            char kbuf[128];
            u32 total = 0;
            while (total < len) {
                u32 chunk = len - total;
                if (chunk > sizeof(kbuf) - 1)
                    chunk = sizeof(kbuf) - 1;
                copy_from_user(kbuf, (const u8 *)u_buf + total, chunk);
                kbuf[chunk] = 0;
                serial(kbuf);
                total += chunk;
            }
            f->eax = total;
        } else {
            char kbuf[128];
            u32 total = 0;
            while (total < len) {
                u32 chunk = len - total;
                if (chunk > sizeof(kbuf))
                    chunk = sizeof(kbuf);
                copy_from_user(kbuf, (const u8 *)u_buf + total, chunk);
                int wr = vfs_write(fd, kbuf, chunk);
                if (wr <= 0) {
                    if (total == 0) total = (u32)-EIO;
                    break;
                }
                total += (u32)wr;
            }
            f->eax = total;
        }
        return f;
    }
    case SYS_SEEK: {
        int fd = (int)f->ebx;
        int offset = (int)f->ecx;
        int whence = (int)f->edx;
        int res = vfs_seek(fd, offset, whence);
        f->eax = (res < 0) ? (u32)-EINVAL : (u32)res;
        return f;
    }
    case SYS_SPAWN: {
        const char *u_path = (const char *)f->ebx;
        char kpath[VFS_MAX_PATH];
        if (!copy_string_from_user(kpath, u_path, sizeof(kpath))) {
            f->eax = (u32)-EFAULT;
            return f;
        }
        extern int process_spawn_elf_path(const char *path);
        int pid = process_spawn_elf_path(kpath);
        f->eax = (pid < 0) ? (u32)-ENOENT : (u32)pid;
        return f;
    }
    case SYS_GETPID: {
        int cur_pid = process_get_current_pid();
        char nb[12];
        number(nb, cur_pid);
        serial("[SYSCALL] pid="); serial(nb); serial(" SYS_GETPID -> "); serial(nb); serial("\n");
        f->eax = (u32)cur_pid;
        return f;
    }
    case SYS_YIELD: {
        f->eax = 0;
        return process_schedule(f);
    }
    case SYS_ALLOC: {
        u32 bytes = f->ebx;
        uintptr_t old_brk = process_sbrk(bytes);
        f->eax = old_brk ? (u32)old_brk : (u32)-ENOMEM;
        return f;
    }
    case SYS_TIME: {
        f->eax = ticks;
        return f;
    }
    case SYS_SLEEP: {
        u32 duration = f->ebx;
        process_sleep_current(duration);
        return process_schedule(f);
    }
    case SYS_WAIT_EVENT: {
        void *u_ev = (void *)f->ebx;
        if (!user_range_valid(0, u_ev, sizeof(SystemEvent), 1)) {
            f->eax = (u32)-EFAULT;
            return f;
        }
        SystemEvent ev;
        int cur_pid = process_get_current_pid();
        if (process_poll_event(cur_pid, &ev)) {
            copy_to_user((u8 *)u_ev, (const u8 *)&ev, sizeof(SystemEvent));
            f->eax = 1;
            return f;
        }
        process_block_current();
        f->eax = (u32)-EAGAIN;
        return process_schedule(f);
    }
    case SYS_POLL_EVENT: {
        void *u_ev = (void *)f->ebx;
        if (!user_range_valid(0, u_ev, sizeof(SystemEvent), 1)) {
            f->eax = (u32)-EFAULT;
            return f;
        }
        SystemEvent ev;
        int cur_pid = process_get_current_pid();
        if (process_poll_event(cur_pid, &ev)) {
            copy_to_user((u8 *)u_ev, (const u8 *)&ev, sizeof(SystemEvent));
            f->eax = 1;
        } else {
            f->eax = 0;
        }
        return f;
    }
    case SYS_SEND_IPC: {
        int dest_pid = (int)f->ebx;
        const void *u_data = (const void *)f->ecx;
        u32 len = f->edx;
        if (len > MAX_IPC_MSG_LEN)
            len = MAX_IPC_MSG_LEN;
        u8 k_buf[MAX_IPC_MSG_LEN];
        if (len > 0) {
            if (!user_range_valid(0, u_data, len, 0)) {
                f->eax = (u32)-EFAULT;
                return f;
            }
            copy_from_user(k_buf, (const u8 *)u_data, len);
        }
        int cur_pid = process_get_current_pid();
        int res = process_send_ipc(cur_pid, dest_pid, k_buf, len);
        f->eax = (res < 0) ? (u32)-EINVAL : (u32)res;
        return f;
    }
    case SYS_RECV_IPC: {
        int *u_src = (int *)f->ebx;
        void *u_buf = (void *)f->ecx;
        u32 max_len = f->edx;
        if (max_len > MAX_IPC_MSG_LEN)
            max_len = MAX_IPC_MSG_LEN;
        if (u_src && !user_range_valid(0, u_src, sizeof(int), 1)) {
            f->eax = (u32)-EFAULT;
            return f;
        }
        if (max_len > 0 && !user_range_valid(0, u_buf, max_len, 1)) {
            f->eax = (u32)-EFAULT;
            return f;
        }
        int src_pid = 0;
        u8 k_buf[MAX_IPC_MSG_LEN];
        int cur_pid = process_get_current_pid();
        int res = process_recv_ipc(cur_pid, &src_pid, k_buf, max_len);
        if (res >= 0) {
            if (u_src)
                copy_to_user((u8 *)u_src, (const u8 *)&src_pid, sizeof(int));
            if (u_buf && res > 0)
                copy_to_user((u8 *)u_buf, k_buf, (u32)res);
            f->eax = (u32)res;
        } else {
            f->eax = (u32)-EAGAIN;
        }
        return f;
    }
    case SYS_SOCKET: {
        int slot = -1;
        for (int i = 0; i < MAX_USER_SOCKETS; i++) {
            if (!g_user_sockets[i]) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            f->eax = (u32)-ENOMEM;
            return f;
        }
        g_user_sockets[slot] = (TcpSocket *)1;
        f->eax = (u32)(slot + 100);
        return f;
    }
    case SYS_CONNECT: {
        int fd = (int)f->ebx;
        if (fd < 100 || fd >= 100 + MAX_USER_SOCKETS) {
            f->eax = (u32)-EBADF;
            return f;
        }
        int slot = fd - 100;
        const u8 *u_ip = (const u8 *)f->ecx;
        u16 port = (u16)f->edx;
        if (!user_range_valid(0, u_ip, 4, 0)) {
            f->eax = (u32)-EFAULT;
            return f;
        }
        u8 k_ip[4];
        copy_from_user(k_ip, u_ip, 4);

        TcpSocket *sock = net_manager_connect_tcp(k_ip, port);
        if (!sock) {
            f->eax = (u32)-EIO;
            return f;
        }
        g_user_sockets[slot] = sock;
        f->eax = 0;
        return f;
    }
    case SYS_SEND: {
        int fd = (int)f->ebx;
        if (fd < 100 || fd >= 100 + MAX_USER_SOCKETS) {
            f->eax = (u32)-EBADF;
            return f;
        }
        int slot = fd - 100;
        TcpSocket *sock = g_user_sockets[slot];
        if (!sock || sock == (TcpSocket *)1) {
            f->eax = (u32)-EBADF;
            return f;
        }
        const void *u_buf = (const void *)f->ecx;
        u32 len = f->edx;
        if (len == 0) {
            f->eax = 0;
            return f;
        }
        if (!user_range_valid(0, u_buf, len, 0)) {
            f->eax = (u32)-EFAULT;
            return f;
        }
        u8 k_buf[256];
        u32 total = 0;
        while (total < len) {
            u32 chunk = len - total;
            if (chunk > sizeof(k_buf)) chunk = sizeof(k_buf);
            copy_from_user(k_buf, (const u8 *)u_buf + total, chunk);
            int sent = tcp_send(sock, k_buf, (int)chunk);
            if (sent <= 0) break;
            total += (u32)sent;
            if ((u32)sent < chunk) break;
        }
        f->eax = total;
        return f;
    }
    case SYS_RECV: {
        int fd = (int)f->ebx;
        if (fd < 100 || fd >= 100 + MAX_USER_SOCKETS) {
            f->eax = (u32)-EBADF;
            return f;
        }
        int slot = fd - 100;
        TcpSocket *sock = g_user_sockets[slot];
        if (!sock || sock == (TcpSocket *)1) {
            f->eax = (u32)-EBADF;
            return f;
        }
        void *u_buf = (void *)f->ecx;
        u32 max_len = f->edx;
        if (max_len == 0) {
            f->eax = 0;
            return f;
        }
        if (!user_range_valid(0, u_buf, max_len, 1)) {
            f->eax = (u32)-EFAULT;
            return f;
        }
        u8 k_buf[256];
        u32 chunk = max_len > sizeof(k_buf) ? sizeof(k_buf) : max_len;
        int rd = tcp_read(sock, k_buf, (int)chunk);
        if (rd > 0) {
            copy_to_user((u8 *)u_buf, k_buf, (u32)rd);
            f->eax = (u32)rd;
        } else if (rd == 0) {
            f->eax = tcp_is_connected(sock) ? (u32)-EAGAIN : 0;
        } else {
            f->eax = 0;
        }
        return f;
    }
    default:
        klog_hex(KLOG_CAT_PROC, "Unknown syscall invoked: ", num);
        f->eax = (u32)-ENOSYS;
        return f;
    }
}
