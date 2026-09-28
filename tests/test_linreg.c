/* 회귀 유틸·ATR 테스트 (계획서 §26: 손으로 확인 가능한 작은 자료, 평탄·단조·경계·초기 구간) */

#include "test_util.h"

#include <math.h>

#include "core/indicators/atr.h"
#include "core/indicators/linreg.h"

static void test_perfect_line(void) {
    double y[10];
    for (int i = 0; i < 10; i++) {
        y[i] = 2.0 * i + 3.0; /* y = 2x + 3 */
    }
    tr_ols_result_t r;
    TR_CHECK(tr_ols_fit(y, 10, 0.0, 5, &r));
    TR_CHECK(r.valid);
    TR_CHECK(fabs(r.slope - 2.0) < 1e-9);
    TR_CHECK(fabs(r.intercept - 3.0) < 1e-9);
    TR_CHECK(fabs(r.r2 - 1.0) < 1e-9);
    TR_CHECK(r.residual_sd < 1e-9);
    TR_CHECK(fabs(r.current - 21.0) < 1e-9); /* 2*9+3 */
}

static void test_flat_series(void) {
    double y[8];
    for (int i = 0; i < 8; i++) {
        y[i] = 5.0;
    }
    tr_ols_result_t r;
    TR_CHECK(tr_ols_fit(y, 8, 0.0, 5, &r));
    TR_CHECK(r.valid);
    TR_CHECK(fabs(r.slope) < 1e-12);
    TR_CHECK(r.r2 == 0.0); /* y 분산 0: 원본과 같이 신뢰도 0 */
    TR_CHECK(fabs(r.current - 5.0) < 1e-9);
}

static void test_hand_computed(void) {
    double y[5] = {1, 2, 2, 3, 5}; /* 평균 2.6 */
    tr_ols_result_t r;
    TR_CHECK(tr_ols_fit(y, 5, 0.0, 5, &r));
    TR_CHECK(r.valid);
    /* slope = 9.0/10 = 0.9, intercept = 2.6−0.9*2 = 0.8 */
    TR_CHECK(fabs(r.slope - 0.9) < 1e-9);
    TR_CHECK(fabs(r.intercept - 0.8) < 1e-9);
    TR_CHECK(fabs(r.current - 4.4) < 1e-9); /* 0.9*4+0.8 */
    /* r² = 81/(10*9.2) ≈ 0.88043 */
    TR_CHECK(fabs(r.r2 - 81.0 / 92.0) < 1e-6);
}

static void test_min_samples(void) {
    double y[4] = {1, 2, 3, 4};
    tr_ols_result_t r;
    TR_CHECK(tr_ols_fit(y, 4, 0.0, 5, &r));
    TR_CHECK(!r.valid); /* 표본 부족 */
}

static void test_x0_offset(void) {
    double y[6];
    for (int i = 0; i < 6; i++) {
        y[i] = 2.0 * (10 + i) + 3.0; /* x=10..15 에서 y=2x+3 */
    }
    tr_ols_result_t r;
    TR_CHECK(tr_ols_fit(y, 6, 10.0, 5, &r));
    TR_CHECK(fabs(r.slope - 2.0) < 1e-9);
    TR_CHECK(fabs(r.current - 33.0) < 1e-9); /* 2*15+3 */
}

static void test_atr_wilder(void) {
    tr_atr_t a;
    TR_CHECK(tr_atr_init(&a, 14));
    TR_CHECK(tr_atr_value(&a) == 0.0);

    /* TR=10 인 봉 14개: 시딩 후 ATR = 10 */
    for (int i = 0; i < 14; i++) {
        tr_atr_on_bar(&a, 105.0, 95.0, 100.0);
    }
    TR_CHECK(fabs(tr_atr_value(&a) - 10.0) < 1e-9);

    /* Wilder 평활: atr = (10*13 + 24)/14 = 11 */
    tr_atr_on_bar(&a, 112.0, 88.0, 100.0); /* TR = 24 */
    TR_CHECK(fabs(tr_atr_value(&a) - 11.0) < 1e-9);

    /* 진행 봉 후보는 상태를 바꾸지 않는다 */
    double before = tr_atr_value(&a);
    double cand = tr_atr_candidate(&a, 200.0, 100.0, 150.0); /* 큰 TR */
    TR_CHECK(cand > before);
    TR_CHECK(tr_atr_value(&a) == before);
}

static void test_atr_gap_tr(void) {
    tr_atr_t a;
    tr_atr_init(&a, 3);
    tr_atr_on_bar(&a, 100.0, 90.0, 95.0);  /* TR=10 (첫 봉) */
    tr_atr_on_bar(&a, 110.0, 100.0, 105.0); /* TR = max(10, 15, 5) = 15 (갭) */
    tr_atr_on_bar(&a, 106.0, 102.0, 104.0); /* TR = max(4, 1, 3) = 4 */
    /* 시딩 평균 = (10+15+4)/3 = 9.666... */
    TR_CHECK(fabs(tr_atr_value(&a) - 29.0 / 3.0) < 1e-9);
}

int main(void) {
    test_perfect_line();
    test_flat_series();
    test_hand_computed();
    test_min_samples();
    test_x0_offset();
    test_atr_wilder();
    test_atr_gap_tr();
    TR_TEST_SUMMARY();
}
