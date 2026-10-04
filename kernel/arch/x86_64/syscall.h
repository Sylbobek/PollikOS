#ifndef POLLIK_SYSCALL64_H
#define POLLIK_SYSCALL64_H
#include "user.h"
void syscall64_init(void);
void syscall64_stack(uintptr_t top);
#ifdef SELFTEST
int process64_test_return(Process64 *p, UserFrame *frame);
#endif
#endif
