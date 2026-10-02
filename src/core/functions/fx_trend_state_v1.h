#ifndef TR_FX_TREND_STATE_V1_H
#define TR_FX_TREND_STATE_V1_H

/* WSF_FXTrendStateV1 (원본 81줄, 무상태).
 * 일봉 회귀 기울기로 방향을 정하고, 현재가가 회귀선 위·아래인지를 강·약으로 나눈다.
 * PriceScale은 차트 값이라 호출자가 price_scale로 넘긴다 (가격과 같은 단위).
 * 무효면 방향·상태·강도·유효가 모두 0이다.
 */

typedef struct {
    double line;       /* 회귀선입력 */
    double slope;      /* 회귀기울기입력 */
    int reg_valid;     /* 회귀유효입력 */
    double r2;         /* 회귀신뢰도입력 */
    double residual;   /* 회귀잔차입력 */
    double price;      /* 현재가격입력 */
    double min_r2;     /* 최소신뢰도입력 */
    double price_scale;/* PriceScale */
} tr_fxtrend_state_input_t;

typedef struct {
    int dir;           /* 일봉추세방향 1/0/−1 */
    int state;         /* 일봉추세상태 2/1/0/−1/−2 */
    double strength;   /* 일봉추세강도 0~100 */
    int valid;         /* 일봉추세유효 */
} tr_fxtrend_state_output_t;

void tr_fxtrend_state_eval(const tr_fxtrend_state_input_t *in, tr_fxtrend_state_output_t *out);

#endif
