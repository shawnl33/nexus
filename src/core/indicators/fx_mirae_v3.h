#ifndef TR_FX_MIRAE_V3_H
#define TR_FX_MIRAE_V3_H

/* #WSF_해외선물미래곡선V3 표시부.
 *
 * 함수 체인(회귀·예측·갭·정렬·스윙)이 만든 값을 Plot으로 바꾼다.
 * 추세선(TL_) 작도와 DEBUG Plot90~95는 포함하지 않는다.
 *
 * 담당 Plot:
 *   1  단계화 −5~+5 (세션 첫 봉은 0)
 *   7  평탄 회귀선 (단계 색, 신뢰도로 두께)
 *   20~24 과거 채점 (모드 1은 10봉 결과띠, 모드 2는 지난 예측가)
 *   30~31 통합 매수/매도 상태
 *   32~35·56~57 방향 기억 목표
 *   36~41·58~59·83~84 기억 범위 (방금 갱신한 봉·5봉 이탈 시 숨김)
 *   42~50·85~89·96 지속 목표와 범위
 *   51~55 마켓 중심·밴드 (중심색은 단계 0이면 단계화 색)
 *   60~70 스윙 되돌림·시간비율, 71~82 확장 목표 (실효등급별 하나만)
 *
 * 과거 [n]은 같은 세션 안에서만 유효하다. 이력이 없으면 무효로 둔다.
 * 같은 봉 재평가는 세션 번호·연속 봉수를 한 번만 올린다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/functions/fx_curve_v1.h"
#include "core/functions/fx_gap_v1.h"
#include "core/functions/fx_predict_v2.h"
#include "core/functions/fx_reg_v1.h"
#include "core/functions/fx_swing_v1.h"
#include "core/functions/fx_trend_v1.h"
#include "core/model/time_us.h"

#define TR_FXV3_PLOT_N 97
#define TR_FXV3_HIST 128

typedef struct {
    int32_t predict_bars[5]; /* 예측봉수1~5, 기본 5/10/15/30/60 */
    double min_r2;           /* 최소신뢰도, 기본 0.40 */
    int32_t persist_bars;    /* 지속봉수, 기본 5 */
    double price_scale;      /* PriceScale (>0) */
    int past_mode;           /* 과거예측표시 0/1/2, 기본 1 */
    int show_range;          /* 예측범위표시, 기본 1 */
    int show_trade;          /* 매매상태표시, 기본 1 */
    double min_final_strength;
    int32_t market_period;   /* 마켓계산기간, 기본 20 */
    double market_band;      /* 마켓밴드배수, 기본 1 */
    int show_mkt_band;       /* 마켓밴드표시, 기본 1 */
    int32_t min_hold_bars;   /* 최소전환유지봉수, 기본 3 */
    double fade_time;        /* 페이드기준시간비율, 기본 1 */
    int show_time_ratio;     /* 시간비율표시, 기본 1 */
} tr_fxv3_config_t;

typedef struct {
    bool session_reset;
    int64_t bar_index;
    tr_time_us_t bar_open;
    double high, low, close;
    int reg_valid;
    double reg_line;
    double reg_r2;
    double reg_resid;
    int reg_sign;          /* 곡선회귀선_구분 */
    double pred_price[5];
    int pred_dir[5];
    double pred_vol;       /* MTF예측변동성 */
    double future_dir;     /* 핵심세션미래방향 */
    int market_dir;        /* 핵심마켓방향 */
    int reg_dir;           /* 핵심회귀방향 */
    int final_valid;
    int final_dir;
    int final_state;
    /* 마켓 밴드. 1분봉은 세션 안에서만 거래량가중 */
    int mkt_valid;
    double mkt_center, mkt_up1, mkt_dn1, mkt_up2, mkt_dn2;
    int mkt_stage; /* -3..3, 0이면 Plot51 색은 단계화 색 */
    /* 스윙. 등급 -1 없음, -2 붕괴, 0 얕음, 1 중간, 2 깊음 */
    int sw_leg_dir;
    double sw_time_ratio;
    int sw_up_grade, sw_dn_grade;
    tr_fxsw_leg_t sw_up, sw_dn;
} tr_fxv3_input_t;

typedef struct {
    bool on;
    double value;
    uint32_t rgb;
    int width;
} tr_fxv3_plot_t;

typedef struct {
    int session_no;
    double high, low;
    double pred[5];
    int dir[5];
    int reg_valid;
    double r2;
} tr_fxv3_frame_t;

typedef struct {
    int session_no;
    int64_t last_session_bar;
    int32_t session_bars;
    double prev_future;
    int prev_dir2;
    int32_t streak;
    int mem_session;
    int mem_valid;
    int mem_dir;
    double mem_price;
    double mem_tgt[5];
    double mem_up[5];
    double mem_dn[5];
    int pst_valid;
    int pst_dir;
    double pst_tgt[5];
    double pst_up[5];
    double pst_dn[5];
} tr_fxv3_snap_t;

typedef struct {
    tr_fxv3_config_t cfg;
    tr_fxv3_snap_t cur;
    tr_fxv3_snap_t snap;
    bool has_bar;
    tr_time_us_t last_open;
    tr_fxv3_frame_t hist[TR_FXV3_HIST];
    int32_t hist_n;
    tr_fxv3_plot_t plots[TR_FXV3_PLOT_N]; /* [k]=Plot k */
    double stage; /* 단계화_1분_통합 */
    double reg_flat;
} tr_fxv3_t;

void tr_fxv3_default_config(tr_fxv3_config_t *cfg, double price_scale);
bool tr_fxv3_init(tr_fxv3_t *s, const tr_fxv3_config_t *cfg);
void tr_fxv3_eval(tr_fxv3_t *s, const tr_fxv3_input_t *in);

/* 엔진이 부르는 1분봉 러너. 회귀·예측·곡선·일봉추세·갭을 계산한 뒤 표시부를 갱신한다. */
typedef struct tr_fxv3_run {
    tr_fxreg_t reg;
    tr_fxp2_t pred;
    tr_fxc_t curve;
    tr_fxtrend_t trend;
    tr_fxgap_t gap;
    tr_fxsw_t swing;
    double tp[20];
    double volw[20];
    int vwap_n;
    double vwap_before;
    double last_vwap;
    double mids[20];
    int mid_n;
    double center_before;
    double last_center;
    int64_t prev_key;
    bool has_key;
    int32_t session_bars;
    int64_t bar_i;
    bool has_bar;
    tr_time_us_t last_open;
    double price_scale;
    tr_fxv3_t view;
} tr_fxv3_run_t;

bool tr_fxv3_run_init(tr_fxv3_run_t *s, double price_scale);
void tr_fxv3_run_eval(tr_fxv3_run_t *s, int64_t date, int64_t time_hhmmss,
                      double open, double high, double low, double close, double volume,
                      tr_time_us_t bar_open);
bool tr_fxv3_run_relink(tr_fxv3_run_t *s);
const tr_fxv3_t *tr_fxv3_run_view(const tr_fxv3_run_t *s);

#endif
