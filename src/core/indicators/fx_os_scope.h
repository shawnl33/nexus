#ifndef TR_FX_OS_SCOPE_H
#define TR_FX_OS_SCOPE_H

/* #우드스탁_해외선물1분통합판정 V13, 매물대압축지속 V4,
 * 평탄카운트 V4, 조정분석 V6, 1분통합판정강조, 진입신호매매, 수익관리.
 * 기본 입력은 원본 Input 기본값.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/functions/fx_adx_v1.h"
#include "core/functions/fx_curve_v1.h"
#include "core/functions/fx_future_values_v1.h"
#include "core/functions/fx_session_profile_v2.h"
#include "core/functions/fx_squeeze_v1.h"
#include "core/functions/fx_flat_count_v2.h"
#include "core/functions/fx_wave_adj_v6.h"
#include "core/indicators/fx_entry_cand.h"
#include "core/indicators/fx_pnl_2023.h"
#include "core/model/time_us.h"

typedef struct {
    int64_t date;
    int64_t time;
    tr_time_us_t bar_open;
    double high, low, close, volume;
    bool is_new_bar;
    const tr_fxfv_output_t *fv;
    int session_first;
    double htf_dir;
    int reg_valid;
    double reg_line, reg_r2;
    int ob_valid;
    double ob_score;
} tr_fxos_input_t;

/* 조정분석 한 버전의 화면 값. 비율은 조정 중·신호 봉에서만 0이 아니다. */
typedef struct {
    double time_r, price_r, opp_r;
    int state;
    uint32_t state_rgb;
    int sig, sig_w;
    uint32_t sig_rgb;
} tr_fxwave_view_t;

typedef struct {
    int judge; /* 통합상태 */
    uint32_t judge_rgb;
    int fut, prof, di, adx;
    int sq_on;
    int sq_len;
    uint32_t sq_rgb;
    int sq_w;
    int hold;
    uint32_t hold_rgb;
    int ratio_on;
    double ratio;
    uint32_t ratio_rgb;
    int rel_on;
    int rel_len;
    uint32_t rel_rgb;
    int rel_w;
    int cf_on;
    int cf_len;
    uint32_t cf_rgb;
    int cf_w;
    int ent_on;
    int ent_y;
    uint32_t ent_rgb;
    int ent_w;
    int adj_on, adj_y, adj_w;
    uint32_t adj_rgb;
    int brk_on, brk_y;
    uint32_t brk_rgb;
    int lead_on, lead_y;
    uint32_t lead_rgb;
    int flat_pos, flat_slope;
    uint32_t flat_pos_rgb, flat_slope_rgb;
    int flat_mark;
    int flat_on;
    int flat_up_pos, flat_dn_pos, flat_up_slope, flat_dn_slope;
    double flat_amp;
    double wave_time, wave_price, wave_opp;
    int wave_state;
    uint32_t wave_state_rgb;
    int wave_sig, wave_sig_w;
    uint32_t wave_sig_rgb;
    uint32_t paint_rgb;
    int sig_dir, sig_exit, sig_kind, sig_qty, exit_qty;
    double pnl_open, pnl_mfe, pnl_mae, pnl_closed, pnl_keep;
    int pnl_entries, pnl_wins;
    uint32_t pnl_open_rgb, pnl_mfe_rgb, pnl_mae_rgb, pnl_keep_rgb;
    int pnl_open_w, pnl_mfe_w, pnl_mae_w, pnl_keep_w;
    int pnl_open_on, pnl_mfe_on, pnl_mae_on, pnl_keep_on;
    double pnl_p4, pnl_p5, pnl_p30, pnl_keep2;
    int pnl_p4_on, pnl_p5_on, pnl_p30_on, pnl_keep2_on;
    int pnl_flips, pnl_danger, pnl_p26_on, pnl_p27_on;
    double pnl_long;
    int pnl_long_on;
    uint32_t pnl_long_rgb;
    int pnl_side;
    double pnl_short;
    int pnl_short_on;
    double pnl_exit;
    int pnl_exit_on;
    /* [0]=V1 [1]=V2 [2]=V4 [3]=V5. V6은 위의 wave_* 이다. */
    tr_fxwave_view_t wv[4];
    int ec_on, ec_w, ec_line_on;
    double ec_px, ec_line;
    uint32_t ec_rgb, ec_line_rgb;
    int rs_on, rs_w;
    double rs_px;
    uint32_t rs_rgb;
    int bk_on;
    double bk_px;
    uint32_t bk_rgb;
    uint32_t os_paint;
} tr_fxos_out_t;

/* 진입신호매매 Input 중 이 계산이 봉마다 읽는 값. 비어 있으면 식의 기본값. */
typedef struct {
    int trade_start;
    int trade_end;
    int rth_start;
    double adj_min;
    int adj_unified;
    int buy_382;
    int state_bars;
    int state_pct;
    int state_pink;
    int sell_u100;
    double sell_low;
    int lead_u100;
    int slope_min;
    int slope_limit;
    int grade_limit;
    int adj_once;
    double strong_px;
    double strong_time;
    double mid_px;
    int start_b_only;
    int struct_boost;
    double struct_px;
    int signal_limit;
    double flip_opp_pct;
    int min_leg;
    int min_trend;
    int min_opp;
    int confirm_back;
    int predict_bars[5];
    double min_r2;
    int persist_bars;
    int market_period;
    int min_hold_bars;
    int swing_link;
    double momentum_ignore_ticks;
    int predict_ticks;
    int confirm_closed;
    int relax;
    int brk_check;
    int brk_once;
    /* 수익관리가 읽는 수량. 0이면 원본 초기값(2, 1). 화면틀 전체가 한 값을 쓴다. */
    double qty_full;
    double qty_part;
    /* 설정창 수량. 주문식에 수량이 없으면 I_CurrentContracts 가 이 값이다. */
    int order_qty;
} tr_fxsig_cfg_t;

typedef struct {
    double price_scale;
    tr_fxsig_cfg_t cfg;
    tr_fxfv_t sig_fv;
    tr_fxc_t curve;
    tr_fxadx_t adx;
    tr_fxprof_t session;
    tr_fxsq_t squeeze;
    tr_fxec_t entry;
    tr_fxflat_out_t flat_prev;
    tr_fxadj_t adj;
    tr_fxadj_t adj1, adj2, adj4, adj5;
    int has_flat;
    double prev_flat, prev_close;
    double prev_up_hi, prev_up_lo, prev_up_382, prev_up_500, prev_up_618;
    double prev_dn_hi, prev_dn_lo, prev_dn_382, prev_dn_500, prev_dn_618;
    int prev_up_struct, prev_dn_struct;
    double buy_used_px, sell_used_px;
    int lead_dn_cross, lead_up_cross;
    int prev_pos, prev_slope;
    double flat_max_range;
    int adj_done, prev_adj_trend;
    unsigned char up_state[60];
    int up_i;
    int strong_buy_gap, strong_sell_gap;
    int buy_gap_saved, sell_gap_saved;
    double g8_buy_px, g8_sell_px;
    int mkt;             /* I_MarketPosition. 1, -1, 0 */
    double avg_entry;    /* I_AvgEntryPrice. 미보유면 0 */
    int contracts;       /* I_CurrentContracts */
    tr_fxpnl_t pnl;
    int prev_color_dir;
    int ec_dir, ec_has, ec_closed_ok;
    double ec_px_st, ec_closed_px;
    int ec_closed_dir;
    int bk_buy, bk_sell, bk_has, bk_has_prev, bk_prev_buy, bk_prev_sell;
    double bk_dn_hi, bk_up_lo, bk_close, bk_prev_dn_hi, bk_prev_up_lo, bk_prev_close;
    double mkt_buf[20];
    int mkt_n, mkt_i, paint_has, paint_first;
    double mkt_avg, mkt_avg_1, paint_fut, paint_fut_1;
    int mkt_avg_1_ok;
    int64_t key;
    bool has_key;
    int reset_bar;
    int32_t session_bars;
    tr_fxos_out_t out;
} tr_fxos_t;

void tr_fxsig_cfg_default(tr_fxsig_cfg_t *cfg);
bool tr_fxos_init(tr_fxos_t *s, double price_scale);
void tr_fxos_set_cfg(tr_fxos_t *s, const tr_fxsig_cfg_t *cfg);
void tr_fxos_eval(tr_fxos_t *s, const tr_fxos_input_t *in);
bool tr_fxos_relink(tr_fxos_t *s);
/* "os" 배열의 숫자만 쓴다. 대괄호는 호출자가 붙인다. */
int tr_fxos_format(char *buf, size_t cap, const tr_fxos_out_t *o);

#endif
