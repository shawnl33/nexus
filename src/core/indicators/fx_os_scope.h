#ifndef TR_FX_OS_SCOPE_H
#define TR_FX_OS_SCOPE_H

/* #우드스탁_해외선물1분통합판정V3 의 Plot1~7 과
 * #우드스탁_해외선물매물대압축지속V4 의 Plot1~4.
 * Plot6 진입후보는 매수 -110, 매도 +110.
 * 기본 입력은 두 원본의 Input 기본값.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/functions/fx_adx_v1.h"
#include "core/functions/fx_curve_v1.h"
#include "core/functions/fx_future_values_v1.h"
#include "core/functions/fx_session_profile_v2.h"
#include "core/functions/fx_squeeze_v1.h"
#include "core/indicators/fx_entry_cand.h"
#include "core/model/time_us.h"

typedef struct {
    int64_t date;
    int64_t time;
    tr_time_us_t bar_open;
    double high, low, close, volume;
    bool is_new_bar;
    const tr_fxfv_output_t *fv;
} tr_fxos_input_t;

typedef struct {
    int judge; /* 통합상태 */
    uint32_t judge_rgb;
    int fut, prof, di, adx;
    int sq_on;
    int sq_len;
    uint32_t sq_rgb;
    int sq_w;
    int hold;
    uint32_t hold_rgb;
    int ratio_on;
    double ratio;
    uint32_t ratio_rgb;
    int rel_on;
    int rel_len;
    uint32_t rel_rgb;
    int rel_w;
    int cf_on;
    int cf_len;
    uint32_t cf_rgb;
    int cf_w;
    int ent_on;
    int ent_y;
    uint32_t ent_rgb;
    int ent_w;
} tr_fxos_out_t;

typedef struct {
    double price_scale;
    tr_fxc_t curve;
    tr_fxadx_t adx;
    tr_fxprof_t session;
    tr_fxsq_t squeeze;
    tr_fxec_t entry;
    int64_t key;
    bool has_key;
    int reset_bar;
    int32_t session_bars;
    tr_fxos_out_t out;
} tr_fxos_t;

bool tr_fxos_init(tr_fxos_t *s, double price_scale);
void tr_fxos_eval(tr_fxos_t *s, const tr_fxos_input_t *in);
bool tr_fxos_relink(tr_fxos_t *s);

#endif
