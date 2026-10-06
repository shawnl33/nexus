#ifndef TR_KS_SCORE_V1_H
#define TR_KS_SCORE_V1_H

/* 국내선물 1분 Data2 점수 블록.
 * reference/yeslanguage/signals/#우드스탁_위클리_페어시스템_양매수.txt 106~681줄.
 * 양매도 시그널, 위클리 합산수익률·프라이스링크 4개, 국내선물 스나이퍼 Data2의
 * 실행문은 같다. 15초 점수는 없다.
 *
 * 한 상태가 WSF_KSValues1mV1, 세션 키, 5/15/30분 WSF_KSSynthetic1mV1을 갖고
 * 삼선·오선·평탄회귀차·마켓중심차·삼선구간이탈·점수 가산까지 계산한다.
 * 다섯 곳의 WSF_KSSession1mV1은 같은 BDate/DayIndex/CurrentBar라 키를 한 번만 구한다.
 * 진입은 삼선 비율, 청산은 두 구간 가격비율을 본다.
 * 같은 봉 재평가는 직전 봉 말에서 다시 계산한다. 값 대입은 그 봉의 [1]을 바꾸지 않는다.
 * 화면의 페어 시뮬(tr_wpair_replay)만 이 함수를 호출한다. 실주문은 없다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/functions/ks_session_v1.h"
#include "core/functions/ks_synthetic_v1.h"
#include "core/functions/ks_values_v1.h"

typedef struct {
    tr_ksv_config_t values; /* tf는 TR_KS_TF_1M. 마켓기간은 합성 마켓기간과 같다 */
    int32_t reg_period_5;   /* 회귀기간5분, 시그널 기본 18 */
    int32_t reg_period_15;  /* 회귀기간15분, 시그널 기본 10 */
    int32_t reg_period_30;  /* 회귀기간30분, 시그널 기본 6 */
    int32_t time_basis;     /* 합성봉시각기준 0 또는 1 */
    int include_mkt_30;     /* 마켓중심30분포함 0 또는 1 */
    double slack_ticks;     /* 이탈여유틱 */
} tr_ksscore_config_t;

typedef struct {
    int64_t bdate;
    int32_t day_index;
    int32_t current_bar;   /* Data2 CurrentBar. 1부터 */
    tr_time_us_t bar_open; /* 새 봉 판정 */
    int64_t cur_time;      /* Data2 sTime, HHMMSS */
    double high, low, close, volume;
} tr_ksscore_input_t;

typedef struct {
    int calc_ready; /* 함수계산가능. 1분 게이트를 init이 고정하므로 1 */
    int64_t session_key;
    int session_reset;

    double stage_score; /* 단계화_1분_통합 */
    double reg_flat;     /* 곡선회귀선_평탄 */
    double market_center;
    double target[5]; /* 지속저장목표1~5 */
    double lup_high, lup_low, lup_382, lup_500, lup_618;
    double ldn_high, ldn_low, ldn_382, ldn_500, ldn_618;

    int three_ready; /* 삼선_목표준비 */
    double three_gap, three_peak, three_prev_peak, three_ratio, three_cnt;
    double three_hi, three_lo, three_range_low;
    int three_below, three_above; /* 1이면 종가가 그 쪽에 있었던 기록 */
    uint32_t three_rgb;
    int three_width; /* 삼선_표시굵기 */

    int five_ready;
    double five_gap, five_peak, five_prev_peak, five_ratio, five_cnt;
    double five_hi, five_lo, five_range_low;
    int five_below, five_above;
    uint32_t five_rgb;

    double reg_px[3]; /* 평탄회귀 5/15/30분 */
    double mkt_px[3];
    int reg_valid[3];
    int mkt_valid[3];

    int reg_ready; /* 기준선준비 */
    double reg_gap, reg_peak, reg_prev_peak, reg_ratio, reg_cnt;
    double reg_hi, reg_lo, reg_range_low;

    int mkt_ready;
    double mkt_gap, mkt_peak, mkt_prev_peak, mkt_ratio, mkt_cnt;
    double mkt_hi, mkt_lo, mkt_range_low;

    int break_ready; /* 이탈_삼선준비 */
    double tgt_hi, tgt_lo, tgt_width;
    double seg_cnt, prev_seg_cnt, compare_bars;
    double cur_hi, cur_lo, cur_range;
    double prev_hi, prev_lo, prev_range;
    int prev_valid;
    double two_hi, two_lo, two_range, two_max, price_ratio;
    int seg_changed, broke, pos, first_break;
    double slack;

    int compound; /* 이번 봉 복합압축조건 */
    int px_cond;  /* 이번 봉 가격압축이탈조건. 부호 있는 위치 */
    int up_dot, dn_dot;
    uint32_t dot_rgb;
    int score; /* 신호합계점수. 양수는 아래, 음수는 위 */
    int score_ex;
    int compound_show; /* 직전 봉 복합압축 */
    int ratio_score;   /* 삼선비율점수 0~3 */
    uint32_t score_rgb;
} tr_ksscore_out_t;

typedef struct {
    double peak, prev_peak, range_low, gap, ratio, cnt;
    double hi, lo;
    int ready, below, above;
    uint32_t rgb;
    int width;
} tr_ksscore_lane_t;

typedef struct {
    int ready;
    double width, cnt, prev_cnt;
    double cur_hi, cur_lo, prev_hi, prev_lo;
    int prev_valid, broke;
    double two_max;
    int first_break, compound, px_cond;
} tr_ksscore_brk_t;

typedef struct {
    int64_t session_key;
    tr_ksscore_lane_t three, five, reg, mkt;
    tr_ksscore_brk_t brk;
} tr_ksscore_series_t;

typedef struct {
    tr_ksscore_config_t cfg;
    tr_ksv_t values;
    tr_ks_session_t session; /* 점수 블록의 세션 키. 값·합성과는 별도 상태 */
    tr_kssyn_t syn[3];       /* 5, 15, 30분 */
    tr_ksscore_series_t series;
    tr_ksscore_series_t snap; /* 직전 봉 말 */
    bool has_bar;
    tr_time_us_t last_open;
    bool has_prev_bar;
    int64_t prev_time;
    double prev_h, prev_l, prev_c, prev_v;
    tr_ksscore_out_t out;
} tr_ksscore_t;

/* 시그널 Var 기본값. price_scale만 호출부가 넣는다. */
void tr_ksscore_default_config(tr_ksscore_config_t *cfg, double price_scale);

bool tr_ksscore_init(tr_ksscore_t *s, const tr_ksscore_config_t *cfg);
void tr_ksscore_eval(tr_ksscore_t *s, const tr_ksscore_input_t *in);
bool tr_ksscore_relink(tr_ksscore_t *s);

#endif
