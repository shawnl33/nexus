#ifndef TR_FX_ADX_V1_H
#define TR_FX_ADX_V1_H

/* WSF_FXADXV1 (원본 144줄). 세션초기화(또는 첫 봉)에서 리셋하는 Wilder ADX.
 * 일분봉여부 변수는 원본에서 계산만 하고 쓰이지 않는다. 리셋은 세션초기화 인자만 본다.
 * [1] 재귀는 봉 말 스냅샷으로 재현한다. 같은 봉을 다시 평가해도 한 봉분만 반영된다.
 * 기간은 Round(Abs(입력)), 하한 2. 유효는 계산봉수 >= 기간×2.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/model/time_us.h"

typedef struct {
    bool session_reset;
    double high, low, close;
    tr_time_us_t bar_open;
} tr_fxadx_input_t;

typedef struct {
    uint32_t bar_count;
    double prev_h, prev_l, prev_c;
    double sm_tr, sm_up_dm, sm_dn_dm;
    double dx_sum;
    double adx_state;
    bool valid_state;
} tr_fxadx_mem_t;

typedef struct {
    uint32_t period;
    tr_fxadx_mem_t st;
    tr_fxadx_mem_t prev;
    bool has_bar;
    tr_time_us_t last_open;
    double adx;
    double plus_di;
    double minus_di;
    bool valid;
} tr_fxadx_t;

bool tr_fxadx_init(tr_fxadx_t *s, int32_t period_input);
void tr_fxadx_eval(tr_fxadx_t *s, const tr_fxadx_input_t *in);

#endif
