#include "core/functions/fx_decision_v2.h"

#include <math.h>

tr_fxdec_out_t tr_fxdec_eval(const tr_fxdec_input_t *in) {
    tr_fxdec_out_t o;
    o.unified = 0;
    o.profile = 0;
    o.adx = 0;
    o.di = 0;
    if (in == 0) {
        return o;
    }
    int fut = in->future_dir > 0.0 ? 1 : (in->future_dir < 0.0 ? -1 : 0);
    double trend = fabs(in->adx_trend);
    double strong = fabs(in->adx_strong);
    if (trend < 1.0) {
        trend = 1.0;
    }
    if (strong < trend) {
        strong = trend;
    }
    if (in->profile_valid) {
        o.profile = in->state5 > 0 ? 1 : (in->state5 < 0 ? -1 : 0);
    }
    if (in->adx_valid) {
        o.di = in->plus_di > in->minus_di ? 1 : (in->plus_di < in->minus_di ? -1 : 0);
        if (fabs(in->adx) >= strong) {
            o.adx = 2;
        } else if (fabs(in->adx) >= trend) {
            o.adx = 1;
        }
    }
    int buy_ok = 0, sell_ok = 0;
    if (in->profile_valid) {
        if (in->mode == 1) {
            buy_ok = in->close > in->center;
            sell_ok = in->close < in->center;
        } else if (in->mode == 2) {
            buy_ok = in->state5 >= 1;
            sell_ok = in->state5 <= -1;
        } else {
            buy_ok = o.profile >= 0;
            sell_ok = o.profile <= 0;
        }
    }
    if (fut == 1) {
        o.unified = 50;
        if (buy_ok && o.di == 1 && o.adx >= 1) {
            o.unified = 100;
        }
    } else if (fut == -1) {
        o.unified = -50;
        if (sell_ok && o.di == -1 && o.adx >= 1) {
            o.unified = -100;
        }
    }
    return o;
}
