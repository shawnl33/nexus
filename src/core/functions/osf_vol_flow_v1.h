#ifndef TR_OSF_VOL_FLOW_V1_H
#define TR_OSF_VOL_FLOW_V1_H

/* OSF_VolFlowV1. 종가 위치(-1~+1)에 거래량을 곱한 값의 기간 평균을
 * 같은 기간 거래량 평균으로 나눈 흐름(-100~+100).
 * 유효는 봉 수 >= 기간. 기울기3은 기간+3봉째부터.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/model/time_us.h"

#define TR_OSF_FLOW_CAP 64

typedef struct {
    double interest;
    double strong;
    int reverse;
    int32_t period; /* 하한 2 */
} tr_osf_flow_config_t;

typedef struct {
    double high, low, close, volume;
    tr_time_us_t bar_open;
} tr_osf_flow_input_t;

typedef struct {
    tr_osf_flow_config_t cfg;
    double mfv[TR_OSF_FLOW_CAP];
    double vol[TR_OSF_FLOW_CAP];
    double score_h[8];
    int n;
    int sn;
    bool has_bar;
    tr_time_us_t last_open;
    double score;
    double slope3;
    int state;
    int valid;
} tr_osf_flow_t;

void tr_osf_flow_init(tr_osf_flow_t *s, double interest, double strong, int reverse, int32_t period);
void tr_osf_flow_eval(tr_osf_flow_t *s, const tr_osf_flow_input_t *in);

#endif
