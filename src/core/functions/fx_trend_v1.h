#ifndef TR_FX_TREND_V1_H
#define TR_FX_TREND_V1_H

/* WSF_FXTrendV1 (원본 151줄). 1분봉에서 07:00/15:30 세션의 (고+저)/2를
 * 완성 일봉으로 모아 회귀하고, WSF_FXTrendStateV1으로 방향·강약을 낸다.
 * 오늘 진행 세션은 표본에서 빠진다. 첫 세션이 경계 시각이 아니면 저장하지 않는다.
 * 회귀기간은 5~100. 표본이 기간 미만이면 회귀선은 종가, 추세는 0.
 * price_scale은 TrendState의 PriceScale이다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/model/time_us.h"

typedef struct {
    int32_t reg_period; /* 5~100 */
} tr_fxtrend_config_t;

typedef struct {
    bool session_reset;
    int64_t bar_index;
    int64_t time_hhmmss;
    double high, low, close;
    double price_scale;
    double min_r2;
    tr_time_us_t bar_open;
} tr_fxtrend_input_t;

typedef struct {
    tr_fxtrend_config_t cfg;
    bool initialized;
    bool full_start;
    double sess_high, sess_low;
    int64_t last_start_bar;
    double mids[100]; /* [0]=최신 완성 */
    int32_t completed;
    double line, slope, r2, residual;
    int link_valid;
    int dir, state;
    double strength;
    int valid;
} tr_fxtrend_t;

void tr_fxtrend_init(tr_fxtrend_t *s, const tr_fxtrend_config_t *cfg);
void tr_fxtrend_eval(tr_fxtrend_t *s, const tr_fxtrend_input_t *in);

#endif
