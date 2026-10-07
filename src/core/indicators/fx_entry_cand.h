#ifndef TR_FX_ENTRY_CAND_H
#define TR_FX_ENTRY_CAND_H

/* WSF_FXEntryCandV15_CO. V3 경로(등급 1~3) 위에 구조·S·R·T 를 얹는다.
 * 미래곡선 Plot25·26 이 이 출력을 쓴다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/functions/fx_adx_v1.h"
#include "core/functions/fx_curve_v1.h"
#include "core/functions/fx_future_values_v1.h"
#include "core/functions/fx_session_profile_v2.h"
#include "core/functions/fx_squeeze_v1.h"
#include "core/model/time_us.h"

#define TR_FXEC_HIST 8

typedef struct {
    int mode;
    int c_grade_in;
    double break_ticks_in;
    int confirm_bars, di_required, expand_bars, release_valid_bars;
    int release_a, rearm_bars, b_extra, b_on;
    int entry_start, entry_end;
    int struct_on, struct_width, struct_valid_bars, struct_buy, struct_include_release;
    int s_on, s_width, s_squeeze_bars, s_ratio, s_active_bars, s_wait_bars;
    int block_start, block_bars;
    int r_on, r_three, r_swing, r_and, r_ratio_max, r_rev_bars, r_pos_on, r_leg_min;
    double r_slack_ticks;
    int weak_window, weak_wait, opp_window, opp_min, opp_struct;
    int t_on, t_start, t_end, t_quiet, t_three, t_ratio, t_width, t_pos_on;
    double mark_ticks;
    double value_mult;
    int min_bars, adx_period;
    double adx_trend, adx_strong;
    int profile_mode;
    int width_bars, ratio_bars, stage1_bars, confirm_sq_bars;
    double narrow_pct, predict_ticks;
} tr_fxec_cfg_t;

typedef struct {
    int reset, bars, cur_bar, time_hms;
    double high, low, close, price_scale;
    int unified, future_dir, adx_valid, profile_valid;
    double plus_di, minus_di, center;
    int sq_hold, sq_release_dir, sq_confirm_dir, sq_release;
    double sq_ratio, sq_release_len;
    int box_dir;
    double box_hi, box_lo;
    int fv_ok;
    double up_hi, up_lo, dn_hi, dn_lo;
    int v1_ok;
    double tgt[3];
} tr_fxec_bar_t;

typedef struct {
    int rel_dir, rel_kind, rel_age, block_dir, entries;
    int pend_dir, pend_n;
    double pend_hi, pend_lo;
    int exp_dir, exp_age;
    double exp_hi, exp_lo;
    double session_max_w;
    int struct_dir, struct_age;
    int last_dir, last_age;
    int s_wait_dir, s_wait_age;
    double s_wait_px;
    double three_peak;
    int p_n, p_prev_ok;
    double p_hi, p_lo, p_prev_hi, p_prev_lo;
    double recent_release_len;
    int buy_age, sell_age, strong_buy_age, strong_sell_age;
    int weak_dir, weak_age, weak_grade;
    int uni[TR_FXEC_HIST];
    int sok, sok1, blue, blue1, red, red1, three_ok, three_ok1;
    double wpct, wpct1, ahi, ahi1, alo, alo1, dn_lo, dn_lo1, up_hi, up_hi1;
    double three_ratio, three_ratio1, sq_ratio1, three_gap, three_gap1;
    int sq_hold1;
    double close1;
} tr_fxec_mem_t;

typedef struct {
    int dir, grade, unified, diag;
    double price;
    uint32_t rgb;
    int size;
    int emph;
    uint32_t emph_rgb;
} tr_fxec_out_t;

typedef struct {
    int64_t date, time;
    tr_time_us_t bar_open;
    bool is_new_bar;
    double high, low, close, volume;
    const tr_fxfv_output_t *fv;
} tr_fxec_input_t;

typedef struct {
    tr_fxec_cfg_t cfg;
    double price_scale;
    tr_fxc_t curve;
    tr_fxadx_t adx;
    tr_fxprof_t profile;
    tr_fxsq_t squeeze_v1;
    tr_fxsq_t squeeze_v4;
    tr_fxec_mem_t st, prev;
    bool has_bar;
    tr_time_us_t last_open;
    int64_t key;
    bool has_key;
    int reset_bar, session_bars, evals;
    tr_fxec_out_t out;
} tr_fxec_t;

void tr_fxec_default_cfg(tr_fxec_cfg_t *cfg);
void tr_fxec_decide(tr_fxec_mem_t *m, const tr_fxec_mem_t *old, const tr_fxec_cfg_t *cfg,
                    const tr_fxec_bar_t *b, tr_fxec_out_t *o);
bool tr_fxec_init(tr_fxec_t *s, double price_scale);
void tr_fxec_eval(tr_fxec_t *s, const tr_fxec_input_t *in);
bool tr_fxec_relink(tr_fxec_t *s);

#endif
