#ifndef POLLIKOS_SYS_TIME_H
#define POLLIKOS_SYS_TIME_H
#include <time.h>
struct timeval { long tv_sec, tv_usec; };
int gettimeofday(struct timeval *tv, void *timezone);
#endif
