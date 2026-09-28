/* 가격·시간·단위 경계 테스트 (계획서 §26: 스케일, 경계값·overflow, 값 부재와 0 구분) */

#include "test_util.h"

#include <limits.h>
#include <stdint.h>

#include "core/model/time_us.h"
#include "core/model/units.h"

static void test_scale_validity(void) {
    TR_CHECK(tr_scale_is_valid(1));
    TR_CHECK(tr_scale_is_valid(10));
    TR_CHECK(tr_scale_is_valid(1000));
    TR_CHECK(!tr_scale_is_valid(0));
    TR_CHECK(!tr_scale_is_valid(-1));
    TR_CHECK(!tr_scale_is_valid(-100));
    TR_CHECK(!tr_scale_is_valid(3));
    TR_CHECK(!tr_scale_is_valid(25));
    TR_CHECK(!tr_scale_is_valid(101));
}

static void test_price_conversion(void) {
    double out = 0.0;
    TR_CHECK(tr_price_to_double_checked(123456, 1000, &out));
    TR_CHECK(out > 123.45 && out < 123.47);

    /* 값의 부재와 0은 구분한다: raw 0은 유효한 가격 0.0이다 */
    TR_CHECK(tr_price_to_double_checked(0, 1000, &out));
    TR_CHECK(out == 0.0);

    /* 잘못된 스케일은 오류 */
    TR_CHECK(!tr_price_to_double_checked(100, 0, &out));
    TR_CHECK(!tr_price_to_double_checked(100, 3, &out));
    TR_CHECK(!tr_price_to_double_checked(100, 1000, 0));

    /* 큰 값도 변환 가능해야 한다 */
    TR_CHECK(tr_price_to_double_checked(INT64_MAX, 1, &out));
}

static void test_time_add_overflow(void) {
    tr_time_us_t out;
    /* 정상 */
    TR_CHECK(tr_time_us_add(1000, 500, &out) && out == 1500);
    TR_CHECK(tr_time_us_add(1000, -500, &out) && out == 500);

    /* 경계값: 정확히 최대/최소까지는 성공 */
    TR_CHECK(tr_time_us_add(INT64_MAX - 1, 1, &out) && out == INT64_MAX);
    TR_CHECK(tr_time_us_add(INT64_MIN + 1, -1, &out) && out == INT64_MIN);

    /* overflow/underflow는 오류 */
    TR_CHECK(!tr_time_us_add(INT64_MAX - 1, 2, &out));
    TR_CHECK(!tr_time_us_add(INT64_MIN + 1, -2, &out));
    TR_CHECK(!tr_time_us_add(INT64_MAX, 1, &out));
    TR_CHECK(!tr_time_us_add(INT64_MIN, -1, &out));

    TR_CHECK(!tr_time_us_add(0, 1, 0));
}

int main(void) {
    test_scale_validity();
    test_price_conversion();
    test_time_add_overflow();
    TR_TEST_SUMMARY();
}
