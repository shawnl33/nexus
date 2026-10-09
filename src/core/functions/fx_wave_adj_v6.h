#ifndef TR_FX_WAVE_ADJ_V6_H
#define TR_FX_WAVE_ADJ_V6_H

/* WSF_FXWaveAdjV6_CO.txt. 같은 봉은 직전 봉 스냅샷에서 다시 계산한다.
 * new_bar 는 그 봉의 첫 평가에서만 1. 입력 기본값은 호출하는 수식의 Input.
 */

#include "core/functions/fx_flat_count_v2.h"

typedef struct {
    double flat, flat_prev;
    double high, low, close, close_prev;
    double price_scale;
    double up_hi, up_lo, up_382;
    double dn_hi, dn_lo, dn_618;
    int session_reset;
    int new_bar;
    int min_leg;
    int min_trend;
    int min_opp;
    int confirm_back;
    double strong_px;
    double strong_time;
    double mid_px;
    int start_b_only;
    int struct_boost;
    double struct_px;
    int signal_limit;
    double flip_opp_pct;
    /* 0 은 V6. 1 은 극값 대 시작가 전환. 2 는 종가 전환과 원구조.
     * 4 는 최근 스윙·즉시 전환·신호 제한. 5 는 가격이 구조를 깨면 무효. 6 은 구조 소진. */
    int rev;
} tr_fxadj_in_t;

typedef struct {
    int trend;
    int in_adj;
    double time_ratio;
    double price_ratio;
    double opp_ratio;
    int sig_dir;
    int sig_kind;
    int sig_grade;
    int flipped;
    int valid;
} tr_fxadj_out_t;

/* 직전 봉에 남기는 계열. 이름은 원본 Var. */
typedef struct {
    tr_fxflat_out_t flat;
    double run_hi; /* 런고 */
    double run_lo; /* 런저 */
    int trend;     /* 추세방향 */
    int trend_len; /* 추세L */
    double trend_start;
    double trend_ext;
    int in_adj;
    int adj_bars;
    double adj_ext;
    double opp_max;
    int bent;
    int trend_age;
    int recent_swing;
    int up_spent;
    int dn_spent;
    double up_hi_v, up_lo_v, dn_hi_v, dn_lo_v;
} tr_fxadj_bar_t;

typedef struct {
    tr_fxadj_bar_t cur;
    tr_fxadj_bar_t closed; /* [1] */
    int has_cur;
    int has_closed;
} tr_fxadj_t;

void tr_fxadj_init(tr_fxadj_t *s);
void tr_fxadj_eval(tr_fxadj_t *s, const tr_fxadj_in_t *in, tr_fxadj_out_t *out);

#endif
