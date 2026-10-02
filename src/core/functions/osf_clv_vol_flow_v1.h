#ifndef TR_OSF_CLV_VOL_FLOW_V1_H
#define TR_OSF_CLV_VOL_FLOW_V1_H

/* OSF_ClvVolFlowV1. 방향은 거래량흐름, 세기는 종가위치(CLV)가 조절한다.
 * 같은 방향이면 흐름×1.2, 다르면 ×0.5, 흐름이 0이면 0. 그다음 ±100.
 * 부호반전은 결합 후에 한 번. 유효는 거래량흐름의 워밍업을 따른다.
 */

#include "core/functions/osf_clv_pressure_v1.h"
#include "core/functions/osf_vol_flow_v1.h"

typedef struct {
    tr_osf_clv_t clv;
    tr_osf_flow_t flow;
    int reverse;
    double interest, strong;
    int32_t period;
    double score_h[8];
    int sn;
    bool has_bar;
    tr_time_us_t last_open;
    double score;
    double slope3;
    int state;
    int valid;
} tr_osf_combo_t;

void tr_osf_combo_init(tr_osf_combo_t *s, double interest, double strong, int reverse, int32_t period);
void tr_osf_combo_eval(tr_osf_combo_t *s, const tr_osf_flow_input_t *in);

#endif
