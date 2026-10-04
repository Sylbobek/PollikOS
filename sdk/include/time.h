#ifndef POLLIKOS_TIME_H
#define POLLIKOS_TIME_H
typedef long time_t;
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
            int tm_wday, tm_yday, tm_isdst; };
time_t time(time_t *result);
struct tm *localtime(const time_t *value);
#endif
