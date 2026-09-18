#ifndef POLLIK_PROCESS_H
#define POLLIK_PROCESS_H

#include "system.h"
#include "vmm.h"

#define MAX_PROCESSES 16

typedef enum {
    PROC_STATE_UNUSED = 0,
    PROC_STATE_READY,
    PROC_STATE_RUNNING,
    PROC_STATE_BLOCKED,
    PROC_STATE_SLEEPING,
    PROC_STATE_DEAD
} ProcessState;

typedef struct {
    u32 edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    u32 gs, fs, es, ds, vector, error, eip, cs, eflags, useresp, ss;
} ProcessFrame;

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

#define MAX_PROCESS_EVENTS 16
#define MAX_IPC_MSG_LEN    64
#define MAX_PROCESS_IPC    8

typedef struct {
    int src_pid;
    int dest_pid;
    u32 len;
    u8 data[MAX_IPC_MSG_LEN];
} IpcMessage;

typedef struct {
    ProcessFrame *frame;
    int pid;
    int ppid;
    char name[24];
    ProcessState state;
    int alive;
    int paused;
    int is_flat;
    int exit_code;
    u32 reports;
    u32 switches;
    page_directory_t *page_directory;
    uintptr_t kernel_stack_bottom;
    uintptr_t kernel_stack_top;
    uintptr_t user_stack_bottom;
    uintptr_t user_stack_top;
    uintptr_t entry_point;
    uintptr_t heap_start;
    uintptr_t heap_end;
    u32 sleep_until_tick;
    void *fd_table[16]; /* vfs_file_t * array */
    SystemEvent event_queue[MAX_PROCESS_EVENTS];
    int event_head;
    int event_tail;
    int event_count;
    IpcMessage ipc_queue[MAX_PROCESS_IPC];
    int ipc_head;
    int ipc_tail;
    int ipc_count;
    u8 fpu_state[108] __attribute__((aligned(16)));
} ProcessControlBlock;

void process_init(void);
void process_list(char *out);
int process_action(int pid, int action);
void process_fault_test(void);
int process_spawn_elf(const char *name, const u8 *elf_data, u32 elf_size);
int process_spawn_elf_path(const char *path);
void process_exit_current(int exit_code);
int process_get_current_pid(void);
const char *process_get_current_name(void);
int process_is_current_flat(void);
void process_report_legacy(u32 count);
int process_get_fault_request(void);
uintptr_t process_sbrk(u32 bytes);
void process_sleep_current(u32 ticks_count);
void process_block_current(void);
void process_unblock(int pid);
int process_send_event(int pid, const SystemEvent *ev);
int process_poll_event(int pid, SystemEvent *out_ev);
int process_wait_event(int pid, SystemEvent *out_ev);
int process_send_ipc(int src_pid, int dest_pid, const void *data, u32 len);
int process_recv_ipc(int pid, int *out_src, void *out_buf, u32 max_len);
ProcessFrame *process_schedule(ProcessFrame *frame);
void **process_get_current_fd_table(void);
void phase2_poll(void);

#endif
