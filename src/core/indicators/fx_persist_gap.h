#ifndef TR_FX_PERSIST_GAP_H
#define TR_FX_PERSIST_GAP_H

/* #WSF_해외선물지속목표차삼선V1 / 오선V1.
 * n_lines=3이면 지속목표 1~3, 4면 1·5·15·30분 평탄회귀, 5면 지속목표 1~5.
 * 폭 = 최고−최저. Plot5 색의 기준선은 마지막 줄이다.
 * 세션(07:00/15:30)이 바뀌면 최고폭을 직전 구간 최고로 넘기고 다시 센다.
 * 첫 봉은 직전 구간 최고를 만들지 않는다. 같은 봉 재평가는 봉수를 한 번만 올린다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/model/time_us.h"

typedef struct {
    int n_lines;       /* 3 또는 5 */
    double emphasize;  /* 비율강조기준, 기본 25 */
} tr_fxpgap_config_t;

typedef struct {
    bool session_reset;
    tr_time_us_t bar_open;
    double high, low, close;
    double target[5]; /* 지속저장목표1~5. 없는 칸은 0 */
} tr_fxpgap_input_t;

typedef struct {
    int ready;
    double gap, peak, prev_peak, ratio, prev_ratio, cnt;
    int plot3, plot5;
    uint32_t rgb4, rgb5;
    int width4;
    double hi, lo;          /* 이번 봉 기준선 최고·최저 */
    uint32_t union_rgb;     /* 통합 지표의 비율 색 */
    int union_w;            /* 통합 지표의 비율 두께 */
} tr_fxpgap_out_t;

typedef struct {
    tr_fxpgap_config_t cfg;
    double peak, prev_peak, sess_low, gap, cnt;
    int ready;
    bool has_bar;
    tr_time_us_t last_open;
    /* 직전 봉 말. 같은 봉 재평가의 출발점 */
    double snap_peak, snap_prev_peak, snap_sess_low, snap_gap, snap_cnt;
    int snap_ready;
    bool snap_has;
    int below, above; /* 종가가 목표 최고 아래 / 최저 위에 있었는지 */
    int snap_below, snap_above;
    double snap_ratio;
    tr_fxpgap_out_t out;
} tr_fxpgap_t;

void tr_fxpgap_init(tr_fxpgap_t *s, int n_lines, double emphasize);
void tr_fxpgap_eval(tr_fxpgap_t *s, const tr_fxpgap_input_t *in);

/* 통합 지표의 삼선·오선 비율색. cross=1이면 평탄·마켓 비율이 같이 좁을 때 굵기 1,
 * 비율 25 미만이고 봉 전체가 목표 밖이면 두께 3. */
void tr_fxunion_paint(tr_fxpgap_t *s, int cross, int other_a_ready, double other_a_ratio,
                      int other_b_ready, double other_b_ratio, double high, double low);

#endif
