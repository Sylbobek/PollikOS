#ifndef POLLIKOS_TYPES_H
#define POLLIKOS_TYPES_H
#include <stddef.h>
#include <stdint.h>
/* SDK-visible ABI types are fixed by the x86_64 LP64 PollikOS target. They are
 * never derived from host platform typedefs. */
typedef long ssize_t;   /* signed transfer size, matches the syscall ABI */
typedef long off_t;     /* signed file offset, bounded 32-bit by the kernel */
typedef int pid_t;      /* process ids are stable 64-bit kernel values narrowed */
typedef int mode_t;
#endif
