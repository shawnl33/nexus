#ifndef TR_LINREG_H
#define TR_LINREG_H

/* 최소자승(OLS) 선형회귀 유틸리티.
 *
 * WSF_Mtf_LinRegV3와 WSF_Htf_CurvePredict가 공유하는 계산부다.
 * 두 지표 인스턴스의 이력은 합치지 않고 계산 함수만 공유한다 (계획서 §10.3).
 *
 * - x는 x0, x0+1, ..., x0+n-1 (연속 정수). V3는 x0=0, Htf는 절대 카운터 X.
 * - R²는 피어슨 상관 제곱 형태. y 분산이 0이면 원본과 같이 0으로 둔다.
 * - 잔차 표준편차: 원본 관례 SSE = SST*(1-R²), sd = sqrt(SSE/(n-2)).
 */

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    double slope;       /* LRS (봉당 가격) */
    double intercept;   /* B (x 기준) */
    double r2;          /* [0,1]. 결정계수. 예측 성공 확률이 아니다 */
    double residual_sd; /* sqrt(SSE/(n-2)). n<=2이면 0 */
    double current;     /* 최신 x 위치의 적합값 = slope*(x0+n-1)+intercept */
    bool valid;         /* n >= min_samples */
} tr_ols_result_t;

/* y_oldest_first: 가장 오래된 값이 [0]. min_samples 미만이면 valid=false, 나머지는 계산 가능 범위만 채운다. */
bool tr_ols_fit(const double *y_oldest_first, size_t n, double x0, size_t min_samples, tr_ols_result_t *out);

#endif
