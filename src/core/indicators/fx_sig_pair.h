#ifndef TR_FX_SIG_PAIR_H
#define TR_FX_SIG_PAIR_H

/* #우드스탁_해외선물진입신호매매_짝_V45 주문 층.
 * 신호는 1단계 판정(V49)을 받고, 짝 전용 금지·이름·수량·1차 지정가만 여기서 정한다.
 * 설정은 원본 Var 기본값으로 고정한다.
 */

typedef struct {
    int dir;
    int kind;
    int grade;
    int adj_kind;
    int adj_grade;
    int unified;
    int slope;
    int pos;
    int lead_valid;
    int time_hms;
    int rth_hms;
    int session_first;
    int color_dir;
    double high, low, close;
    double price_scale;
    double price_unit; /* 1포인트의 입력 단위: 원가격 1, raw 가격 100. 0이면 1. */
    double rise_hi, rise_lo, fall_hi, fall_lo;
    double width_ratio;
    double swing;
    int di;
    int prev_dn; /* 선행직전하락기울기. 양수 길이 */
    double three_ratio;
    int sq_hold;
    int has_market; /* 1이면 색·시간 청산 주문은 MarketPosition이 같은 방향일 때만 */
    int market;     /* 1, -1, 0 */
    int sq_hold_prev;
    int unified_count;
} tr_fxpair_in_t;

typedef struct {
    int virt;
    double entry_px, hi, lo;
    int first_ticks;
    int session_n;
    int last_dir;
    int prev_dir;
    int prev_color_exit;
    double prev_pnl;
    double pnl_buy, pnl_sell;
    int done;
    int reach[50];
    int last_color;
    int v68;
    int bar_n;
    int exit_bar;
    int entry_bar;
    int move_ticks;
    int flat_after;
    int profit_flips;
    double prev_mtm;
    int prev_sq;
    int uni_count;
    int uni_last;
    int limit_filled;
    int remaining_qty; /* 차트 청산 표시용 잔량. 가상포지션 판정과 분리한다. */
    char name[96];
    char exit_name[96];
} tr_fxpair_t;

typedef struct {
    int entry_dir;
    int entry_qty;
    const char *entry_name;
    int exit_code;
    int exit_loss;
    int suf;
    const char *exit_name;
    int limit_dir;
    double limit_px;
    int limit_qty;
    const char *limit_name;
    int limit_hit;
    int exit_qty;
    int limit_hit_qty;
} tr_fxpair_out_t;

void tr_fxpair_init(tr_fxpair_t *s);
void tr_fxpair_on_bar(tr_fxpair_t *s, const tr_fxpair_in_t *in, tr_fxpair_out_t *out);

#endif
