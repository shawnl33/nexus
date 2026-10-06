#ifndef TR_KS_VALUES_V1_H
#define TR_KS_VALUES_V1_H

/* WSF_KSValues1mV1 / WSF_KSValues15V1
 * (reference/yeslanguage/functions/WSF_KSValues1mV1.txt, 360줄).
 *
 * 국내선물 1분·15초 핵심 18값. 본문은 WSF_FXFutureValuesV1과 같고,
 * 세션 키만 WSF_KSSession* 이고 하부 호출 이름만 KS다.
 * 회귀·예측·곡선·스윙 수식은 해외 모듈과 같다 (변수 n이 회귀표본수로 바뀌었을 뿐).
 * 1분봉(DataCompress 2, BarInterval 1)과 15초봉(DataCompress 1, BarInterval 15)은
 * 회귀기간이 둘 다 30이라 같은 하부 모듈을 그 주기로 초기화한다.
 *
 * - 세션: CurrentBar==1 또는 BDate/DayIndex 경계에서 리셋 (원본 151~157줄).
 * - 호출 순서: KSReg #1 → KSPredict → 평탄/지속저장/마켓VWAP →
 *   KSCurve → KSReg #2 → 핵심마켓 → 단계화 → KSSwing.
 * - 실행 게이트는 init의 tf로 고정한다. 게이트 밖 주기는 init이 거부한다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/functions/fx_curve_v1.h"
#include "core/functions/fx_predict_v2.h"
#include "core/functions/fx_reg_v1.h"
#include "core/functions/fx_swing_v1.h"
#include "core/functions/ks_session_v1.h"
#include "core/market/ring.h"
#include "core/model/time_us.h"

typedef enum {
    TR_KS_TF_1M = 0,  /* DataCompress 2, BarInterval 1 */
    TR_KS_TF_15S = 1  /* DataCompress 1, BarInterval 15 */
} tr_ks_tf_t;

typedef struct {
    tr_ks_tf_t tf;
    int32_t predict_ticks;   /* 예측변수 */
    int32_t predict_bars[5]; /* 예측봉수1~5 */
    double min_r2;           /* 최소신뢰도 */
    int32_t persist_bars;    /* 지속봉수 */
    int32_t market_period;   /* 마켓계산기간 */
    int32_t min_hold_bars;   /* 최소전환유지봉수 */
    double price_scale;      /* PriceScale (>0) */
} tr_ksv_config_t;

typedef struct {
    int64_t bdate;         /* BDate */
    int32_t day_index;     /* DayIndex */
    int32_t current_bar;   /* CurrentBar */
    tr_time_us_t bar_open; /* 새 봉 판정 (Index 대응) */
    double high, low, close;
    double volume;
} tr_ksv_input_t;

typedef struct {
    double score;             /* 단계화_1분_통합 */
    double reg_flat;          /* 곡선회귀선_평탄 */
    double market_center;     /* 마켓중심가격 */
    double persist_target[5]; /* 지속저장목표1~5 */
    double lup_high, lup_low, lup_382, lup_500, lup_618;
    double ldn_high, ldn_low, ldn_382, ldn_500, ldn_618;
} tr_ksv_output_t;

typedef struct {
    double h, l, c, v;
} tr_ksv_bar_t;

typedef struct {
    int64_t key;
    int32_t session_bars;
    double persist_streak;
    int persist_valid;
    int persist_dir;
    double persist_target[5];
    int prev_pred_dir2;
    double prev_future_dir;
    double prev_core_mkt;
} tr_ksv_series_t;

typedef struct {
    tr_ksv_config_t cfg;
    tr_fxreg_t reg_pred;
    tr_fxreg_t reg_core;
    tr_fxp2_t predict;
    tr_fxc_t curve;
    tr_fxsw_t swing;
    tr_ks_session_t session;
    tr_ksv_series_t series;
    tr_ksv_series_t prev_bar;
    bool has_bar;
    tr_time_us_t last_bar_open;
    bool session_reset;
    int32_t session_bars;
    bool is_new_bar;
    tr_ksv_bar_t bar_win_buf[100];
    tr_ring bar_win;
    double state_reg_flat;
    double state_mkt_center;
    bool mkt_valid;
    tr_ksv_output_t out;
} tr_ksv_t;

bool tr_ksv_init(tr_ksv_t *s, const tr_ksv_config_t *cfg);
void tr_ksv_eval(tr_ksv_t *s, const tr_ksv_input_t *in);
bool tr_ksv_relink(tr_ksv_t *s);

#endif
