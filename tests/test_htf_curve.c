/* WSF_Htf_CurvePredict 포팅 테스트: 원본 의미 보존 확인 (계획서 §26 미래곡선 항목) */

#include "test_util.h"

#include <math.h>

#include "core/indicators/htf_curve_predict.h"

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

int main(void) {
    test_perfect_line();
    test_warmup_validity();
    test_eval_always_shifts();
    TR_TEST_SUMMARY();
}
