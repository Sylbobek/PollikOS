#ifndef POLLIKOS_DIRENT_H
#define POLLIKOS_DIRENT_H
/* Compatibility aliases for the pollikos_dirent_t stream entry. PollikOS does
 * not implement POSIX d_off/d_reclen/d_namlen semantics. */
#include <pollikos/fs.h>
#define dirent pollikos_dirent
#define d_ino inode
#define d_type type
#define d_name name
#endif
