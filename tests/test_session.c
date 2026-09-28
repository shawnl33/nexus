/* 세션 정책 테스트 (계획서 §26: 세션, 야간장, 휴장, 날짜 변경) */

#include "test_util.h"

#include "core/market/session.h"
#include "core/model/civil_time.h"

#define KST 540

static tr_time_us_t kst(int y, unsigned mo, unsigned d, unsigned h, unsigned mi) {
    tr_civil_t c = {y, mo, d, h, mi, 0};
    tr_time_us_t t = 0;
    tr_time_us_from_civil(&c, KST, &t);
    return t;
}

/* KRX 주식: 09:00–15:30, 평일 */
static const tr_session_policy_t KRX = {KST, 540, 930, TR_SESSION_WEEKDAYS};
/* 야간 선물 유사: 18:00–익일 05:00, 월~금 개장 */
static const tr_session_policy_t NIGHT = {KST, 1080, 300, TR_SESSION_WEEKDAYS};

static void test_krx_regular(void) {
    /* 2024-01-02 화요일 */
    tr_time_us_t open, close;
    TR_CHECK(tr_session_span(&KRX, kst(2024, 1, 2, 10, 0), &open, &close));
    TR_CHECK(open == kst(2024, 1, 2, 9, 0));
    TR_CHECK(close == kst(2024, 1, 2, 15, 30));

    TR_CHECK(tr_session_is_open(&KRX, kst(2024, 1, 2, 9, 0)));   /* 개장 시각 포함 */
    TR_CHECK(tr_session_is_open(&KRX, kst(2024, 1, 2, 15, 29)));
    TR_CHECK(!tr_session_is_open(&KRX, kst(2024, 1, 2, 15, 30))); /* 폐장 시각은 제외 */
    TR_CHECK(!tr_session_is_open(&KRX, kst(2024, 1, 2, 8, 59)));
    TR_CHECK(!tr_session_is_open(&KRX, kst(2024, 1, 2, 16, 0)));

    int64_t day;
    TR_CHECK(tr_session_trading_day(&KRX, kst(2024, 1, 2, 10, 0), &day));
    TR_CHECK(day == tr_days_from_civil(2024, 1, 2));
}

static void test_krx_weekend(void) {
    /* 2024-01-06 토요일: 시간대는 세션 안이지만 요일이 아님 */
    TR_CHECK(!tr_session_is_open(&KRX, kst(2024, 1, 6, 10, 0)));
    TR_CHECK(!tr_session_is_open(&KRX, kst(2024, 1, 7, 10, 0))); /* 일요일 */
}

static void test_overnight(void) {
    /* 2024-01-02 화요일 18:00 개장 ~ 2024-01-03 05:00 폐장 */
    tr_time_us_t open, close;
    TR_CHECK(tr_session_span(&NIGHT, kst(2024, 1, 2, 23, 0), &open, &close));
    TR_CHECK(open == kst(2024, 1, 2, 18, 0));
    TR_CHECK(close == kst(2024, 1, 3, 5, 0));

    /* 자정 이후 구간도 같은 세션·같은 트레이딩 데이 */
    TR_CHECK(tr_session_span(&NIGHT, kst(2024, 1, 3, 3, 0), &open, &close));
    TR_CHECK(open == kst(2024, 1, 2, 18, 0));
    TR_CHECK(close == kst(2024, 1, 3, 5, 0));

    int64_t day1, day2;
    TR_CHECK(tr_session_trading_day(&NIGHT, kst(2024, 1, 2, 23, 59), &day1));
    TR_CHECK(tr_session_trading_day(&NIGHT, kst(2024, 1, 3, 0, 1), &day2));
    TR_CHECK(day1 == day2); /* 날짜 변경이 트레이딩 데이를 바꾸지 않음 */

    TR_CHECK(!tr_session_is_open(&NIGHT, kst(2024, 1, 3, 5, 0)));  /* 폐장 */
    TR_CHECK(!tr_session_is_open(&NIGHT, kst(2024, 1, 3, 10, 0))); /* 주간 공백 */
    /* 금요일 야간 세션은 토요일 새벽까지 이어진다 */
    TR_CHECK(tr_session_is_open(&NIGHT, kst(2024, 1, 6, 3, 0)));
    /* 토요일 18:00 개장은 없다 */
    TR_CHECK(!tr_session_is_open(&NIGHT, kst(2024, 1, 6, 20, 0)));
}

static void test_invalid_policy(void) {
    tr_session_policy_t bad = {KST, 1440, 930, TR_SESSION_WEEKDAYS};
    TR_CHECK(!tr_session_policy_validate(&bad));
    bad = (tr_session_policy_t){KST, 540, 540, TR_SESSION_WEEKDAYS};
    TR_CHECK(!tr_session_policy_validate(&bad));
    bad = (tr_session_policy_t){KST, 540, 930, 0};
    TR_CHECK(!tr_session_policy_validate(&bad));
    bad = (tr_session_policy_t){1000, 540, 930, TR_SESSION_WEEKDAYS};
    TR_CHECK(!tr_session_policy_validate(&bad));
    TR_CHECK(!tr_session_policy_validate(0));
    TR_CHECK(!tr_session_is_open(&bad, kst(2024, 1, 2, 10, 0)));
}

int main(void) {
    test_krx_regular();
    test_krx_weekend();
    test_overnight();
    test_invalid_policy();
    TR_TEST_SUMMARY();
}
