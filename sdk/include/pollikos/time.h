#ifndef POLLIKOS_MONOTONIC_TIME_H
#define POLLIKOS_MONOTONIC_TIME_H
#include <stdint.h>
/* Monotonic 100 Hz PIT ticks and derived milliseconds. No calendar time. */
uint64_t __pollikos_clocks_per_second(void);
long pollikos_clock_ticks(void);
long pollikos_monotonic_ms(void);
/* UTC Unix seconds from the x86_64 CMOS RTC, or -1 with errno set. */
long pollikos_realtime_seconds(void);
long pollikos_sleep_ms(unsigned long milliseconds);
/* sleep_ms returns 0 on success or -1 with errno set. */
int sleep_ms(unsigned int milliseconds);
#endif
