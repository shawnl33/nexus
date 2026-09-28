#ifndef TR_HTF_CURVE_PREDICT_H
#define TR_HTF_CURVE_PREDICT_H

/* WSF_Htf_CurvePredict 포팅 (원본 74줄, 완전 제공).
 *
 * - (L+H)/2 최근 19개에 대한 롤링 선형회귀 + 회귀 직선의 원시 외삽.
 * - 후처리 없음: R²/ATR 감쇠를 적용하지 않는다 (원본 60줄).
 * - 방향 출력은 열거값이 아니라 가격 단위 수치: 곡선예측가격 − 곡선하 (원본 71줄).
 * - 원본의 DayIndex()%1==0은 항상 참이므로 매 평가 실행이 원본 동작 (호환 유지).
 * - 원본의 깨진 변곡 감지(64~67줄)·무의미 k 카운터는 출력 무영향으로 확인되어 생략.
 * - x좌표는 원본과 같이 누적 카운터 X(절대값)를 사용한다.
 * - 유효성: 원본은 워밍업 중 0 채움 쓰레기값을 낸다. 포팅은 값은 같은 방식으로 계산하되
 *   유효성을 별도 표시한다 (evals>=20에서 VALID).
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/model/units.h"

#define TR_HTF_SAMPLES 19

typedef struct {
    int32_t ticks;          /* 곡선예측틱수 */
    double minclose[100];   /* [0]=최신, 원본과 같은 시프트 배열·0 초기 채움 */
    int64_t x;              /* 누적 평가 카운터 (원본 X) */
    double prev_minlrl;     /* MinLRL[1] */
    bool has_prev;
    /* 출력 (원본 NumericRef 대응) */
    double low_curve;       /* 곡선하: 직전 평가의 회귀선 값 */
    double high_curve;      /* 곡선상: 현재 회귀선 값 */
    double pred_price;      /* 곡선예측가격: 원시 외삽 */
    double change;          /* 곡선예측가격_변화: |예측 − 곡선하| */
    double direction;       /* 곡선예측가격_방향: 예측 − 곡선하 (가격 단위 수치) */
    double slope;           /* MinLRS */
    double intercept;       /* MinB (절대 x 기준) */
    uint64_t evals;
} tr_htf_curve_t;

bool tr_htf_curve_init(tr_htf_curve_t *s, int32_t predict_ticks);

/* 매 평가 호출 (원본은 매 평가 시프트한다 — 같은 봉 재평가도 별도 평가로 취급됨에 주의). */
void tr_htf_curve_eval(tr_htf_curve_t *s, double high, double low);

tr_validity_t tr_htf_curve_validity(const tr_htf_curve_t *s);

#endif
