/* WSF_FXTrendStateV1: 기울기·잔차·현재가 위치로 일봉 추세 방향/상태/강도 */

#include "test_util.h"

#include <math.h>

#include "core/functions/fx_trend_state_v1.h"

static void test_strong_up(void) {
    /* 기준잔차=max(1,10)=10, 최소기울기=max(1,0.2)=0.2
     * 비율=1/10, 기울기강도=0.5, 신뢰강도=0.8
     * 강도=(0.48+0.20)*100=68, 가격이 회귀선 위 → 상태 2 */
    tr_fxtrend_state_input_t in = {
        .line = 100, .slope = 1, .reg_valid = 1, .r2 = 0.8, .residual = 10,
        .price = 110, .min_r2 = 0.4, .price_scale = 1,
    };
    tr_fxtrend_state_output_t o;
    tr_fxtrend_state_eval(&in, &o);
    TR_CHECK(o.valid == 1);
    TR_CHECK(o.dir == 1);
    TR_CHECK(o.state == 2);
    TR_CHECK(fabs(o.strength - 68.0) < 1e-9);
}

static void test_weak_up_scales_strength(void) {
    tr_fxtrend_state_input_t in = {
        .line = 100, .slope = 1, .reg_valid = 1, .r2 = 0.8, .residual = 10,
        .price = 90, .min_r2 = 0.4, .price_scale = 1,
    };
    tr_fxtrend_state_output_t o;
    tr_fxtrend_state_eval(&in, &o);
    TR_CHECK(o.dir == 1);
    TR_CHECK(o.state == 1);
    TR_CHECK(fabs(o.strength - 68.0 * 0.65) < 1e-9);
}

static void test_low_r2_is_flat(void) {
    tr_fxtrend_state_input_t in = {
        .line = 100, .slope = 5, .reg_valid = 1, .r2 = 0.2, .residual = 1,
        .price = 100, .min_r2 = 0.4, .price_scale = 1,
    };
    tr_fxtrend_state_output_t o;
    tr_fxtrend_state_eval(&in, &o);
    TR_CHECK(o.valid == 0 && o.dir == 0 && o.state == 0 && o.strength == 0.0);
}

int main(void) {
    test_strong_up();
    test_weak_up_scales_strength();
    test_low_r2_is_flat();
    TR_TEST_SUMMARY();
}
