#ifndef TR_FX_SESSION_PROFILE_V2_H
#define TR_FX_SESSION_PROFILE_V2_H

/* WSF_FXSessionProfileV2_CO (원본 119줄). 계산기간 0이면 V1과 같다.
 * 세션 누적 또는 최근 N봉의 (H+L+C)/3 거래량가중 중심과 평균편차로 1·2차 밴드를 만든다.
 * 거래량이 0 이하면 가중치 1. 평균편차가 PriceScale보다 작으면 PriceScale로 올린다.
 * [1]과 봉 창은 봉 말 스냅샷이다. 같은 봉 재평가는 한 봉분만 반영한다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/model/time_us.h"

#define TR_FXPROF_CAP 256

typedef struct {
    bool session_reset;
    bool is_new_bar;
    tr_time_us_t bar_open;
    double high, low, close, volume;
    double price_scale;
    double value_mult; /* 가치영역배수 */
    int32_t min_bars;
    int32_t period;    /* 0=세션 누적(V1), N=최근 N봉 */
} tr_fxprof_input_t;

typedef struct {
    double center, hi1, lo1, hi2, lo2;
    int state5;
    int valid;
    int32_t session_bars;
} tr_fxprof_out_t;

typedef struct {
    int32_t bars;
    double cum_w, cum_pv, cum_dev;
    int n;
    double tp[TR_FXPROF_CAP];
    double w[TR_FXPROF_CAP];
} tr_fxprof_mem_t;

typedef struct {
    tr_fxprof_mem_t st;
    tr_fxprof_mem_t prev;
    bool has_bar;
    tr_time_us_t last_open;
    tr_fxprof_out_t out;
} tr_fxprof_t;

void tr_fxprof_init(tr_fxprof_t *s);
void tr_fxprof_eval(tr_fxprof_t *s, const tr_fxprof_input_t *in);

#endif
