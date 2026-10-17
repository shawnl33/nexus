#ifndef TR_VOLBURST_PAINT_H
#define TR_VOLBURST_PAINT_H

/* #BG_Vol_burst_V2 강조. 1분봉 캔들 색만 낸다. 주문은 없다.
 *
 * ATRv 는 진폭의 Wilder 평활이다. 내장 ATR() 의 단순이동평균이 아니다.
 * ATRLong, ATRAft, RVAvg 는 ma() 다. 기간이 차지 않은 칸은 0으로 평균한다.
 * 상대거래량은 이번 거래량 / 직전 봉의 RVAvg 다.
 * 신호 봉 Since=0, 다음 EntryBars 봉이 진입색, 그 다음 HoldBars 까지가 하늘색이다.
 * 같은 봉을 다시 넣으면(is_new_bar=0) 그 봉의 첫 계산 전으로 되돌린 뒤 다시 계산한다.
 */

#include <stdbool.h>
#include <stdint.h>

#define TR_VB_RGB(r, g, b) ((uint32_t)(((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))
#define TR_VB_STRONG TR_VB_RGB(160, 0, 255)
#define TR_VB_MID TR_VB_RGB(255, 140, 0)
#define TR_VB_STRONG_ZONE TR_VB_RGB(255, 150, 200)
#define TR_VB_MID_ZONE TR_VB_RGB(255, 230, 0)
#define TR_VB_HOLD TR_VB_RGB(120, 200, 255)

#define TR_VB_ATR_CAP 390
#define TR_VB_VOL_CAP 64
#define TR_VB_RNG_CAP 64

typedef struct {
    int rv_len;
    int atr_len;
    int long_len;
    int range_len;
    double strong_rv;
    double strong_vol;
    double mid_vol;
    double mid_range;
    double mid_rv;
    int cooldown;
    int sig_start;
    int sig_end;
    int entry_bars;
    int hold_bars;
    int show_hold;
    int aft_on;
    int aft_start;
    int aft_len;
    double aft_rv;
} tr_vb_cfg_t;

typedef struct {
    int64_t time; /* HHMMSS */
    double high, low, close, volume;
    int is_new_bar;
} tr_vb_bar_t;

typedef struct {
    int bar_cnt;
    double atr;
    double prev_c;
    int has_prev;
    double rv_avg;
    double rel_v;
    int last_sig;
    int last_lvl;
    double atr_hist[TR_VB_ATR_CAP];
    int atr_n;
    double vol_hist[TR_VB_VOL_CAP];
    int vol_n;
    double hi_hist[TR_VB_RNG_CAP];
    double lo_hist[TR_VB_RNG_CAP];
    int rng_n;
} tr_vb_commit_t;

typedef struct {
    tr_vb_cfg_t cfg;
    tr_vb_commit_t cur;
    tr_vb_commit_t open;
    int has_open;
} tr_vb_t;

void tr_vb_default_cfg(tr_vb_cfg_t *cfg);
/* 기간이 저장 상한을 넘으면 false. */
bool tr_vb_init(tr_vb_t *s, const tr_vb_cfg_t *cfg);
/* 칠하지 않으면 0. */
uint32_t tr_vb_on_bar(tr_vb_t *s, const tr_vb_bar_t *bar);

#endif
