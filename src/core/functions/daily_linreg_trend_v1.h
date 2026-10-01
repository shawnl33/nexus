#ifndef TR_DAILY_LINREG_TREND_V1_H
#define TR_DAILY_LINREG_TREND_V1_H

/* WSF_Daily_LinRegTrendV1 포팅 (원본 81줄, 완전 제공, 무상태).
 *
 * 일봉 선형회귀 결과와 현재 가격으로 추세 방향·상태·강도를 판정한다.
 * - 기준잔차 = Max(PriceScale, 회귀잔차), 최소기울기 = Max(PriceScale, 기준잔차×0.02)
 * - 게이트: 회귀유효 && 신뢰도 >= 최소신뢰도 && |기울기| >= 최소기울기
 * - 강도 = (신뢰강도×0.60 + 기울기강도×0.40)×100, 기울기강도 = Min(1, |기울기|/기준잔차×5)
 * - 가격이 회귀선과 반대편이면 상태 ±1 + 강도×0.65, 같은 편이면 상태 ±2
 */

#include <stdbool.h>

typedef struct {
    double line;        /* 회귀선입력 (1봉 투영값) */
    double slope;       /* 회귀기울기입력 */
    bool reg_valid;     /* 회귀유효입력 == 1 */
    double r2;          /* 회귀신뢰도입력 */
    double residual;    /* 회귀잔차입력 */
    double price;       /* 현재가격입력 (C) */
    double min_r2;      /* 최소신뢰도입력 */
    double price_scale;
} tr_dlrt1_input_t;

typedef struct {
    int dir;            /* 일봉추세방향: 1/0/−1 */
    int state;          /* 일봉추세상태: −2..+2 */
    double strength;    /* 일봉추세강도: 0~100 */
    bool valid;         /* 일봉추세유효 */
} tr_dlrt1_output_t;

void tr_dlrt1_eval(const tr_dlrt1_input_t *in, tr_dlrt1_output_t *out);

#endif
