#ifndef TR_FX_CURVE_OS_H
#define TR_FX_CURVE_OS_H

/* #우드스탁_미래곡선_해외선물. FutureValuesV3_CO(스윙연결 0, 기세무시틱 1)의
 * 18값과 합성 5/15/30분 기준선. 지속목표 색과 지난구간 구조색은 이 파일의 규칙.
 * 합성봉시각기준은 0이다. 엔진이 넘기는 시각이 봉 시작이라, HTS 종료시각 기준 1과
 * 같은 버킷이 된다. Plot25·26 진입후보는 EntryCandV15가 없어 끈다.
 */

#include "core/indicators/fx_entry_cand.h"
#include "core/indicators/fx_mirae_v1.h"

#define TR_FXCU_PLOTS 28

typedef struct {
    double dn_lo, up_lo, dn_hi, up_hi;
    double mem_dn_lo, mem_up_lo, mem_dn_hi, mem_up_hi;
    int dn_lo_up, up_lo_up, dn_hi_up, up_hi_up;
} tr_fxcu_mem_t;

typedef struct {
    tr_fxmirae_t base;
    tr_fxec_t entry;
    tr_fxcu_mem_t st;
    tr_fxcu_mem_t prev;
    bool has_bar;
    tr_time_us_t last_open;
    tr_fxmirae_plot_t plots[TR_FXCU_PLOTS];
} tr_fxcu_t;

bool tr_fxcu_init(tr_fxcu_t *s, double price_scale);
void tr_fxcu_eval(tr_fxcu_t *s, const tr_fxmirae_input_t *in, const tr_fxsyn_t *syn5,
                  const tr_fxsyn_t *syn15, const tr_fxsyn_t *syn30);
bool tr_fxcu_relink(tr_fxcu_t *s);

#endif
