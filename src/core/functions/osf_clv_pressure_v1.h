#ifndef TR_OSF_CLV_PRESSURE_V1_H
#define TR_OSF_CLV_PRESSURE_V1_H

/* OSF_ClvPressureV1. 호가 없는 해외선물의 봉 안 매수·매도 압력.
 * CLV = ((종가-저가)-(고가-종가))/(고가-저가)*100.
 * 점수 = 원시 60% + 3봉평균 25% + 5봉평균 15%. 기울기3은 4봉째부터.
 * 유효는 항상 1. 같은 봉 재평가는 한 봉만 반영한다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/model/time_us.h"

typedef struct {
    double interest; /* 관심기준, 절댓값, 하한 1 */
    double strong;   /* 강세기준, 절댓값, 관심 이상 */
    int reverse;     /* 1이면 부호 반전 */
} tr_osf_clv_config_t;

typedef struct {
    double high, low, close;
    tr_time_us_t bar_open;
} tr_osf_clv_input_t;

typedef struct {
    tr_osf_clv_config_t cfg;
    double raw[8];
    double score_h[8];
    int n;
    bool has_bar;
    tr_time_us_t last_open;
    double score;
    double slope3;
    int state;
    int valid;
} tr_osf_clv_t;

void tr_osf_clv_init(tr_osf_clv_t *s, double interest, double strong, int reverse);
void tr_osf_clv_eval(tr_osf_clv_t *s, const tr_osf_clv_input_t *in);

#endif
