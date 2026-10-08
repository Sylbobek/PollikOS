#ifndef POLLIK_FD64_H
#define POLLIK_FD64_H
#include <stdint.h>
struct vfs_file;
typedef enum { FD64_CLOSED, FD64_VFS, FD64_STDIN, FD64_STDOUT, FD64_STDERR, FD64_PIPE } Fd64Kind;
enum { FD64_READ=1, FD64_WRITE=2, FD64_CLOEXEC=1 };
/* One authoritative slot: backing, type, rights and inheritance policy. */
typedef struct {
    struct vfs_file *file;
    uint8_t kind, rights, flags, pipe;
} Descriptor64;
#endif
