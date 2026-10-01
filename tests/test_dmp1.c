/* WSF_DailyMarketProfileV1 포팅 테스트: VWAP·가중표준편차, 무효 리셋 */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/daily_market_profile_v1.h"

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
    tr_dmp1_t s;
    TR_CHECK(tr_dmp1_init(&s, 3, 1.0, storage, CAP));
    tr_candle_t b;
    make_bar(&b, 12, 10, 11, 100, 1);
    tr_dmp1_on_bar(&s, &b);
    make_bar(&b, 13, 11, 12, 200, 2);
    tr_dmp1_on_bar(&s, &b);
    make_bar(&b, 14, 12, 13, 300, 3);
    tr_dmp1_on_bar(&s, &b);
    /* 중심 = (11*100 + 12*200 + 13*300)/600 = 12.3333 */
    TR_CHECK(s.valid);
    TR_CHECK(fabs(s.center - 12.3333333) < 1e-6);
    double sd = sqrt((1.7777778 * 100.0 + 0.1111111 * 200.0 + 0.4444444 * 300.0) / 600.0);
    TR_CHECK(fabs(s.upper - (s.center + sd)) < 1e-6);
    TR_CHECK(fabs(s.lower - (s.center - sd)) < 1e-6);
}

static void test_invalid_resets_outputs(void) {
    tr_dmp1_t s;
    tr_dmp1_init(&s, 3, 1.0, storage, CAP);
    tr_candle_t b;
    make_bar(&b, 12, 10, 11, 100, 1);
    tr_dmp1_on_bar(&s, &b);
    make_bar(&b, 13, 11, 12, 200, 2);
    tr_dmp1_on_bar(&s, &b);
    TR_CHECK(s.valid && s.center > 0.0);

    /* 거래량 없는 봉만 있는 새 기간: 무효, 출력은 0 리셋 (이월 없음) */
    tr_dmp1_t s2;
    tr_dmp1_init(&s2, 3, 1.0, storage, CAP);
    make_bar(&b, 15, 13, 14, 0, 3);
    tr_dmp1_on_bar(&s2, &b);
    TR_CHECK(!s2.valid);
    TR_CHECK(s2.center == 0.0 && s2.upper == 0.0 && s2.lower == 0.0);
}

static void test_period_clamp(void) {
    tr_dmp1_t s;
    TR_CHECK(tr_dmp1_init(&s, 1, 0.0, storage, CAP)); /* 기간 1→2 클램프, 배수 0→1 */
    TR_CHECK(s.period == 2);
    TR_CHECK(s.band_mult == 1.0);
}

int main(void) {
    test_vwap_and_bands();
    test_invalid_resets_outputs();
    test_period_clamp();
    TR_TEST_SUMMARY();
}
