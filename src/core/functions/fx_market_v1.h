#ifndef TR_FX_MARKET_V1_H
#define TR_FX_MARKET_V1_H

/* WSF_FXMarketV1 (원본 63줄). 최근 계산기간 봉의 (H+L+C)/3 거래량가중 중심과
 * 가중 모집단 표준편차 밴드. 기간 하한 2, 상한 100. 배수 0 이하면 1.
 * V<=0 봉은 합에서 빠진다. 같은 봉 재평가는 최신 표본만 바꾼다.
 * 누적거래량이 0이면 출력은 0.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/model/time_us.h"

#define TR_FXMKT_CAP 100

typedef struct {
    double high, low, close, volume;
    tr_time_us_t bar_open;
} tr_fxmkt_input_t;

typedef struct {
    uint32_t period;
    double band_mult;
    double tp[TR_FXMKT_CAP];
    double vol[TR_FXMKT_CAP];
    int32_t count;
    bool has_bar;
    tr_time_us_t last_open;
    double center, upper, lower;
    bool valid;
} tr_fxmkt_t;

bool tr_fxmkt_init(tr_fxmkt_t *s, int32_t period, double band_mult);
void tr_fxmkt_eval(tr_fxmkt_t *s, const tr_fxmkt_input_t *in);

#endif
