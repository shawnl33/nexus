/* WSF_Htf_CurvePredict 포팅 테스트: 원본 의미 보존 확인 (계획서 §26 미래곡선 항목) */

#include "test_util.h"

#include <math.h>

#include "core/functions/htf_curve_predict.h"

/* 완전 직선: i번째 평가(1부터)의 중간값 = 98 + 2i → LRS=2, B=98 */
static void test_perfect_line(void) {
    tr_htf_curve_t s;
    TR_CHECK(tr_htf_curve_init(&s, 10));

    for (int i = 1; i <= 25; i++) {
        double mid = 98.0 + 2.0 * i;
        tr_htf_curve_eval(&s, mid + 1.0, mid - 1.0); /* (H+L)/2 = mid */
    }

    TR_CHECK(fabs(s.slope - 2.0) < 1e-9);
    /* X=25: 곡선상 = 2*25+98 = 148, 곡선하 = 146 */
    TR_CHECK(fabs(s.high_curve - 148.0) < 1e-9);
    TR_CHECK(fabs(s.low_curve - 146.0) < 1e-9);
    /* 예측 = 2*(25+10)+98 = 168 */
    TR_CHECK(fabs(s.pred_price - 168.0) < 1e-9);
    /* 방향 = 예측 − 곡선하 = 168−146 = 22 (가격 단위 수치, 열거 아님) */
    TR_CHECK(fabs(s.direction - 22.0) < 1e-9);
    TR_CHECK(fabs(s.change - 22.0) < 1e-9);
    TR_CHECK(tr_htf_curve_validity(&s) == TR_VALIDITY_VALID);
}

static void test_warmup_validity(void) {
    tr_htf_curve_t s;
    tr_htf_curve_init(&s, 10);
    for (int i = 1; i <= 19; i++) {
        double mid = 50.0 + i;
        tr_htf_curve_eval(&s, mid + 0.5, mid - 0.5);
    }
    TR_CHECK(s.evals == 19);
    TR_CHECK(tr_htf_curve_validity(&s) == TR_VALIDITY_MISSING); /* [1] 의존 출력 미성숙 */
    double mid = 50.0 + 20;
    tr_htf_curve_eval(&s, mid + 0.5, mid - 0.5);
    TR_CHECK(tr_htf_curve_validity(&s) == TR_VALIDITY_VALID);
}

static void test_eval_always_shifts(void) {
    /* 원본은 매 평가 시프트한다(항상 참 조건). 같은 값을 20번 넣으면 회귀는 평탄 */
    tr_htf_curve_t s;
    tr_htf_curve_init(&s, 5);
    for (int i = 0; i < 20; i++) {
        tr_htf_curve_eval(&s, 101.0, 99.0); /* 중간값 100 */
    }
    TR_CHECK(fabs(s.slope) < 1e-9);
    TR_CHECK(fabs(s.high_curve - 100.0) < 1e-9);
    /* 20번째 평가: 19번째 평가의 표본도 전부 100이므로 곡선하=100 → 방향 0 */
    TR_CHECK(fabs(s.direction) < 1e-9);
}

static void test_warmup_zero_fill_outputs(void) {
    /* 워밍업(count<19) 구간은 원본의 0 채움 배열을 읽어 계산한다 — yl_var 링은
     * 빈 슬롯을 주지 않으므로 없는 인덱스를 0으로 읽어 같은 값을 재현해야 한다.
     * 기대값은 전환 전 현행 코드로 기록한 실측치 (yl_var 전환 후 비트 동일 확인) */
    tr_htf_curve_t s;
    tr_htf_curve_init(&s, 7);
    for (int i = 1; i <= 19; i++) {
        double mid = 100.0 + 0.5 * i + (i % 3) * 0.25;
        double spread = 1.0 + (i % 2) * 0.5;
        tr_htf_curve_eval(&s, mid + spread, mid - spread);
        if (i == 1) {
            TR_CHECK(s.slope == 1.5907894736842105);
            TR_CHECK(s.intercept == 18.028947368421054);
            TR_CHECK(s.high_curve == 19.619736842105265);
            TR_CHECK(s.low_curve == 0.0); /* 첫 평가는 직전 회귀선 없음 */
            TR_CHECK(s.pred_price == 30.755263157894738);
            TR_CHECK(s.direction == 30.755263157894738);
            TR_CHECK(s.change == 30.755263157894738);
        }
        if (i == 2) {
            TR_CHECK(s.slope == 3.0166666666666666);
            TR_CHECK(s.intercept == 31.761403508771931);
            TR_CHECK(s.high_curve == 37.794736842105266);
            TR_CHECK(s.low_curve == 19.619736842105265);
            TR_CHECK(s.pred_price == 58.911403508771926);
            TR_CHECK(s.direction == 39.291666666666657);
        }
        if (i == 5) {
            TR_CHECK(s.slope == 6.2600877192982454);
            TR_CHECK(s.high_curve == 83.130263157894731);
            TR_CHECK(s.low_curve == 69.482894736842098);
            TR_CHECK(s.pred_price == 126.95087719298246);
            TR_CHECK(s.direction == 57.467982456140362);
        }
        if (i == 18) {
            TR_CHECK(s.slope == 2.0802631578947368);
            TR_CHECK(s.high_curve == 118.19605263157897);
            TR_CHECK(s.low_curve == 125.09078947368423);
            TR_CHECK(s.pred_price == 132.7578947368421);
            TR_CHECK(s.direction == 7.6671052631578789);
        }
        if (i == 19) {
            TR_CHECK(s.slope == 0.49736842105263157);
            TR_CHECK(s.high_curve == 109.72631578947369);
            TR_CHECK(s.low_curve == 118.19605263157897);
            TR_CHECK(s.pred_price == 113.20789473684211);
            TR_CHECK(s.direction == -4.9881578947368581);
            TR_CHECK(s.change == 4.9881578947368581);
        }
    }
}

int main(void) {
    test_perfect_line();
    test_warmup_validity();
    test_eval_always_shifts();
    test_warmup_zero_fill_outputs();
    TR_TEST_SUMMARY();
}
