#include <time.h>
#include <sys/time.h>
/* The CMOS RTC has one-second resolution; no sub-second source is exposed. */
#include <pollikos/syscall.h>
#include <pollikos/time.h>
#include <errno.h>

long pollikos_realtime_seconds(void) {
    int64_t result = __pollikos_syscall0(USER_CLOCK_REALTIME);
    return result < 0 ? __pollikos_fail(result) : (long)result;
}
time_t time(time_t *result) {
    long now = pollikos_realtime_seconds();
    if (now < 0) return (time_t)-1;
    if (result) *result = (time_t)now;
    return (time_t)now;
}
struct tm *localtime(const time_t *value) {
    static struct tm result;
    static const unsigned char month_days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (!value || *value < 0 || *value > 253402300799L) return 0;
    long seconds = *value;
    long days = seconds / 86400;
    result.tm_sec = (int)(seconds % 60);
    result.tm_min = (int)((seconds / 60) % 60);
    result.tm_hour = (int)((seconds / 3600) % 24);
    result.tm_wday = (int)((days + 4) % 7);
    result.tm_isdst = 0;
    int year = 1970;
    while (days >= 365 + (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
        long year_days = 365 + (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        days -= year_days;
        ++year;
    }
    result.tm_year = year - 1900;
    result.tm_yday = (int)days;
    int month = 0;
    while (month < 11) {
        int length = month_days[month] + (month == 1 &&
            year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        if (days < length) break;
        days -= length;
        ++month;
    }
    result.tm_mon = month;
    result.tm_mday = (int)days + 1;
    return &result;
}
int gettimeofday(struct timeval *tv, void *timezone) {
    (void)timezone;
    if (tv) {
        long now = pollikos_realtime_seconds();
        if (now < 0) return -1;
        tv->tv_sec = now;
        tv->tv_usec = 0;
    }
    return 0;
}
