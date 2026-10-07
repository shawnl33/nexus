#ifndef TR_FX_SQUEEZE_V1_H
#define TR_FX_SQUEEZE_V1_H

/* WSF_FXSqueezeV1_CO (원본 108줄). 최근 N봉 매물대 폭이 최근 비율창 최대보다
 * 좁음기준% 미만으로 이어진 봉 수와, 단계1 이상 압축이 풀리는 해제·지연확정.
 * 매물대는 내부의 SessionProfileV2(계산기간=폭계산봉수)다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/functions/fx_session_profile_v2.h"

#define TR_FXSQ_CAP 256

typedef struct {
    bool session_reset;
    bool is_new_bar;
    tr_time_us_t bar_open;
    double high, low, close, volume;
    double price_scale;
    double value_mult;
    int32_t min_bars;
    int32_t width_bars;   /* 폭계산봉수 */
    int32_t ratio_bars;   /* 비율기준봉수 */
    double narrow_pct;    /* 좁음기준 */
    int32_t stage1_bars;
    int32_t confirm_bars; /* 방향확정봉수 */
    int confirm_closed;   /* 확정봉판정 1=직전 완료봉 */
    double break_ticks;  /* 돌파여유틱. V4 박스. 0이면 여유 없음 */
    int32_t rearm_bars;   /* 재돌파허용봉수. 0이면 재무장 없음 */
} tr_fxsq_input_t;

typedef struct {
    int hold;
    double ratio;
    int narrow;
    int release;
    int release_dir;
    int release_len;
    int confirm_dir;
    int confirm_len;
    double band_hi, band_lo;
    int profile_valid;
    int box_dir;
    int box_len;
    double box_hi, box_lo;
} tr_fxsq_out_t;

typedef struct {
    int32_t hold, hold_1;
    int narrow;
    int reset, reset_1;
    double band_hi, band_lo, band_hi_1, band_lo_1;
    double close;
    int wait_on;
    double wait_hi, wait_lo;
    int32_t wait_age, wait_len;
    int box_on, box_done, box_age, box_len;
    double box_hi, box_lo;
    int rearm_on, rearm_age, rearm_len;
    double rearm_hi, rearm_lo;
    int brk_dir, brk_len;
    double brk_hi, brk_lo;
    int n;
    double width[TR_FXSQ_CAP];
    int32_t bars;
} tr_fxsq_mem_t;

typedef struct {
    tr_fxprof_t prof;
    tr_fxsq_mem_t st;
    tr_fxsq_mem_t prev;
    bool has_bar;
    tr_time_us_t last_open;
    int evals;
    tr_fxsq_out_t out;
} tr_fxsq_t;

void tr_fxsq_init(tr_fxsq_t *s);
void tr_fxsq_eval(tr_fxsq_t *s, const tr_fxsq_input_t *in);

#endif
