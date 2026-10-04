#include "rtc64.h"
#include "memory.h"
#include "../../hal.h"

static uint8_t cmos_read(uint8_t reg) {
    hal_port_write8(0x70, (uint8_t)(reg & 0x7f));
    return hal_port_read8(0x71);
}

static int read_snapshot(Rtc64Snapshot *s) {
    s->update_in_progress = (cmos_read(0x0a) & 0x80) != 0;
    if (s->update_in_progress) return 0;
    s->second = cmos_read(0x00);
    s->minute = cmos_read(0x02);
    s->hour = cmos_read(0x04);
    s->day = cmos_read(0x07);
    s->month = cmos_read(0x08);
    s->year = cmos_read(0x09);
    s->century = cmos_read(0x32);
    s->status_b = cmos_read(0x0b);
    s->status_d = cmos_read(0x0d);
    s->update_in_progress = (cmos_read(0x0a) & 0x80) != 0;
    return !s->update_in_progress;
}

int rtc64_read_unix_seconds(int64_t *unix_seconds) {
    if (!unix_seconds) return 0;
    unsigned long flags = hal_irq_save_disable();
    int result = 0;
    for (unsigned tries = 0; tries < 100; ++tries) {
        Rtc64Snapshot a, b;
        if (!read_snapshot(&a) || !read_snapshot(&b)) continue;
        const uint8_t *left = (const uint8_t *)&a;
        const uint8_t *right = (const uint8_t *)&b;
        unsigned i;
        for (i = 0; i < sizeof(a); ++i) if (left[i] != right[i]) break;
        if (i != sizeof(a)) continue;
        result = rtc64_decode(&a, unix_seconds);
        break;
    }
    hal_irq_restore(flags);
    return result;
}

#ifdef SELFTEST
static int fixture(uint8_t second, uint8_t minute, uint8_t hour,
                   uint8_t day, uint8_t month, uint8_t year, uint8_t century,
                   uint8_t status_b, int64_t expected) {
    Rtc64Snapshot s = {second, minute, hour, day, month, year, century, status_b, 0x80, 0};
    int64_t actual = -1;
    return rtc64_decode(&s, &actual) && actual == expected;
}
static int invalid_fixture(Rtc64Snapshot s) {
    int64_t actual = 0;
    return !rtc64_decode(&s, &actual);
}
void rtc64_selftest(void) {
    Rtc64Snapshot bad = {0x6a,0,0,1,1,0,0,0x06,0x80,0};
    memory_require(fixture(0x56,0x34,0x12,0x29,0x02,0x00,0x20,0x02,951827696),
                   "RTC BCD leap day and century conversion");
    memory_require(fixture(0x59,0x59,0x23,0x31,0x12,0x99,0x19,0x02,946684799),
                   "RTC 19xx century conversion");
    memory_require(fixture(0x00,0x00,0x00,0x01,0x01,24,20,0x06,1704067200),
                   "RTC binary mode with century register");
    memory_require(fixture(0x00,0x00,0x00,0x01,0x01,0x70,0x00,0x02,0),
                   "RTC missing century register fallback");
    memory_require(fixture(0x00,0x00,0x92,0x01,0x01,0x24,0x20,0x00,1704110400),
                   "RTC 12-hour PM conversion");
    memory_require(fixture(0x00,0x00,0x12,0x01,0x01,0x24,0x20,0x00,1704067200),
                   "RTC 12-hour midnight conversion");
    memory_require(invalid_fixture(bad), "RTC invalid BCD digit rejected");
    bad = (Rtc64Snapshot){0,0,24,1,1,24,20,0x06,0x80,0};
    memory_require(invalid_fixture(bad), "RTC invalid 24-hour value rejected");
    bad = (Rtc64Snapshot){0,0,0,29,2,23,20,0x06,0x80,0};
    memory_require(invalid_fixture(bad), "RTC invalid leap day rejected");
    bad = (Rtc64Snapshot){0,0,1,1,1,24,0x1a,0x02,0x80,0};
    memory_require(invalid_fixture(bad), "RTC malformed century rejected");
    bad = (Rtc64Snapshot){0,0,1,1,1,24,20,0x02,0x00,0};
    memory_require(invalid_fixture(bad), "RTC invalid battery status rejected");
    memory_log("[RTC64] PASS: BCD/binary, 12/24-hour, century, range and invalid-value vectors\n");
}
#endif
