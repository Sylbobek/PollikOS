#ifndef POLLIK_X64_PROCESS_INTERNAL_H
#define POLLIK_X64_PROCESS_INTERNAL_H
#include "user.h"

/* Process-manager hooks used by the scheduler; not part of the userspace ABI. */
Process64 *process64_internal_slot(size_t index);
Process64 *process64_internal_find(uint64_t pid);
int process64_internal_known(const Process64 *process);
int process64_internal_dead(const Process64 *process);
int process64_internal_parent_live(uint64_t pid);
void process64_internal_transition(Process64 *process, Process64State next);
void process64_internal_wake_waiter(Process64 *child);
int process64_internal_signal_prepare(Process64 *process, UserFrame *frame);
int process64_internal_return_valid(Process64 *process, UserFrame *frame);
void process64_internal_tty_wake_readers(void);

#endif
