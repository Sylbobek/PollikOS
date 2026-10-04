#ifndef POLLIK_X64_SCHEDULER_INTERNAL_H
#define POLLIK_X64_SCHEDULER_INTERNAL_H
#include "user.h"

/* Internal boundary between process lifetime and runnable execution. */
Process64 *scheduler64_current(void);
Thread64 *scheduler64_current_thread(void);
int scheduler64_running(void);
void scheduler64_enqueue(Thread64 *thread);
void scheduler64_dequeue(Thread64 *thread);
Thread64 *scheduler64_take_next(void);
size_t scheduler64_runnable_count(void);
int scheduler64_context_begin(Process64 *process);
uintptr_t *scheduler64_resume_stack(void);
void scheduler64_leave_current(int switch_to_kernel_cr3) __attribute__((noreturn));
void scheduler64_park(Thread64 *thread, UserFrame *frame) __attribute__((noreturn));
void scheduler64_suspend(Thread64 *thread, UserFrame *frame) __attribute__((noreturn));

#endif
