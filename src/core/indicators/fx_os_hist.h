#ifndef TR_FX_OS_HIST_H
#define TR_FX_OS_HIST_H

/* 1분통합판정이 봉마다 다시 만드는 비율·고리.
 * 미래값·압축·평탄의 출력만 받고, 그 함수를 다시 계산하지 않는다.
 * 고리의 [0]이 이번 봉이다.
 */

#define TR_FXOS_HIST_CAP 256

typedef struct {
    int session_first;
    double t1, t2, t3;
    double open, high, low, close;
    int two_window;
    int two_min_bars;
    int flat_ready;
    double flat_1, flat_5, flat_15, flat_30;
    int flat_window;
    int fv_ok;
    double rise_hi, rise_lo, fall_hi, fall_lo;
    int prev_dn_len;
    int prev_up_len;
    int slope;
    double sq_ratio;
    int prev_up_slope;
    int prev_dn_slope;
    int sq_hold;
    int sq_color;
    int unified;
    int turn_buy;
    double mkt_1;
    int mkt5_ok, mkt15_ok, mkt30_ok;
    double mkt_5, mkt_15, mkt_30;
    double price_scale;
    int slack_ticks;
} tr_fxos_hist_in_t;

typedef struct {
    double three_ratio;
    double two_ratio;
    double flat_ratio;
    double swing;
    double prior_low;
    double prior_high;
    int width_seen;
    int tall_buy;
    int tall_sell;
    int hot_up;
    int hot_dn;
    int red_n;
    int bars_since_buy;
    int had_m100;
    int had_p100;
    int gray;
    int bars;
    double three_max;
    double spread;
    int was_ready;
    int seg_count;
    double seg_hi, seg_lo;
    double prev_hi, prev_lo;
    int prev_ok;
    int session_bars;
    double two_max;
    double flat_max;
    double swing_max;
    double mkt_max;
    double mkt_r[TR_FXOS_HIST_CAP];
    int break_armed;
    int prev_first;
    int prev_px_exit;
    int prev_compound;
    double open_r[TR_FXOS_HIST_CAP];
    double high_r[TR_FXOS_HIST_CAP];
    double low_r[TR_FXOS_HIST_CAP];
    double close_r[TR_FXOS_HIST_CAP];
    double sq_r[TR_FXOS_HIST_CAP];
    double range_r[TR_FXOS_HIST_CAP];
    double flat_r[TR_FXOS_HIST_CAP];
    int up_r[TR_FXOS_HIST_CAP];
    int dn_r[TR_FXOS_HIST_CAP];
    int hold_r[TR_FXOS_HIST_CAP];
    int color_r[TR_FXOS_HIST_CAP];
    int unified_r[TR_FXOS_HIST_CAP];
} tr_fxos_hist_t;

void tr_fxos_hist_init(tr_fxos_hist_t *h);
void tr_fxos_hist_bar(tr_fxos_hist_t *h, const tr_fxos_hist_in_t *in);
/* 종가가 박스 상단 1/3이면 1, 하단 1/3이면 -1, 가운데거나 박스가 없으면 0. */
int tr_fxos_box_color(double close, double hi, double lo, int valid);

#endif
