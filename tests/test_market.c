/* 마켓 프로파일 테스트: VWAP·가중표준편차 손계산, 유효 조건, 세션 리셋, 중심단계 */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/indicators/market_profile.h"

#define CAP 4
static tr_candle_t storage[CAP];

static void make_bar(tr_candle_t *b, double h, double l, double c, int64_t v, int64_t open_us) {
    memset(b, 0, sizeof(*b));
    b->instrument_id = 1;
    b->open_time_us = open_us;
    b->high = h;
    b->low = l;
    b->close = c;
    b->volume = v;
}

static void test_vwap_and_bands(void) {
    tr_market_t s;
    TR_CHECK(tr_market_init(&s, 3, 1.0, 1.0, storage, CAP));

    tr_candle_t b;
    make_bar(&b, 12, 10, 11, 100, 1);
    tr_market_on_bar(&s, &b, true, 10.0);
    TR_CHECK(!s.valid); /* 계산봉수 1 < 2 */

    make_bar(&b, 13, 11, 12, 200, 2);
    tr_market_on_bar(&s, &b, false, 10.0);
    /* 중심 = (11*100 + 12*200)/300 = 11.6667, sd = sqrt((0.4444*100 + 0.1111*200)/300) */
    TR_CHECK(s.valid);
    TR_CHECK(fabs(s.center - 11.6666667) < 1e-6);
    double sd = sqrt((0.4444444 * 100.0 + 0.1111111 * 200.0) / 300.0);
    TR_CHECK(fabs(s.upper1 - (s.center + sd)) < 1e-6);
    TR_CHECK(fabs(s.lower1 - (s.center - sd)) < 1e-6);
    TR_CHECK(fabs(s.upper2 - (s.center + 2.0 * sd)) < 1e-6);
}

static void test_stage(void) {
    tr_market_t s;
    tr_market_init(&s, 3, 1.0, 1.0, storage, CAP);
    tr_candle_t b;
    make_bar(&b, 12, 10, 11, 100, 1);
    tr_market_on_bar(&s, &b, true, 10.0);
    make_bar(&b, 13, 11, 12, 200, 2);
    tr_market_on_bar(&s, &b, false, 10.0);
    make_bar(&b, 14, 12, 13, 300, 3);
    tr_market_on_bar(&s, &b, false, 10.0); /* 평탄회귀선 10 < 종가 */

    /* 중심 상승, 종가 13 > 중심 12.33, 종가 > 평탄회귀선 → 양의 단계 */
    TR_CHECK(s.valid && s.center_slope > 0.0);
    TR_CHECK(s.stage == 3); /* 위치강도 ≈ 66.7 >= 66 */

    /* 평탄회귀선이 종가 위면 단계 없음 */
    make_bar(&b, 14, 12, 13, 300, 4);
    tr_market_on_bar(&s, &b, false, 20.0);
    TR_CHECK(s.stage == 0);
}

static void test_zero_volume_carry(void) {
    tr_market_t s;
    tr_market_init(&s, 3, 1.0, 1.0, storage, CAP);
    tr_candle_t b;
    make_bar(&b, 12, 10, 11, 100, 1);
    tr_market_on_bar(&s, &b, true, 10.0);
    make_bar(&b, 13, 11, 12, 200, 2);
    tr_market_on_bar(&s, &b, false, 10.0);
    double center_before = s.center;
    TR_CHECK(s.valid);

    /* 거래량 0 봉: 자체는 계산에 기여하지 않고, 이전 유효 봉들로 계산은 계속 유효.
       중심은 같은 입력으로 재계산되어 변하지 않는다 */
    make_bar(&b, 15, 13, 14, 0, 3);
    tr_market_on_bar(&s, &b, false, 10.0);
    TR_CHECK(s.valid);
    TR_CHECK(s.center == center_before);
    TR_CHECK(s.stage == 0); /* 중심기울기 0 → 단계 없음 */
}

static void test_session_reset(void) {
    tr_market_t s;
    tr_market_init(&s, 3, 1.0, 1.0, storage, CAP);
    tr_candle_t b;
    make_bar(&b, 12, 10, 11, 100, 1);
    tr_market_on_bar(&s, &b, true, 10.0);
    make_bar(&b, 13, 11, 12, 200, 2);
    tr_market_on_bar(&s, &b, false, 10.0);
    TR_CHECK(s.valid);

    make_bar(&b, 14, 12, 13, 300, 3);
    tr_market_on_bar(&s, &b, true, 10.0); /* 새 세션: 봉 카운터 1 */
    TR_CHECK(!s.valid); /* 계산봉수 1 */
    TR_CHECK(s.session_bars == 1);
}

int main(void) {
    test_vwap_and_bands();
    test_stage();
    test_zero_volume_carry();
    test_session_reset();
    TR_TEST_SUMMARY();
}
