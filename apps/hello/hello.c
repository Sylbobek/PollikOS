#include "../../include/pollikos.h"

int main(void) {
    int pid = getpid();
    write(1, "Hello from Ring 3 ELF!\n", 23);

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
