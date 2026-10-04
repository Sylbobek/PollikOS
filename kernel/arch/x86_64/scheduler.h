#ifndef POLLIK_X64_SCHEDULER_H
#define POLLIK_X64_SCHEDULER_H
#include "user.h"
/* Owns runnable dispatch, timer accounting and context switching. The process
 * manager retains PID slots, process resources and terminal-state ownership. */
#define TIMER64_VECTOR 32
#ifdef POLLIK_TEST_TIMER_HZ
#define TIMER64_HZ POLLIK_TEST_TIMER_HZ
#else
#define TIMER64_HZ 100
#endif
/* BSP only; all APIs require IF=0. Successful submit transfers ownership.
 * Boundary/completion callbacks run on the dispatcher stack, never user RSP0.
 * Completion callback is read-only; its process is destroyed on return. */
typedef struct { uint64_t ticks, user_ticks, idle_ticks, switches, dispatches, reaped; } Scheduler64Stats;
typedef void (*Scheduler64Boundary)(void);
typedef void (*Scheduler64Completion)(const Process64 *);
int scheduler64_submit(Process64 *process);
int scheduler64_run(uint64_t maximum_ticks, Scheduler64Boundary boundary, Scheduler64Completion completion);
int process64_kill(uint64_t pid, int reason);
int process64_block(uint64_t pid);
int process64_wake(uint64_t pid);
void process64_wake_expired(void); /* timer-tick scan for sleep deadlines */
int process64_tick_limit(uint64_t pid, uint64_t ticks);
uint64_t scheduler64_ticks(void); /* monotonic PIT ticks since scheduler start */
Scheduler64Stats scheduler64_stats(void);
size_t scheduler64_count(void);
void scheduler64_timer(UserFrame *frame);
void timer64_start(void);
void timer64_stop(void);
void timer64_ack(void);
void scheduler64_demo(void);
#ifdef SELFTEST
void scheduler64_selftest(void);
void scheduler64_fail_submit(int fail);
#endif
#endif
