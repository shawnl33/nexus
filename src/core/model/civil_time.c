#include "core/model/civil_time.h"

/* 음수에서도 바닥 나눗셈 */
static int64_t floor_div_i64(int64_t a, int64_t b) {
    int64_t q = a / b;
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) {
        q--;
    }
    return q;
}

unsigned tr_weekday_from_days(int64_t days_since_epoch) {
    /* 1970-01-01은 목요일(4) */
    int64_t w = (days_since_epoch % 7 + 7 + 4) % 7;
    return (unsigned)w;
}

int64_t tr_days_from_civil(int year, unsigned month, unsigned day) {
    int64_t y = year;
    unsigned m = month;
    unsigned d = day;
    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);                       /* [0, 399] */
    unsigned doy = (unsigned)((153 * ((int)m + (m > 2 ? -3 : 9)) + 2) / 5) + d - 1; /* [0, 365] */
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           /* [0, 146096] */
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *year, unsigned *month, unsigned *day) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);                    /* [0, 146096] */
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* [0, 399] */
    int64_t y = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);         /* [0, 365] */
    unsigned mp = (5 * doy + 2) / 153;                              /* [0, 11] */
    *day = doy - (153 * mp + 2) / 5 + 1;                            /* [1, 31] */
    *month = (unsigned)((int)mp + (mp < 10 ? 3 : -9));                /* [1, 12] */
    *year = (int)(y + (*month <= 2));
}

bool tr_civil_from_time_us(tr_time_us_t t, int32_t utc_offset_min, tr_civil_t *out) {
    if (out == 0) {
        return false;
    }
    tr_time_us_t shifted;
    if (!tr_time_us_add(t, (int64_t)utc_offset_min * TR_US_PER_MIN, &shifted)) {
        return false;
    }
    int64_t days = floor_div_i64(shifted, TR_US_PER_DAY);
    int64_t rem = shifted - days * TR_US_PER_DAY; /* [0, US_PER_DAY) */
    int year;
    unsigned month, day;
    civil_from_days(days, &year, &month, &day);
    out->year = year;
    out->month = month;
    out->day = day;
    out->hour = (unsigned)(rem / (TR_US_PER_SEC * 3600));
    rem %= TR_US_PER_SEC * 3600;
    out->min = (unsigned)(rem / (TR_US_PER_SEC * 60));
    rem %= TR_US_PER_SEC * 60;
    out->sec = (unsigned)(rem / TR_US_PER_SEC);
    return true;
}

static bool is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

bool tr_time_us_from_civil(const tr_civil_t *c, int32_t utc_offset_min, tr_time_us_t *out) {
    if (c == 0 || out == 0) {
        return false;
    }
    static const unsigned dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (c->month < 1 || c->month > 12) {
        return false;
    }
    unsigned mdays = dim[c->month - 1] + (c->month == 2 && is_leap(c->year) ? 1u : 0u);
    if (c->day < 1 || c->day > mdays || c->hour > 23 || c->min > 59 || c->sec > 59) {
        return false;
    }
    int64_t days = tr_days_from_civil(c->year, c->month, c->day);
    int64_t us = days * TR_US_PER_DAY
               + ((int64_t)c->hour * 3600 + (int64_t)c->min * 60 + (int64_t)c->sec) * TR_US_PER_SEC
               - (int64_t)utc_offset_min * TR_US_PER_MIN;
    *out = us;
    return true;
}

void tr_local_midnight(int64_t days_since_epoch, int32_t utc_offset_min, tr_time_us_t *midnight_us) {
    *midnight_us = days_since_epoch * TR_US_PER_DAY - (int64_t)utc_offset_min * TR_US_PER_MIN;
}

void tr_local_day_and_min(tr_time_us_t t, int32_t utc_offset_min, int64_t *out_days, uint32_t *out_min) {
    tr_time_us_t shifted = t + (int64_t)utc_offset_min * TR_US_PER_MIN;
    int64_t days = floor_div_i64(shifted, TR_US_PER_DAY);
    int64_t rem = shifted - days * TR_US_PER_DAY;
    if (out_days != 0) {
        *out_days = days;
    }
    if (out_min != 0) {
        *out_min = (uint32_t)(rem / TR_US_PER_MIN);
    }
}
