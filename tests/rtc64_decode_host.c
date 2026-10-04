#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "rtc64.h"

static unsigned passed;

#define RTC(sec,min,hour,day,month,year,century,status_b) \
    (Rtc64Snapshot){sec,min,hour,day,month,year,century,status_b,0x80,0}

static int valid(const char *name, Rtc64Snapshot snapshot, int64_t expected) {
    int64_t actual = INT64_C(-777);
    if (!rtc64_decode(&snapshot, &actual) || actual != expected) {
        fprintf(stderr, "FAIL: %s: actual=%lld expected=%lld\n", name,
                (long long)actual, (long long)expected);
        return 0;
    }
    ++passed;
    printf("PASS: %s\n", name);
    return 1;
}

static int invalid(const char *name, Rtc64Snapshot snapshot) {
    int64_t actual = INT64_C(-777);
    if (rtc64_decode(&snapshot, &actual) || actual != INT64_C(-777)) {
        fprintf(stderr, "FAIL: %s: unexpectedly decoded as %lld\n", name,
                (long long)actual);
        return 0;
    }
    ++passed;
    printf("PASS: %s\n", name);
    return 1;
}

static int boundary_pair(void) {
    Rtc64Snapshot before = RTC(0x59,0x59,0x23,0x31,0x12,0x23,0x20,0x02);
    Rtc64Snapshot after = RTC(0,0,0,0x01,0x01,0x24,0x20,0x02);
    int64_t before_seconds = -1, after_seconds = -1;
    if (!rtc64_decode(&before, &before_seconds) ||
        !rtc64_decode(&after, &after_seconds) ||
        after_seconds - before_seconds != 1) {
        fprintf(stderr, "FAIL: year boundary interval: before=%lld after=%lld\n",
                (long long)before_seconds, (long long)after_seconds);
        return 0;
    }
    ++passed;
    printf("PASS: year boundary is one second across 2023-12-31/2024-01-01\n");
    return 1;
}

int main(void) {
    int ok = 1;
    ok &= valid("binary 24-hour with century",
                RTC(0,0,0,1,1,24,20,0x06), INT64_C(1704067200));
    ok &= valid("BCD 24-hour with century",
                RTC(0x59,0x58,0x13,0x01,0x01,0x24,0x20,0x02),
                INT64_C(1704117539));
    ok &= valid("BCD 12-hour PM bit",
                RTC(0,0,0x92,0x01,0x01,0x24,0x20,0x00),
                INT64_C(1704110400));
    ok &= valid("BCD 12-hour midnight",
                RTC(0,0,0x12,0x01,0x01,0x24,0x20,0x00),
                INT64_C(1704067200));
    ok &= valid("century absent uses 1970 pivot",
                RTC(0,0,0,1,1,0x70,0,0x02), 0);
    ok &= valid("year boundary 2023-12-31 to 2024-01-01",
                RTC(0x59,0x59,0x23,0x31,0x12,0x23,0x20,0x02),
                INT64_C(1704067199));
    ok &= boundary_pair();
    ok &= valid("leap day 2024-02-29",
                RTC(0,0,0,0x29,0x02,0x24,0x20,0x02),
                INT64_C(1709164800));
    ok &= valid("day after leap day 2024-03-01",
                RTC(0,0,0,0x01,0x03,0x24,0x20,0x02),
                INT64_C(1709251200));

    ok &= invalid("month zero", RTC(0,0,0,1,0,24,20,0x06));
    ok &= invalid("month thirteen", RTC(0,0,0,1,13,24,20,0x06));
    ok &= invalid("day 32", RTC(0,0,0,32,1,24,20,0x06));
    ok &= invalid("hour 25", RTC(0,0,25,1,1,24,20,0x06));
    Rtc64Snapshot uip = RTC(0,0,0,1,1,24,20,0x06);
    uip.update_in_progress = 1;
    ok &= invalid("update in progress", uip);
    if (!ok) return 1;
    printf("[RTC64_HOST] PASS: %u UTC decoding vectors\n", passed);
    return 0;
}
