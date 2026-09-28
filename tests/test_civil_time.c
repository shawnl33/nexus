/* civil time 변환 테스트 (계획서 §26: UTC·현지 시각) */

#include "test_util.h"

#include "core/model/civil_time.h"

#define KST 540

static void test_epoch(void) {
    TR_CHECK(tr_days_from_civil(1970, 1, 1) == 0);
    TR_CHECK(tr_weekday_from_days(0) == 4); /* 1970-01-01 목요일 */

    tr_civil_t c = {1970, 1, 1, 0, 0, 0};
    tr_time_us_t t = 1;
    TR_CHECK(tr_time_us_from_civil(&c, 0, &t) && t == 0);

    tr_civil_t out;
    TR_CHECK(tr_civil_from_time_us(0, 0, &out));
    TR_CHECK(out.year == 1970 && out.month == 1 && out.day == 1 &&
             out.hour == 0 && out.min == 0 && out.sec == 0);
}

static void test_known_dates(void) {
    /* 2000-01-01 00:00:00 UTC = 946684800초 */
    tr_civil_t c = {2000, 1, 1, 0, 0, 0};
    tr_time_us_t t;
    TR_CHECK(tr_time_us_from_civil(&c, 0, &t) && t == INT64_C(946684800) * TR_US_PER_SEC);
    TR_CHECK(tr_weekday_from_days(tr_days_from_civil(2000, 1, 1)) == 6); /* 토요일 */

    /* 윤일: 2024-02-29 */
    tr_civil_t leap = {2024, 2, 29, 12, 34, 56};
    tr_time_us_t lt;
    TR_CHECK(tr_time_us_from_civil(&leap, 0, &lt));
    tr_civil_t back;
    TR_CHECK(tr_civil_from_time_us(lt, 0, &back));
    TR_CHECK(back.year == 2024 && back.month == 2 && back.day == 29 &&
             back.hour == 12 && back.min == 34 && back.sec == 56);
}

static void test_pre_epoch(void) {
    tr_civil_t c = {1960, 7, 15, 8, 30, 0};
    tr_time_us_t t;
    TR_CHECK(tr_time_us_from_civil(&c, 0, &t));
    TR_CHECK(t < 0);
    tr_civil_t back;
    TR_CHECK(tr_civil_from_time_us(t, 0, &back));
    TR_CHECK(back.year == 1960 && back.month == 7 && back.day == 15 &&
             back.hour == 8 && back.min == 30 && back.sec == 0);
}

static void test_offset(void) {
    /* 1970-01-01 09:00 KST = epoch 0 */
    tr_civil_t c = {1970, 1, 1, 9, 0, 0};
    tr_time_us_t t;
    TR_CHECK(tr_time_us_from_civil(&c, KST, &t) && t == 0);

    tr_civil_t out;
    TR_CHECK(tr_civil_from_time_us(0, KST, &out));
    TR_CHECK(out.hour == 9 && out.min == 0);

    /* UTC 23:30은 KST로 다음 날 08:30 */
    tr_civil_t u = {2024, 1, 2, 23, 30, 0};
    tr_time_us_t ut;
    TR_CHECK(tr_time_us_from_civil(&u, 0, &ut));
    tr_civil_t k;
    TR_CHECK(tr_civil_from_time_us(ut, KST, &k));
    TR_CHECK(k.day == 3 && k.hour == 8 && k.min == 30);
}

static void test_invalid(void) {
    tr_time_us_t t;
    tr_civil_t bad_month = {2024, 13, 1, 0, 0, 0};
    TR_CHECK(!tr_time_us_from_civil(&bad_month, 0, &t));
    tr_civil_t bad_day = {2024, 1, 32, 0, 0, 0};
    TR_CHECK(!tr_time_us_from_civil(&bad_day, 0, &t));
    tr_civil_t non_leap = {2023, 2, 29, 0, 0, 0};
    TR_CHECK(!tr_time_us_from_civil(&non_leap, 0, &t));
    tr_civil_t leap_ok = {2024, 2, 29, 0, 0, 0};
    TR_CHECK(tr_time_us_from_civil(&leap_ok, 0, &t));
}

static void test_local_day_and_min(void) {
    tr_civil_t u = {2024, 1, 2, 23, 30, 0};
    tr_time_us_t ut;
    TR_CHECK(tr_time_us_from_civil(&u, 0, &ut));
    int64_t days;
    uint32_t min;
    tr_local_day_and_min(ut, KST, &days, &min);
    TR_CHECK(days == tr_days_from_civil(2024, 1, 3));
    TR_CHECK(min == 8 * 60 + 30);

    tr_time_us_t midnight;
    tr_local_midnight(days, KST, &midnight);
    TR_CHECK(ut - midnight == (int64_t)(8 * 60 + 30) * TR_US_PER_MIN);
}

int main(void) {
    test_epoch();
    test_known_dates();
    test_pre_epoch();
    test_offset();
    test_invalid();
    test_local_day_and_min();
    TR_TEST_SUMMARY();
}
