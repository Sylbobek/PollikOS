#ifndef POLLIK_FILE64_H
#define POLLIK_FILE64_H
#include "user.h"
typedef enum { FD64_CLOSED, FD64_VFS, FD64_STDIN, FD64_STDOUT, FD64_STDERR, FD64_PIPE } Fd64Kind;
Fd64Kind file64_kind(const Process64 *p, uint64_t fd);
unsigned file64_stream_count(const Process64 *p);
void file64_init(Process64 *process);
void file64_clone_parent(Process64 *child, Process64 *parent);
int file64_dispatch(Process64 *process, UserFrame *frame);
void file64_cleanup(Process64 *process);
unsigned file64_count(const Process64 *process);
void file64_demo(void);
void stat64_demo(void);
void dir64_demo(void);
void runtime64_demo(void);
#ifdef SELFTEST
void file64_selftest(void);
void stat64_selftest(void);
void dir64_selftest(void);
void runtime64_selftest(void);
#endif
#endif
