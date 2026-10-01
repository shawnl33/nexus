/* WSF_Daily_LinRegTrendV1 포팅 테스트: 게이트, 강도식, 상태 분기 */

#include "test_util.h"

#include <math.h>

#include "core/functions/daily_linreg_trend_v1.h"

static tr_dlrt1_input_t make_in(void) {
    tr_dlrt1_input_t in;
    in.line = 110.0;
    in.slope = 2.0;
    in.reg_valid = true;
    in.r2 = 1.0;
    in.residual = 0.0;
    in.price = 112.0;
    in.min_r2 = 0.40;
    in.price_scale = 1.0;
    return in;
}

static void test_uptrend_strong(void) {
    tr_dlrt1_input_t in = make_in();
    tr_dlrt1_output_t out;
    tr_dlrt1_eval(&in, &out);
    /* 기준잔차 = max(1, 0) = 1, 최소기울기 = max(1, 0.02) = 1, |2| >= 1 통과 */
    TR_CHECK(out.valid);
    TR_CHECK(out.dir == 1);
    TR_CHECK(out.state == 2); /* 가격 >= 회귀선 */
    /* 강도 = (1*0.6 + min(1, 2*5)*0.4)*100 = 100 */
    TR_CHECK(fabs(out.strength - 100.0) < 1e-9);
}

static void test_uptrend_weak_side(void) {
    tr_dlrt1_input_t in = make_in();
    in.price = 105.0; /* 회귀선 110 아래 */
    tr_dlrt1_output_t out;
    tr_dlrt1_eval(&in, &out);
    TR_CHECK(out.valid);
    TR_CHECK(out.dir == 1);
    TR_CHECK(out.state == 1);
    TR_CHECK(fabs(out.strength - 65.0) < 1e-9); /* 100*0.65 */
}

static void test_downtrend_strong(void) {
    tr_dlrt1_input_t in = make_in();
    in.slope = -2.0;
    in.price = 100.0; /* 회귀선 110 아래 = 하락 방향과 같은 편 */
    tr_dlrt1_output_t out;
    tr_dlrt1_eval(&in, &out);
    TR_CHECK(out.valid);
    TR_CHECK(out.dir == -1);
    TR_CHECK(out.state == -2);
    TR_CHECK(fabs(out.strength - 100.0) < 1e-9);
}

static void test_gate_failures(void) {
    tr_dlrt1_input_t in = make_in();
    tr_dlrt1_output_t out;

    in.r2 = 0.30; /* 신뢰도 미달 */
    tr_dlrt1_eval(&in, &out);
    TR_CHECK(!out.valid && out.dir == 0 && out.strength == 0.0);

    in = make_in();
    in.residual = 10.0; /* 최소기울기 = max(1, 0.2) = 1, 기울기 0.5 < 1 */
    in.slope = 0.5;
    tr_dlrt1_eval(&in, &out);
    TR_CHECK(!out.valid);

    in = make_in();
    in.reg_valid = false;
    tr_dlrt1_eval(&in, &out);
    TR_CHECK(!out.valid);
}

int main(void) {
    test_uptrend_strong();
    test_uptrend_weak_side();
    test_downtrend_strong();
    test_gate_failures();
    TR_TEST_SUMMARY();
}
