#ifndef TR_FX_POSMON_H
#define TR_FX_POSMON_H

/* #우드스탁_해외선물포지션감시_짝_V1 / V2.
 * 같은 차트의 시스템 포지션을 읽어 손익을 틱 단위로 낸다.
 * v2 가 0 이면 달력 날짜로 일을 나누고, 1 이면 07:00/15:30 세션키와
 * 지표가 저장한 직전 평균가를 쓴다.
 */

#include <stdint.h>

typedef struct {
    int64_t date;
    int64_t time;
    double high, low, close;
    double price_scale;
    double tick_mult;     /* 틱배율. 0이면 1 */
    double partial_ticks; /* 일부청산틱. 음수면 10 */
    int market;
    int contracts;
    double avg;
    int v2;
} tr_fxpos_in_t;

typedef struct {
    int side;
    double open_ticks;
    double mfe_ticks;
    double mae_ticks;
    double exit_ticks;
    int exit_on;
    double closed_long;
    double closed_short;
    int wins;
    int losses;
    uint32_t open_rgb;
    uint32_t mfe_rgb;
    uint32_t mae_rgb;
    uint32_t long_rgb;
    int open_w;
    int mfe_w;
    int mae_w;
    double keep_mfe;
    int keep_mfe_on;
    double keep_mae;
    int keep_mae_on;
    int flips;
    int danger;
    double total_ticks;
    int total_on;
} tr_fxpos_out_t;

typedef struct {
    int64_t date;
    int64_t session;
    int has;
    int prev_mkt;
    int prev_qty;
    double prev_avg;
    int held_mkt;
    int held_qty;
    double held_avg;
    int bars;
    double mfe_long, mae_long, open_long;
    double mfe_short, mae_short, open_short;
    double exit_pnl;
    int exit_age;
    double closed_long, closed_short;
    int wins, losses;
    int hi_cnt, lo_cnt;
    double hi, lo;
    int flips, danger;
    double prev_pnl;
    int hist_i, hist_n;
    double open_ring[512];
    double mfe_ring[512];
    unsigned char rose_ring[512];
} tr_fxpos_t;

void tr_fxpos_init(tr_fxpos_t *s);
void tr_fxpos_eval(tr_fxpos_t *s, const tr_fxpos_in_t *in, tr_fxpos_out_t *out);

#endif
