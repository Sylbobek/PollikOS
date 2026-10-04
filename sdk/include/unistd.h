#ifndef POLLIKOS_UNISTD_H
#define POLLIKOS_UNISTD_H
#include <stddef.h>
#include <pollikos/types.h>
#include <pollikos/fs.h>
#include <pollikos/process.h>
#include <pollikos/time.h>
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
/* PollikFS implements no permission bits: R_OK means the path exists and is
 * readable (all files are), W_OK means it exists and is a regular file, X_OK
 * means it exists as a regular file. F_OK is a pure existence check. */
int access(const char *path, int mode);
int execvp(const char *file, char *const argv[]);
pid_t getpgid(pid_t pid);
pid_t getpgrp(void);
int setpgid(pid_t pid, pid_t pgid);
/* Descriptor plumbing for shell pipelines and redirection. */
int pipe(int fds[2]);
int dup(int fd);
int dup2(int oldfd, int newfd);
#endif
