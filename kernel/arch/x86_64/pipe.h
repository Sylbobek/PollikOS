#ifndef POLLIK_X64_PIPE_H
#define POLLIK_X64_PIPE_H
#include <stddef.h>
#include <stdint.h>
/* Bounded kernel pipes for the x86_64 shell. Each pipe is a small ring; an fd
 * refers to one end through Process64.pipe_index/pipe_write. Blocking is done
 * by the process layer, which owns the scheduler state and user copies. */
#define PIPE64_MAX 8
#define PIPE64_SIZE 4096
int pipe64_create(uint8_t *index);            /* 0 on success, -1 when full */
void pipe64_retain(int index, int write);
void pipe64_release(int index, int write);    /* close one end; wakes peers */
int pipe64_used(int index);
unsigned pipe64_has_readers(int index);
unsigned pipe64_has_writers(int index);
size_t pipe64_available(int index);
size_t pipe64_space(int index);
size_t pipe64_write_data(int index, const void *source, size_t count);
size_t pipe64_read_data(int index, void *destination, size_t count);
/* Implemented by the process layer: rescan blocked pipe transfers. */
void pipe64_wake_blocked(void);
#endif
