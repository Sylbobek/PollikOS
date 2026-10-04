#include "rtc64.h"

static int decode_number(uint8_t raw, int binary, int *value) {
    if (binary) {
        *value = raw;
        return 1;
    }
    if ((raw & 15u) > 9u || (raw >> 4) > 9u) return 0;
    *value = (raw >> 4) * 10 + (raw & 15u);
    return 1;
}

static int leap_year(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

/* Pure conversion of one captured CMOS snapshot to UTC Unix seconds. */
int rtc64_decode(const Rtc64Snapshot *s, int64_t *unix_seconds) {
    static const uint8_t month_days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (!s || !unix_seconds || s->update_in_progress || !(s->status_d & 0x80)) return 0;
    int binary = (s->status_b & 0x04) != 0;
    int second, minute, hour, day, month, year2, century, year;
    if (!decode_number(s->second, binary, &second) ||
        !decode_number(s->minute, binary, &minute) ||
        !decode_number((uint8_t)(s->hour & 0x7f), binary, &hour) ||
        !decode_number(s->day, binary, &day) ||
        !decode_number(s->month, binary, &month) ||
        !decode_number(s->year, binary, &year2) ||
        !decode_number(s->century, binary, &century)) return 0;

    if (second > 59 || minute > 59 || day < 1 || month < 1 || month > 12 ||
        year2 > 99 || (s->status_b & 0x02 && (s->hour & 0x80))) return 0;
    if (s->status_b & 0x02) {
        if (hour > 23) return 0;
    } else {
        if (hour < 1 || hour > 12) return 0;
        hour = (hour % 12) + ((s->hour & 0x80) ? 12 : 0);
    }

    if (s->century == 0) {
        /* CMOS century registers are optional. Use the conventional pivot only
         * when the register is zero; malformed nonzero values are rejected. */
        year = year2 >= 70 ? 1900 + year2 : 2000 + year2;
    } else {
        if (century < 19 || century > 99) return 0;
        year = century * 100 + year2;
    }
    if (year < 1970 || year > 9999) return 0;
    if (day > month_days[month - 1] + (month == 2 && leap_year(year))) return 0;

    int64_t days = 0;
    for (int y = 1970; y < year; ++y) days += 365 + leap_year(y);
    for (int m = 1; m < month; ++m)
        days += month_days[m - 1] + (m == 2 && leap_year(year));
    days += day - 1;
    *unix_seconds = days * 86400 + hour * 3600 + minute * 60 + second;
    return 1;
}
