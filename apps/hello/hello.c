#include "../../include/pollikos.h"

static int syscall_abi_test(void) {
    PollikAbiInfo info = {0};
    info.size = sizeof(info);
    if (pollikos_get_abi_info(&info) != (int)sizeof(info) ||
        info.size != sizeof(info) || info.major != POLLIKOS_ABI_VERSION_MAJOR ||
        info.minor != POLLIKOS_ABI_VERSION_MINOR ||
        info.architecture != POLLIKOS_ABI_ARCH_I386 ||
        info.transport != POLLIKOS_ABI_TRANSPORT_INT80 ||
        info.operation_namespace != POLLIKOS_ABI_NAMESPACE_I386 ||
        (info.features & (POLLIKOS_ABI_FEATURE_PROCESS | POLLIKOS_ABI_FEATURE_FILES)) !=
            (POLLIKOS_ABI_FEATURE_PROCESS | POLLIKOS_ABI_FEATURE_FILES))
        return 0;

    if (pollikos_get_abi_info((PollikAbiInfo *)0xC0000000u) != -POLLIKOS_EFAULT)
        return 0;
    u32 short_size = POLLIKOS_ABI_INFO_MIN_SIZE - 1;
    if (pollikos_get_abi_info((PollikAbiInfo *)&short_size) != -POLLIKOS_EINVAL ||
        short_size != POLLIKOS_ABI_INFO_MIN_SIZE - 1)
        return 0;
    if (write(1, (const void *)0xC0000000u, 1) != -POLLIKOS_EFAULT)
        return 0;

    return 1;
}

int main(void) {
    int pid = getpid();
    write(1, "Hello from Ring 3 ELF!\n", 23);

    static const char abi_pass[] = "[TEST] SYSCALL ABI PASS\n";
    static const char abi_fail[] = "[TEST] SYSCALL ABI FAIL\n";
    const char *abi_message = syscall_abi_test() ? abi_pass : abi_fail;
    write(1, abi_message, strlen(abi_message));

    /* Deliberately leave the reservation open. The process reaper must return
     * it to the global table after every exit in the 100-cycle stress test. */
    static const char socket_pass[] = "[TEST] SOCKET OWNER PASS\n";
    static const char socket_fail[] = "[TEST] SOCKET OWNER FAIL\n";
    const char *socket_message = pollikos_socket(0, 0, 0) >= 100 ? socket_pass : socket_fail;
    write(1, socket_message, strlen(socket_message));

    /* Test IPC self-send and receive */
    const char msg[] = "IPC_PING_OK";
    int send_res = pollikos_send_ipc(pid, msg, sizeof(msg));
    if (send_res > 0) {
        int src = -1;
        char buf[32] = {0};
        int recv_res = pollikos_recv_ipc(&src, buf, sizeof(buf));
        if (recv_res > 0 && src == pid) {
            write(1, "[TEST] IPC PASS\n", 16);
        }
    }

    /* Test Event poll */
    SystemEvent ev;
    int ev_res = pollikos_poll_event(&ev);
    if (ev_res == 0) {
        write(1, "[TEST] EVENT_QUEUE PASS\n", 24);
    }

    yield();
    return 42;
}
