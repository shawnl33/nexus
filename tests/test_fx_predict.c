/* WSF_FXPredictV2 포팅 테스트: 세션 ATR 누적/전환, horizon 5개 예측, 가속도 억제·게이트,
 * 무효 평탄 기본값, horizon 클램프, relink */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/fx_predict_v2.h"

static tr_fxp2_input_t base_in(void) {
    tr_fxp2_input_t in;
    memset(&in, 0, sizeof(in));
    in.cur_line = 100.0;
    in.slope = 2.0;
    in.reg_valid = false;
    in.r2 = 0.0;
    in.is_new_bar = true;
    in.horizons[0] = 5;
    in.horizons[1] = 10;
    in.horizons[2] = 15;
    in.horizons[3] = 20;
    in.horizons[4] = 25;
    return in;
}

/* bar1~3 워밍업: TR 5,3,4 → 세션ATR 5,4,4. 기울기 이력은 slopes[0..2] (bar1이 최구) */
static void warm3(tr_fxp2_t *s, const double slopes[3]) {
    static const double H[3] = {105.0, 106.0, 108.0};
    static const double L[3] = {100.0, 103.0, 104.0};
    static const double C[3] = {104.0, 105.0, 107.0};
    for (int b = 0; b < 3; b++) {
        tr_fxp2_input_t in = base_in();
        in.slope = slopes[b];
        in.session_reset = (b == 0);
        in.session_bars = b + 1;
        in.high = H[b];
        in.low = L[b];
        in.close = C[b];
        tr_fxp2_eval(s, &in);
    }
}

/* bar4 입력: TR=max(4,2,2)=4 → 세션ATR (4×3+4)/4 = 4 유지 */
static tr_fxp2_input_t bar4_in(void) {
    tr_fxp2_input_t in = base_in();
    in.session_bars = 4;
    in.high = 109.0;
    in.low = 105.0;
    in.close = 108.0;
    return in;
}

/* 첫 평가부터 세션초기화=0으로 오는 경우: 이전 종가가 없어 TR=H−L만 쓴다 (has_prev_close 적응) */
static void test_no_prev_close_first_eval(void) {
    tr_fxp2_t s;
    tr_fxp2_init(&s);
    tr_fxp2_input_t in = base_in();
    in.session_reset = false;
    in.session_bars = 1;
    in.high = 105.0; in.low = 100.0; in.close = 104.0;
    tr_fxp2_eval(&s, &in);
    TR_CHECK(s.volatility == 5.0); /* TR=H−L=5, ATR=(0×0+5)/1 */
}

/* 세션 ATR 수동 계산: 재시드 → 누적 평균 (원본 36~40줄, 손계산 대조) */
static void test_session_atr_accumulation(void) {
    tr_fxp2_t s;
    tr_fxp2_init(&s);
    tr_fxp2_input_t in = base_in();

    in.session_reset = true; in.session_bars = 1;
    in.high = 105.0; in.low = 100.0; in.close = 104.0;
    tr_fxp2_eval(&s, &in);
    TR_CHECK(s.volatility == 5.0); /* 재시드: 세션ATR = H−L */

    in.session_reset = false; in.session_bars = 2;
    in.high = 106.0; in.low = 103.0; in.close = 105.0; /* TR=max(3,|106−104|,|103−104|)=3 */
    tr_fxp2_eval(&s, &in);
    TR_CHECK(s.volatility == (5.0 * 1.0 + 3.0) / 2.0); /* = 4 */

    in.session_bars = 3;
    in.high = 108.0; in.low = 104.0; in.close = 107.0; /* TR=max(4,3,1)=4 */
    tr_fxp2_eval(&s, &in);
    TR_CHECK(s.volatility == (4.0 * 2.0 + 4.0) / 3.0); /* = 4 */

    in.session_bars = 4;
    in.high = 109.0; in.low = 105.0; in.close = 108.0; /* TR=max(4,2,2)=4 */
    tr_fxp2_eval(&s, &in);
    TR_CHECK(s.volatility == (4.0 * 3.0 + 4.0) / 4.0); /* = 4 */
}

/* 세션봉수 > 14: (이전×13+TR)/14 전환 (손계산 대조) */
static void test_atr_wilder_branch(void) {
    tr_fxp2_t s;
    tr_fxp2_init(&s);
    tr_fxp2_input_t in = base_in();
    in.session_reset = true; in.session_bars = 1;
    in.high = 102.0; in.low = 98.0; in.close = 100.0;
    tr_fxp2_eval(&s, &in); /* ATR=4 */
    for (int b = 2; b <= 14; b++) {
        in.session_reset = false; in.session_bars = b;
        in.high = 102.0; in.low = 98.0; in.close = 100.0; /* TR=max(4,2,2)=4 */
        tr_fxp2_eval(&s, &in);
    }
    TR_CHECK(s.volatility == 4.0);

    in.session_bars = 15;
    in.high = 105.0; in.low = 96.0; in.close = 100.0; /* TR=max(9,5,4)=9 */
    tr_fxp2_eval(&s, &in);
    TR_CHECK(s.volatility == (4.0 * 13.0 + 9.0) / 14.0); /* = 61/14 */
}

/* horizon 5개 예측: move=min(2n, 6√max(1,n)) 클램프, 방향 부호 (ATR=4, 기울기 2, R²=1) */
static void test_prediction_five_horizons(void) {
    tr_fxp2_t s;
    tr_fxp2_init(&s);
    static const double sl[3] = {2.0, 2.0, 2.0};
    warm3(&s, sl);

    tr_fxp2_input_t in = bar4_in();
    in.slope = 2.0;
    in.reg_valid = true;
    in.r2 = 1.0;
    tr_fxp2_eval(&s, &in);

    /* 신뢰계수=clamp(1.6)=1, 보정기울기=clamp(2, 4×0.5)=2, 가속도=(2−2)/3=0 */
    TR_CHECK(s.adj_slope == 2.0);
    TR_CHECK(s.accel == 0.0);
    TR_CHECK(s.volatility == 4.0);
    static const int nh[5] = {5, 10, 15, 20, 25};
    for (int k = 0; k < 5; k++) {
        double mv = 2.0 * (double)nh[k];
        double mx = 4.0 * 1.5 * sqrt(fmax(1.0, (double)nh[k]));
        double exp_move = mv > mx ? mx : mv;
        TR_CHECK(s.pred_price[k] == 100.0 + exp_move);
        TR_CHECK(s.pred_dir[k] == 1);
    }
}

/* 둔화 가속도는 사용된다: 기울기 4,3,2,1 → 원가속도 −1, 클램프된 가속도 유지 */
static void test_accel_damping_used(void) {
    tr_fxp2_t s;
    tr_fxp2_init(&s);
    static const double sl[3] = {4.0, 3.0, 2.0};
    warm3(&s, sl);

    tr_fxp2_input_t in = bar4_in();
    in.slope = 1.0; /* hist [1,2,3,4] → 기울기[3]=4 */
    in.reg_valid = true;
    in.r2 = 1.0;
    tr_fxp2_eval(&s, &in);

    /* 원가속도 = (1−4)/3 = −1, 최대가속도 = 4×0.08로 클램프. 보정기울기=1과 부호 반대라 유지 */
    TR_CHECK(s.accel == -(4.0 * 0.08));
    TR_CHECK(s.adj_slope == 1.0);
    double exp_move = 1.0 * 5.0 + 0.5 * (-(4.0 * 0.08)) * 5.0 * 5.0;
    double mx = 4.0 * 1.5 * sqrt(5.0);
    if (exp_move > mx) {
        exp_move = mx;
    }
    TR_CHECK(s.pred_price[0] == 100.0 + exp_move);
    TR_CHECK(s.pred_dir[0] == 1);
}

/* 같은 방향 가속은 억제: 기울기 1,2,3,4 → 원가속도 +1, 보정기울기 양수 → 가속도 0 */
static void test_accel_suppressed_same_direction(void) {
    tr_fxp2_t s;
    tr_fxp2_init(&s);
    static const double sl[3] = {1.0, 2.0, 3.0};
    warm3(&s, sl);

    tr_fxp2_input_t in = bar4_in();
    in.slope = 4.0; /* hist [4,3,2,1] → 기울기[3]=1 */
    in.reg_valid = true;
    in.r2 = 1.0;
    tr_fxp2_eval(&s, &in);

    /* 원가속도 = (4−1)/3 = +1이지만 보정기울기=clamp(4,2)=2와 같은 부호 → 가속도 0 */
    TR_CHECK(s.adj_slope == 2.0);
    TR_CHECK(s.accel == 0.0);
}

/* 세션봉수<=3 게이트: 기울기 이력이 있어도 가속도 0 (원본 82~83줄) */
static void test_session_bars_accel_gate(void) {
    tr_fxp2_t s;
    tr_fxp2_init(&s);
    static const double sl[3] = {4.0, 3.0, 2.0};
    warm3(&s, sl);

    tr_fxp2_input_t in = bar4_in();
    in.slope = 1.0; /* hist [1,2,3,4] — 원가속도 −1이 생기지만 */
    in.reg_valid = true;
    in.r2 = 1.0;
    in.session_bars = 3; /* 게이트 발동 */
    tr_fxp2_eval(&s, &in);
    TR_CHECK(s.accel == 0.0);
}

/* 회귀유효입력==0: 예측가격 평탄·방향 0·보정/가속도 0, 예측변동성은 세션ATR 유지 */
static void test_invalid_reg_flat(void) {
    tr_fxp2_t s;
    tr_fxp2_init(&s);
    static const double sl[3] = {2.0, 2.0, 2.0};
    warm3(&s, sl);

    tr_fxp2_input_t in = bar4_in();
    in.reg_valid = false;
    in.cur_line = 123.0;
    tr_fxp2_eval(&s, &in);
    for (int k = 0; k < 5; k++) {
        TR_CHECK(s.pred_price[k] == 123.0);
        TR_CHECK(s.pred_dir[k] == 0);
    }
    TR_CHECK(s.adj_slope == 0.0);
    TR_CHECK(s.accel == 0.0);
    TR_CHECK(s.volatility == 4.0); /* 무효여도 세션ATR 갱신·반영 (bar4 후 ATR=4) */
}

/* 예측봉수 클램프: 음수/0 horizon → Max(0,·) → 이동 0 (원본 42~46줄) */
static void test_horizon_clamp(void) {
    tr_fxp2_t s;
    tr_fxp2_init(&s);
    static const double sl[3] = {2.0, 2.0, 2.0};
    warm3(&s, sl);

    tr_fxp2_input_t in = bar4_in();
    in.slope = 2.0;
    in.reg_valid = true;
    in.r2 = 1.0;
    in.horizons[0] = -5;
    in.horizons[1] = 0;
    in.horizons[2] = 3;
    in.horizons[3] = 100;
    in.horizons[4] = 7;
    tr_fxp2_eval(&s, &in);

    TR_CHECK(s.pred_price[0] == 100.0 && s.pred_dir[0] == 0); /* n=0 → 이동 0 */
    TR_CHECK(s.pred_price[1] == 100.0 && s.pred_dir[1] == 0);
    /* n=3: move=2×3=6 < 6√3 → 그대로 */
    TR_CHECK(s.pred_price[2] == 100.0 + 6.0 && s.pred_dir[2] == 1);
    /* n=100: move=200 > 6√100=60 → 클램프 */
    TR_CHECK(s.pred_price[3] == 100.0 + 4.0 * 1.5 * sqrt(100.0) && s.pred_dir[3] == 1);
    /* n=7: move=14 < 6√7 */
    TR_CHECK(s.pred_price[4] == 100.0 + 14.0 && s.pred_dir[4] == 1);
}

/* relink: 통째 값 복사 후 재연결하면 두 인스턴스가 독립적으로 동작한다 */
static void test_relink_independence(void) {
    tr_fxp2_t a;
    tr_fxp2_init(&a);
    static const double sl[3] = {2.0, 2.0, 2.0};
    warm3(&a, sl);
    tr_fxp2_t b = a;
    TR_CHECK(tr_fxp2_relink(&b));
    tr_fxp2_input_t in = bar4_in();
    in.slope = 9.0;
    tr_fxp2_eval(&b, &in);
    double va = 0.0, vb = 0.0;
    TR_CHECK(ylv_at(&a.slope_hist, 0, &va) && va == 2.0);
    TR_CHECK(ylv_at(&b.slope_hist, 0, &vb) && vb == 9.0);
}

int main(void) {
    test_no_prev_close_first_eval();
    test_session_atr_accumulation();
    test_atr_wilder_branch();
    test_prediction_five_horizons();
    test_accel_damping_used();
    test_accel_suppressed_same_direction();
    test_session_bars_accel_gate();
    test_invalid_reg_flat();
    test_horizon_clamp();
    test_relink_independence();
    TR_TEST_SUMMARY();
}
