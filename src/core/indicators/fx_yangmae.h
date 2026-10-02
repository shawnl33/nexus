#ifndef TR_FX_YANGMAE_H
#define TR_FX_YANGMAE_H

/* 삼선구간이탈 + 양매수 가설 V1(가설1·2)과 V2(가설3·4).
 * 삼선 목표 폭이 유지되는 동안 고저를 모으고, 폭이 바뀌면 직전 구간으로 고정한다.
 * 가설1: 삼선 굵기>0, 두 구간 가격비율<40, 회귀·마켓 비율<30. 켜진 첫 봉.
 * 가설2: 그 다음 확인대기봉수 안에 두 구간 고저를 여유 틱만큼 이탈.
 * 가설3: 가설1이면서 현재 구간 평균거래량/직전 < 100.
 * 가설4: 가설2이면서 이탈 봉 거래량/확인 때 평균 > 100.
 * 표시는 한 봉 늦은 [1]이다. 같은 봉 재평가는 직전 봉 말에서 다시 계산한다.
 * 가격거래량압축도 같은 구간 합으로 낸다. 가격 비율은 두 구간 범위/세션 최대,
 * 거래량 비율은 현재 구간 평균/직전 구간 평균이다. 둘 다 기준 아래면 동시압축.
 * 세 값 모두 다음 봉에 그린다. 세션 첫 봉은 그리지 않는다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/model/time_us.h"

typedef struct {
    double price_scale;
    double slack_ticks;     /* 이탈여유틱, 기본 0 */
    double price_limit;     /* 가격압축기준 40 */
    double reg_limit;       /* 회귀압축기준 30 */
    double mkt_limit;       /* 마켓압축기준 30 */
    int32_t wait_bars;      /* 확인대기봉수 10 */
    double vol_drop_limit;  /* 거래감소기준 100 */
    double vol_rise_limit;  /* 거래증가기준 100 */
    int32_t vol_min_bars;   /* 거래량최소봉수 1 */
    double mark_ticks;      /* 표시간격틱 3 */
} tr_fxymae_config_t;

typedef struct {
    bool session_reset;
    bool targets_ready; /* 지속목표 1~3이 0이 아님 */
    tr_time_us_t bar_open;
    double high, low, close, volume;
    double t1, t2, t3;
    int union_w;        /* 삼선 표시굵기 */
    int reg_ready, mkt_ready;
    double reg_ratio, mkt_ratio;
} tr_fxymae_input_t;

typedef struct {
    int ready;
    int pos;            /* 직전 범위 대비 현재 위치 1/0/−1 */
    int first_break;    /* 이번 봉 첫 이탈 */
    int show_first;     /* 직전 봉 첫 이탈. 세션 첫 봉·목표 미준비는 0 */
    int prev_valid;
    double prev_hi, prev_lo, two_hi, two_lo;
    double cnt, prev_cnt;
    int h1, h2, h3, h4;           /* 이번 봉 발생. h2/h4는 부호 */
    int show_h1, show_h2, show_h3, show_h4; /* 직전 봉 발생, 화면에 그릴 값 */
    double mark_h1, mark_h2, mark_h3, mark_h4; /* 0이면 표시 안 함 */
    /* 가격거래량압축. 세션 첫 봉은 0. 값은 직전 봉이다. */
    int show_price, show_vol, show_both;
    double show_price_ratio, show_vol_ratio;
} tr_fxymae_out_t;

typedef struct {
    tr_fxymae_config_t cfg;
    double cnt, prev_cnt, cur_hi, cur_lo, prev_hi, prev_lo, width;
    int prev_valid, broke, ready;
    double two_max;
    double vol_cur, vol_prev, vol_base;
    int h2_wait, h2_elapsed;
    double h2_up, h2_dn;
    int h1, h2, h3, h4;
    int first; /* 이번 봉 첫 이탈. 다음 봉 show_first 가 된다 */
    int cond; /* 가설1 조건. 발생[1]과 구분 */
    double prev_low, prev_high;
    bool has_bar;
    tr_time_us_t last_open;
    /* 직전 봉 말 */
    double s_cnt, s_prev_cnt, s_cur_hi, s_cur_lo, s_prev_hi, s_prev_lo, s_width;
    int s_prev_valid, s_broke, s_ready;
    double s_two_max, s_vol_cur, s_vol_prev, s_vol_base;
    int s_h2_wait, s_h2_elapsed;
    double s_h2_up, s_h2_dn;
    int s_h1, s_h2, s_h3, s_h4, s_first, s_cond;
    double s_low, s_high;
    int s_pvc_price_on, s_pvc_vol_on, s_pvc_both;
    double s_pvc_price_ratio, s_pvc_vol_ratio;
    bool s_has;
    /* 이번 봉이 세션 첫 봉인지. 스냅샷에 넣지 않는다.
     * 같은 봉 재평가에서 세션 플래그가 내려가도 표시를 숨긴다. */
    int bar_reset;
    int pvc_price_on, pvc_vol_on, pvc_both;
    double pvc_price_ratio, pvc_vol_ratio;
    tr_fxymae_out_t out;
} tr_fxymae_t;

void tr_fxymae_default_config(tr_fxymae_config_t *cfg, double price_scale);
void tr_fxymae_init(tr_fxymae_t *s, const tr_fxymae_config_t *cfg);
void tr_fxymae_eval(tr_fxymae_t *s, const tr_fxymae_input_t *in);

#endif
