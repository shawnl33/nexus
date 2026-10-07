#ifndef TR_FX_SNIPER_CO_H
#define TR_FX_SNIPER_CO_H

/* #우드스탁_스나이퍼스코프_해외선물_CO_V3.
 * 삼선 비율 분모는 삼선비율기준봉수(0=세션 최대). 회귀·마켓·가격 비율은 비율기준봉수.
 * 입력 선은 FutureValuesV1과 합성 5/15/30(봉 시작 시각)이다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/model/time_us.h"

#define TR_SNCO_CAP 256
#define TR_SNCO_PLOTS 16

typedef struct {
    int id;
    bool on;
    double value;
    uint32_t rgb;
    int width;
} tr_snco_plot_t;

typedef struct {
    int64_t date, time;
    tr_time_us_t bar_open;
    bool is_new_bar;
    double high, low, close, volume;
    double price_scale;
    int fv_ok;
    double reg_flat, market;
    double target[3];
    int reg_ok[3];
    double reg_px[3];
    int mkt_ok[3];
    double mkt_px[3];
    int mkt30;
} tr_snco_input_t;

typedef struct {
    int32_t bars;
    double three_peak, reg_peak, mkt_peak, span_peak;
    int three_ready;
    double three_gap;
    int below, above;
    int leg_n, leg_n_prev;
    double cur_hi, cur_lo, prev_hi, prev_lo;
    int prev_ok, broke;
    int first_break;
    double span;
    int compound, px_exit;
    double vol_sum, vol_prev;
    int vol_ready;
    double vol_ratio;
    int both;
    int n3, nr, nm, ns;
    double h3[TR_SNCO_CAP], hr[TR_SNCO_CAP], hm[TR_SNCO_CAP], hs[TR_SNCO_CAP];
} tr_snco_mem_t;

typedef struct {
    tr_snco_mem_t st, prev;
    bool has_bar;
    tr_time_us_t last_open;
    int64_t key;
    bool has_key;
    int reset_bar;
    tr_snco_plot_t plots[TR_SNCO_PLOTS];
    int scope_state;
} tr_snco_t;

void tr_snco_init(tr_snco_t *s);
void tr_snco_eval(tr_snco_t *s, const tr_snco_input_t *in);

#endif
