#ifndef POLLIK_HEAP64_H
#define POLLIK_HEAP64_H
#include "user.h"
/* C2 per-process userspace memory primitives. The kernel supplies memory;
 * allocator policy (malloc/free) stays in the future libc.
 * brk: requested==0 queries, otherwise the exact new break is committed.
 * mmap: anonymous private RW/NX pages only, flags must be zero.
 * munmap: exact ownership record required; no splitting or partial unmap. */
void heap64_reset(Process64 *process);
int heap64_dispatch(Process64 *process, UserFrame *frame);
int64_t heap64_brk(Process64 *process, uint64_t requested);
int64_t heap64_mmap(Process64 *process, uint64_t length, uint64_t flags);
int64_t heap64_munmap(Process64 *process, uint64_t address, uint64_t length);
void heap64_demo(void);
#ifdef SELFTEST
void heap64_selftest(void);
#endif
#endif
