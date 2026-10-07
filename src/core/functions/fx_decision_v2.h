#ifndef TR_FX_DECISION_V2_H
#define TR_FX_DECISION_V2_H

/* WSF_FXDecisionV2_CO (원본 96줄). 상태 없는 판정.
 * 매물대기준모드 0=1차 밴드 안쪽 포함, 1=종가와 중심, 2=1차 밴드 이탈.
 * 통합상태 100/50/0/-50/-100. 미래방향이 0이면 0.
 */

typedef struct {
    double future_dir;
    int state5;
    int profile_valid;
    double center;
    double close;
    int mode;
    double adx, plus_di, minus_di;
    int adx_valid;
    double adx_trend;
    double adx_strong;
} tr_fxdec_input_t;

typedef struct {
    int unified; /* 통합상태 */
    int profile; /* 매물대상태 -1/0/1 */
    int adx;     /* 0/1/2 */
    int di;      /* DI방향 -1/0/1 */
} tr_fxdec_out_t;

tr_fxdec_out_t tr_fxdec_eval(const tr_fxdec_input_t *in);

#endif
