/* WSF_FXMarketV1: 거래량가중 중심과 모집단 표준편차 밴드 */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/fx_market_v1.h"

static tr_fxmkt_input_t bar(int n, double px, double vol) {
    tr_fxmkt_input_t in;
    memset(&in, 0, sizeof(in));
    in.high = px;
    in.low = px;
    in.close = px;
    in.volume = vol;
    in.bar_open = (tr_time_us_t)n * 60 * 1000000;
    return in;
}

static void test_two_bar_band(void) {
    tr_fxmkt_t s;
    TR_CHECK(tr_fxmkt_init(&s, 2, 1));
    tr_fxmkt_input_t b1 = bar(1, 3, 1);
    tr_fxmkt_eval(&s, &b1);
    TR_CHECK(s.valid);
    TR_CHECK(fabs(s.center - 3.0) < 1e-9);
    TR_CHECK(fabs(s.upper - 3.0) < 1e-9);

    tr_fxmkt_input_t b2 = bar(2, 9, 1);
    tr_fxmkt_eval(&s, &b2);
    TR_CHECK(fabs(s.center - 6.0) < 1e-9);
    TR_CHECK(fabs(s.upper - 9.0) < 1e-9);
    TR_CHECK(fabs(s.lower - 3.0) < 1e-9);

    /* 같은 봉 종가를 12로 고치면 표본은 2개로 남는다 */
    b2.close = 12;
    b2.high = 12;
    b2.low = 12;
    tr_fxmkt_eval(&s, &b2);
    TR_CHECK(s.count == 2);
    TR_CHECK(fabs(s.center - 7.5) < 1e-9); /* (3+12)/2 */
}

static void test_zero_volume_invalid(void) {
    tr_fxmkt_t s;
    tr_fxmkt_init(&s, 2, 1);
    tr_fxmkt_input_t b = bar(1, 5, 0);
    tr_fxmkt_eval(&s, &b);
    TR_CHECK(!s.valid);
    TR_CHECK(s.center == 0.0);
}

int main(void) {
    test_two_bar_band();
    test_zero_volume_invalid();
    TR_TEST_SUMMARY();
}
