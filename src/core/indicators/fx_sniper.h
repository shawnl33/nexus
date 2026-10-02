#ifndef TR_FX_SNIPER_H
#define TR_FX_SNIPER_H

/* 압축첫이탈 + 삼선비율점수 + 스나이퍼점수 + 점수통합.
 * 스나이퍼스코프 V1~V3는 이 점수들의 표시 배치만 다르다.
 * 점수 부호: 양수는 하단(하락 쪽) 신호, 음수는 상단(상승 쪽) 신호.
 * 첫 이탈 표시는 직전 봉 결과다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/model/time_us.h"

#define TR_FXSNIPER_HIST 100

typedef struct {
    double price_scale;
    int32_t n_targets;          /* 목표선수 3 또는 5 */
    double target_limit;       /* 목표압축기준 25 */
    double price_limit;        /* 가격압축기준 30 */
    int32_t range_period;      /* 가격범위기간 10 */
    int32_t atr_period;        /* 평균변동기간 20 */
    double target_atr_cap;     /* 2 */
    double price_atr_cap;      /* 3 */
    int32_t hold_bars;         /* 압축유지봉수 3 */
    int32_t wait_bars;         /* 이탈대기봉수 10 */
    double slack_ticks;        /* 이탈여유틱 1 */
} tr_fxsniper_config_t;

typedef struct {
    bool session_reset;
    tr_time_us_t bar_open;
    double high, low, close;
    double target[5];
    int break_ready;           /* 삼선 목표 준비 + 두구간 최대범위 */
    int prev_range_valid;
    int pos;                   /* 이탈 현재위치 */
    int first_break;           /* 이번 봉 첫 이탈 */
    double price_ratio;        /* 두구간가격비율 */
    int three_ready, reg_ready, mkt_ready;
    double three_ratio, reg_ratio, mkt_ratio;
    int below, above;          /* 삼선 최고아래/최저위 기록. 0이면 아직 그 쪽 */
} tr_fxsniper_input_t;

typedef struct {
    int calc_ready;
    int compressed;
    int stage;                 /* 0/1/2 또는 첫이탈*3 */
    int first_break;
    double target_ratio, price_ratio;
    int score;                 /* 스코프 가산 포함 -4~4 근처 */
    int score_ex;              /* 스코프 제외 -3~3 */
    int ratio_score;           /* 0~3 */
    int compound;              /* 복합압축 직전 봉 */
    uint32_t rgb;
    int px_exit;               /* 가격압축이탈 직전 봉. 세션 첫 봉은 0 */
    int below, above;          /* 삼선 최고아래/최저위 기록. 0이면 아직 그 쪽 */
    int session_reset;
} tr_fxsniper_out_t;

typedef struct {
    tr_fxsniper_config_t cfg;
    double hi[TR_FXSNIPER_HIST], lo[TR_FXSNIPER_HIST], cl[TR_FXSNIPER_HIST];
    int n;
    int32_t session_bars;
    double target_max, price_max, prev_target_width;
    int streak, waiting, waited;
    double save_hi, save_lo;
    int first_break, prev_break, prev_compound, prev_cond, prev_ym_first;
    int px_exit;
    bool has_bar;
    tr_time_us_t last_open;
    int s_session_bars, s_streak, s_waiting, s_waited, s_first, s_prev_break, s_prev_compound, s_prev_cond, s_prev_ym, s_n;
    int s_px_exit;
    double s_target_max, s_price_max, s_prev_width, s_save_hi, s_save_lo;
    bool s_has;
    tr_fxsniper_out_t out;
} tr_fxsniper_t;

void tr_fxsniper_default_config(tr_fxsniper_config_t *cfg, double price_scale);
void tr_fxsniper_init(tr_fxsniper_t *s, const tr_fxsniper_config_t *cfg);
void tr_fxsniper_eval(tr_fxsniper_t *s, const tr_fxsniper_input_t *in);

#endif
