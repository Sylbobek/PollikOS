#ifndef POLLIK_RTC64_H
#define POLLIK_RTC64_H
#include <stdint.h>

typedef struct {
    uint8_t second, minute, hour, day, month, year, century, status_b, status_d;
    uint8_t update_in_progress;
} Rtc64Snapshot;

/* Return one for a valid UTC timestamp, zero for malformed or unavailable RTC data. */
int rtc64_decode(const Rtc64Snapshot *snapshot, int64_t *unix_seconds);
int rtc64_read_unix_seconds(int64_t *unix_seconds);
#ifdef SELFTEST
void rtc64_selftest(void);
#endif

#endif
