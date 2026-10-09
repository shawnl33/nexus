#ifndef TR_FX_PNL_2023_H
#define TR_FX_PNL_2023_H

/* 2023_우드스탁_N선물_수익관리.txt. CountIF 창은 현재 봉을 포함한다.
 * DayIndex 는 날짜가 바뀐 첫 봉이 0. 주석 처리된 I_I_·만기일 블록은 쓰지 않는다.
 * new_bar 는 그 봉의 첫 평가에서만 1.
 */

#include <stdint.h>

#define TR_FXPNL_HIST 2048

typedef struct {
    int64_t date;
    double high, low, close;
    double price_scale;
    int market;      /* I_MarketPosition. 매수 1, 매도 -1, 없음 0 */
    int contracts;   /* I_CurrentContracts */
    double avg_entry; /* I_AvgEntryPrice. 0이면 포지션이 생긴 봉의 종가 */
    int new_bar;
    double qty_full; /* 최초진입수량. 0이면 2 */
    double qty_part; /* 일부청산수량. 0이면 1 */
} tr_fxpnl_in_t;

typedef struct {
    int side;
    int bars;
    double open_pts, mfe_pts, mae_pts;
    int open_on, mfe_on, mae_on;
    int open_w, mfe_w, mae_w;
    uint32_t open_rgb, mfe_rgb, mae_rgb;
    double keep_pts, keep2_pts;
    int keep_on, keep2_on;
    uint32_t keep_rgb;
    double closed_pts;
    double closed_long_pts;
    int closed_long_on;
    uint32_t closed_long_rgb;
    double closed_short_pts;
    int closed_short_on;
    double exit_pts;
    int exit_on;
    int entries, wins;
    double p4, p5, p30;
    int p4_on, p5_on, p30_on;
    int flips, danger;
    int p26_on, p27_on;
} tr_fxpnl_out_t;

typedef struct {
    double pnl;
    double hi, lo;
    int hi_cnt, lo_cnt;
    int bars;
    double mfe_long, mae_long, open_long;
    double mfe_short, mae_short, open_short;
    double exit_pnl;
    int exit_n, exit_age;
    double closed_long, closed_short;
    int entries_long, entries_short, wins, losses;
    int flat_age;
    int flips, danger;
    double entry_px;
    int market, contracts;
    int64_t date;
    int day_index;
} tr_fxpnl_bar_t;

typedef struct {
    double pnl_pts;
    double short_mfe_pts;
    unsigned char hi_rose;
    unsigned char went_flat;
    unsigned char drop60;
} tr_fxpnl_hist_t;

typedef struct {
    tr_fxpnl_bar_t cur;
    tr_fxpnl_bar_t closed;
    tr_fxpnl_hist_t cur_hist;
    tr_fxpnl_hist_t hist[TR_FXPNL_HIST];
    int hist_i;
    int hist_n;
    int has_cur;
    int has_closed;
} tr_fxpnl_t;

void tr_fxpnl_init(tr_fxpnl_t *s);
void tr_fxpnl_eval(tr_fxpnl_t *s, const tr_fxpnl_in_t *in, tr_fxpnl_out_t *out);

#endif
