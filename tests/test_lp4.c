/* WSF_Mtf_LinRegPredictV4 포팅 테스트: R² 계수·ATR 클램프·가속도 억제·무효 기본값 */

#include "test_util.h"

#include <math.h>

#include "core/indicators/linreg_predict_v4.h"

#define LINE 1000.0

static void seed_atr(tr_lp4_t *s) {
    /* TR=10 인 확정 봉 20개로 ATR ≈ 10 시딩 */
    for (int i = 0; i < 20; i++) {
        tr_lp4_on_bar_closed(s, 105.0, 95.0, 100.0);
    }
}

static tr_lp4_input_t make_input(double slope, double r2, bool valid, bool new_bar) {
    tr_lp4_input_t in;
    in.cur_line = LINE;
    in.slope = slope;
    in.reg_valid = valid;
    in.r2 = r2;
    in.high = 105.0;
    in.low = 95.0;
    in.close = 100.0;
    in.trading_day = 100;
    in.is_new_bar = new_bar;
    in.compress_min_le30 = false;
    return in;
}

static void test_full_coef(void) {
    tr_lp4_t s;
    TR_CHECK(tr_lp4_init(&s, 5, 10, 15));
    seed_atr(&s);

    /* R²=1 → 계수 1. 기울기 3 → 클램프 한도(0.5*ATR=5) 이내. 가속도 0 */
    tr_lp4_input_t in = make_input(3.0, 1.0, true, true);
    tr_lp4_eval(&s, &in);
    TR_CHECK(fabs(s.adj_slope - 3.0) < 1e-9);
    TR_CHECK(fabs(s.accel) < 1e-9);
    /* 이동값 = 3*5 = 15 (< 1.5*10*√5 ≈ 33.5) */
    TR_CHECK(fabs(s.pred_price[0] - (LINE + 15.0)) < 1e-9);
    TR_CHECK(s.pred_dir[0] == 1);
    TR_CHECK(fabs(s.pred_price[1] - (LINE + 30.0)) < 1e-9);
    TR_CHECK(fabs(s.volatility - 10.0) < 1e-9);
    TR_CHECK(s.valid_out);
}

static void test_low_r2_flattens(void) {
    tr_lp4_t s;
    tr_lp4_init(&s, 5, 10, 15);
    seed_atr(&s);
    /* R²=0.10 ≤ 0.20 → 계수 0 → 예측 평탄화 */
    tr_lp4_input_t in = make_input(8.0, 0.10, true, true);
    tr_lp4_eval(&s, &in);
    TR_CHECK(s.adj_slope == 0.0);
    TR_CHECK(fabs(s.pred_price[0] - LINE) < 1e-9);
    TR_CHECK(s.pred_dir[0] == 0);
}

static void test_slope_clamp(void) {
    tr_lp4_t s;
    tr_lp4_init(&s, 5, 10, 15);
    seed_atr(&s);
    /* R²=0.45 → 계수 0.5. 기울기 20 → 20*0.5=10 → ±5(0.5*ATR) 클램프 */
    tr_lp4_input_t in = make_input(20.0, 0.45, true, true);
    tr_lp4_eval(&s, &in);
    TR_CHECK(fabs(s.adj_slope - 5.0) < 1e-9);
    /* 이동값 = 5*5 = 25 (< 33.5) */
    TR_CHECK(fabs(s.pred_price[0] - (LINE + 25.0)) < 1e-9);
}

static void test_move_clamp(void) {
    tr_lp4_t s;
    tr_lp4_init(&s, 60, 10, 15);
    seed_atr(&s);
    /* R²=1, 기울기 5 → adj 5 (클��프 경계). 이동값 = 5*60=300 > 1.5*10*√60 ≈ 116.2 */
    tr_lp4_input_t in = make_input(5.0, 1.0, true, true);
    tr_lp4_eval(&s, &in);
    double max_move = 10.0 * 1.5 * sqrt(60.0);
    TR_CHECK(fabs(s.pred_price[0] - (LINE + max_move)) < 1e-6);
}

static void test_same_direction_accel_suppressed(void) {
    tr_lp4_t s;
    tr_lp4_init(&s, 5, 10, 15);
    seed_atr(&s);
    /* 기울기 1,2,3,4 로 4봉: raw 가속도 = (4−1)/3 = 1 → 클램프 0.8.
       보정기울기와 같은 부호 → 가속도 0 억제 */
    for (int i = 1; i <= 4; i++) {
        tr_lp4_input_t in = make_input((double)i, 1.0, true, true);
        tr_lp4_eval(&s, &in);
    }
    TR_CHECK(s.accel == 0.0);
    /* 이동값 = 4*5 = 20 (가속도 없음) */
    TR_CHECK(fabs(s.pred_price[0] - (LINE + 20.0)) < 1e-9);
}

static void test_deceleration_kept(void) {
    tr_lp4_t s;
    tr_lp4_init(&s, 5, 10, 15);
    seed_atr(&s);
    /* 기울기 4,3,2,1 로 4봉: raw = (1−4)/3 = −1 → −0.8. 둔화 성분 → 유지 */
    for (int i = 0; i < 4; i++) {
        tr_lp4_input_t in = make_input(4.0 - i, 1.0, true, true);
        tr_lp4_eval(&s, &in);
    }
    TR_CHECK(fabs(s.accel - (-0.8)) < 1e-9);
    /* 이동값 = 1*5 + 0.5*(−0.8)*25 = 5 − 10 = −5 */
    TR_CHECK(fabs(s.pred_price[0] - (LINE - 5.0)) < 1e-9);
    TR_CHECK(s.pred_dir[0] == -1);
}

static void test_invalid_defaults(void) {
    tr_lp4_t s;
    tr_lp4_init(&s, 5, 10, 15);
    seed_atr(&s);
    tr_lp4_input_t in = make_input(7.0, 0.9, false, true);
    tr_lp4_eval(&s, &in);
    TR_CHECK(!s.valid_out);
    TR_CHECK(fabs(s.pred_price[0] - LINE) < 1e-9);
    TR_CHECK(s.pred_dir[0] == 0);
    TR_CHECK(s.adj_slope == 0.0 && s.accel == 0.0);
    TR_CHECK(fabs(s.volatility - 10.0) < 1e-9); /* 예측변동성은 항상 ATR */
}

static void test_session_boundary_guard(void) {
    tr_lp4_t s;
    tr_lp4_init(&s, 5, 10, 15);
    seed_atr(&s);
    /* 둔화 기울기 4..1, 마지막 봉의 trading_day가 [3]과 다륩면 가속도 0 */
    for (int i = 0; i < 4; i++) {
        tr_lp4_input_t in = make_input(4.0 - i, 1.0, true, true);
        in.trading_day = 100 + (i > 0); /* [3]과 현재의 일자가 다름 */
        in.compress_min_le30 = true;
        tr_lp4_eval(&s, &in);
    }
    TR_CHECK(s.accel == 0.0);
}

int main(void) {
    test_full_coef();
    test_low_r2_flattens();
    test_slope_clamp();
    test_move_clamp();
    test_same_direction_accel_suppressed();
    test_deceleration_kept();
    test_invalid_defaults();
    test_session_boundary_guard();
    TR_TEST_SUMMARY();
}
