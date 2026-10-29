#ifndef TR_FX_JUDGE_PAINT95_H
#define TR_FX_JUDGE_PAINT95_H

/* #우드스탁_해외선물1분통합판정강조 V95. 수익비 V95 진입을 봉 색으로 옮긴 강조식.
 * 평소 봉은 호출자가 둔 통합색을 유지한다. 여기가 반환하는 색은 진입 봉만이다.
 * 짝 주문부(주문번호 1~24)는 아직 이 모듈에 없다. V81~V95 진입만 색을 낸다.
 */

#include <stdint.h>

#define TR_FXP95_MIN 390

typedef struct {
    int64_t date;
    int64_t time; /* HHMMSS */
    int is_new_bar;
    int session_reset; /* FX세션키가 바뀐 봉 */
    double open, high, low, close, volume;
    int judge;
    int fut;
    int di;
    int state5;
    int pos;       /* 선행위치Cnt */
    int slope;     /* 선행기울기Cnt */
    int pos_prev;  /* 직전 봉 선행위치Cnt */
    double target5;
    double swing_ratio;
    int red_struct;
    double up_lo;
    double up_hi;
} tr_fxp95_in_t;

typedef struct {
    double hi[16], lo[16], cl[16];
    int wr;
    int n;
    int prev_rth;
    int seen;
    int prev_date;
    int prev_bmin;
    double sess_open, day_hi, day_lo;
    double prev_hi, prev_lo, prev2_hi, prev2_lo;
    double london;
    double amp_today[TR_FXP95_MIN];
    double amp_h[5][TR_FXP95_MIN];
    double vol_today[TR_FXP95_MIN];
    double vol_h[5][TR_FXP95_MIN];
    int amp_n, amp_last, amp_keep;
    double run_hi, run_lo, run_vol;
    int absorb;
    int uni, uni_side, prev_uni;
    int n_recov, n_fade, n_absorb, n_ext, ext_wait;
    int n_ylo, n_yhi, n_edn, n_eup, n_q30;
    int pos_dir;
    int real_n;
} tr_fxp95_rt;

typedef struct {
    tr_fxp95_rt cur;
    tr_fxp95_rt open;
    int has_open;
    int group;
    int shadow;
    int order_no;
    int dir;
    uint32_t rgb;
} tr_fxp95_t;

/* 신호그룹·그림자 → 봉 색. 그룹 0이면 judge_rgb. */
uint32_t tr_fxp95_color(int group, int shadow, uint32_t judge_rgb);

/* 한 봉. 진입이 있으면 group, order_no, dir, rgb를 채운다. 없으면 group 0. */
void tr_fxp95_bar(tr_fxp95_t *s, const tr_fxp95_in_t *in);
const char *tr_fxp95_order_name(int order_no);

#endif
