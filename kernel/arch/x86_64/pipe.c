/* Kernel pipe table for the x86_64 shell.
 *
 * Pipes are deliberately separate from the VFS object pool: they are byte
 * streams with two process-visible ends and no filesystem backing. The table
 * is small and fixed; each pipe is a bounded 4 KiB ring. End lifetimes are
 * reference counted so EOF (last writer) and broken-pipe (last reader) are
 * visible to blocked peers through pipe64_wake_blocked().
 */
#include "pipe.h"
#include "user.h"

typedef struct {
    unsigned used;
    unsigned readers, writers;
    unsigned head, tail, count;
    uint8_t buffer[PIPE64_SIZE];
} Pipe64;

static Pipe64 pipes[PIPE64_MAX];

int pipe64_create(uint8_t *index) {
    for (int slot = 0; slot < PIPE64_MAX; ++slot) {
        if (pipes[slot].used) continue;
        Pipe64 *pipe = &pipes[slot];
        pipe->used = 1;
        pipe->readers = 1;
        pipe->writers = 1;
        pipe->head = pipe->tail = pipe->count = 0;
        *index = (uint8_t)slot;
        return 0;
    }
    return -1;
}
int pipe64_used(int index) {
    return index >= 0 && index < PIPE64_MAX && pipes[index].used;
}
void pipe64_retain(int index, int write) {
    if (!pipe64_used(index)) return;
    if (write) ++pipes[index].writers;
    else ++pipes[index].readers;
}
void pipe64_release(int index, int write) {
    if (!pipe64_used(index)) return;
    Pipe64 *pipe = &pipes[index];
    if (write && pipe->writers) --pipe->writers;
    if (!write && pipe->readers) --pipe->readers;
    if (!pipe->readers && !pipe->writers) pipe->used = 0;
}
unsigned pipe64_has_readers(int index) {
    return pipe64_used(index) && pipes[index].readers > 0;
}
unsigned pipe64_has_writers(int index) {
    return pipe64_used(index) && pipes[index].writers > 0;
}
size_t pipe64_available(int index) {
    return pipe64_used(index) ? pipes[index].count : 0;
}
size_t pipe64_space(int index) {
    return pipe64_used(index) ? PIPE64_SIZE - pipes[index].count : 0;
}
size_t pipe64_write_data(int index, const void *source, size_t count) {
    if (!pipe64_used(index)) return 0;
    Pipe64 *pipe = &pipes[index];
    const uint8_t *bytes = source;
    size_t written = 0;
    while (written < count && pipe->count < PIPE64_SIZE) {
        pipe->buffer[pipe->tail] = bytes[written++];
        pipe->tail = (pipe->tail + 1) % PIPE64_SIZE;
        ++pipe->count;
    }
    return written;
}
size_t pipe64_read_data(int index, void *destination, size_t count) {
    if (!pipe64_used(index)) return 0;
    Pipe64 *pipe = &pipes[index];
    uint8_t *bytes = destination;
    size_t taken = 0;
    while (taken < count && pipe->count) {
        bytes[taken++] = pipe->buffer[pipe->head];
        pipe->head = (pipe->head + 1) % PIPE64_SIZE;
        --pipe->count;
    }
    return taken;
}
