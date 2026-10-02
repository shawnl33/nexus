#ifndef TR_FX_GAP_V1_H
#define TR_FX_GAP_V1_H

/* WSF_FXGapV1 (원본 149줄). 1분봉 전용. 완성 세션은 07:00/15:30 경계.
 * 첫 로딩 세션이 그 시각이 아니면 완성으로 저장하지 않는다.
 * 이후 세션 리셋은 항상 새 완성 구간의 시작으로 본다.
 * 갭비율 = |현재 세션 시가 − 직전 완성 종가| / 최근 n개 완성 TR 평균.
 * n은 변동기간을 5~10으로 클램프. 등급 0/1/2 → 비중 1/0.5/0.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/model/time_us.h"

typedef struct {
    int32_t volatility_period; /* 5~10 */
    double mid_gap;
    double big_gap;
} tr_fxgap_config_t;

typedef struct {
    bool session_reset;
    int64_t bar_index;
    int64_t time_hhmmss; /* stime */
    double open, high, low, close;
    tr_time_us_t bar_open; /* 같은 봉 재평가 구분. 완성 저장은 bar_index 게이트 */
} tr_fxgap_input_t;

typedef struct {
    tr_fxgap_config_t cfg;
    bool initialized;
    bool full_start;
    double sess_open, sess_high, sess_low, sess_close;
    int64_t sess_start_hhmmss;
    int64_t last_start_bar;
    double prev_close;
    double prev_prev_close;
    double trs[20];
    int32_t tr_count;
    int32_t completed;
    double gap_ratio;
    int gap_grade;
    double daily_weight;
    int32_t elapsed_min;
    int gap_dir;
    bool valid;
} tr_fxgap_t;

void tr_fxgap_init(tr_fxgap_t *s, const tr_fxgap_config_t *cfg);
void tr_fxgap_eval(tr_fxgap_t *s, const tr_fxgap_input_t *in);

#endif
