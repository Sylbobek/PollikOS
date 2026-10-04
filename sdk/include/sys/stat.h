#ifndef POLLIKOS_SYS_STAT_H
#define POLLIKOS_SYS_STAT_H
#include <pollikos/fs.h>
/* PollikFS has no permission bits; st_mode carries only the file type. */
#define S_IFMT 0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
struct stat {
    unsigned long st_ino;
    unsigned long st_size;
    unsigned int st_mode;
    unsigned int st_type;    /* POLLIKOS_TYPE_* */
    unsigned long st_mtime;  /* legacy PollikOS tick stamps */
    unsigned long st_ctime;
};
int stat(const char *path, struct stat *result);
int fstat(int fd, struct stat *result);
#endif
