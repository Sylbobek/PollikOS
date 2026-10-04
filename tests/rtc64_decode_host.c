#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "rtc64.h"
#include "../kernel/hal.h"

static unsigned passed;
static unsigned char cmos[128], cmos_index;
static unsigned cmos_uip_reads;
static int cmos_uip_once;

void hal_port_write8(unsigned short port, unsigned char value) {
    if (port == 0x70) cmos_index = (unsigned char)(value & 0x7f);
}

unsigned char hal_port_read8(unsigned short port) {
    if (port != 0x71) return 0xff;
    if (cmos_index == 0x0a) {
        ++cmos_uip_reads;
        if (cmos_uip_once) {
            cmos_uip_once = 0;
            return 0x80;
        }
        return 0;
    }
    return cmos[cmos_index];
}

unsigned long hal_irq_save_disable(void) { return 0; }
void hal_irq_restore(unsigned long flags) { (void)flags; }

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

static int rtc_read_retries_after_uip(void) {
    memset(cmos, 0, sizeof(cmos));
    cmos[0x00] = 0x00;
    cmos[0x02] = 0x00;
    cmos[0x04] = 0x00;
    cmos[0x07] = 0x01;
    cmos[0x08] = 0x01;
    cmos[0x09] = 0x24;
    cmos[0x32] = 0x20;
    cmos[0x0b] = 0x02;
    cmos[0x0d] = 0x80;
    cmos_index = 0;
    cmos_uip_reads = 0;
    cmos_uip_once = 1;
    int64_t actual = -1;
    if (!rtc64_read_unix_seconds(&actual) || actual != INT64_C(1704067200) ||
        cmos_uip_reads != 5) {
        fprintf(stderr, "FAIL: RTC read UIP retry: actual=%lld UIP reads=%u\n",
                (long long)actual, cmos_uip_reads);
        return 0;
    }
    ++passed;
    printf("PASS: real RTC read retries after mocked update-in-progress\n");
    return 1;
}

int main(void) {
    int ok = 1;
    ok &= valid("binary 24-hour with century register 0x20 (2024)",
                RTC(0,0,0,1,1,24,20,0x06), INT64_C(1704067200));
    ok &= valid("BCD 24-hour with century",
                RTC(0x59,0x58,0x13,0x01,0x01,0x24,0x20,0x02),
                INT64_C(1704117539));
    ok &= valid("12:00 PM decodes as 12:00",
                RTC(0,0,0x92,0x01,0x01,0x24,0x20,0x00),
                INT64_C(1704110400));
    ok &= valid("12:00 AM decodes as 00:00",
                RTC(0,0,0x12,0x01,0x01,0x24,0x20,0x00),
                INT64_C(1704067200));
    ok &= valid("1:00 PM decodes as 13:00",
                RTC(0,0,0x81,0x01,0x01,0x24,0x20,0x00),
                INT64_C(1704114000));
    ok &= valid("century register 0x19 decodes 1999",
                RTC(0x59,0x59,0x23,0x31,0x12,0x99,0x19,0x02),
                INT64_C(946684799));
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
    ok &= invalid("February 30", RTC(0,0,0,0x30,0x02,0x24,0x20,0x02));
    ok &= invalid("April 31", RTC(0,0,0,0x31,0x04,0x24,0x20,0x02));
    ok &= invalid("February 29 in non-leap year",
                  RTC(0,0,0,0x29,0x02,0x23,0x20,0x02));
    Rtc64Snapshot uip = RTC(0,0,0,1,1,24,20,0x06);
    uip.update_in_progress = 1;
    ok &= invalid("update in progress", uip);
    ok &= rtc_read_retries_after_uip();
    if (!ok) return 1;
    printf("[RTC64_HOST] PASS: %u UTC decoding vectors\n", passed);
    return 0;
}
