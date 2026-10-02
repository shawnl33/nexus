#include "core/functions/fx_market_v1.h"

#include <math.h>
#include <string.h>

bool tr_fxmkt_init(tr_fxmkt_t *s, int32_t period, double band_mult) {
    if (s == 0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    if (period < 2) {
        period = 2;
    }
    if (period > TR_FXMKT_CAP) {
        period = TR_FXMKT_CAP;
    }
    s->period = (uint32_t)period;
    s->band_mult = fabs(band_mult);
    if (s->band_mult <= 0.0) {
        s->band_mult = 1.0;
    }
    return true;
}

static void recompute(tr_fxmkt_t *s) {
    s->center = 0.0;
    s->upper = 0.0;
    s->lower = 0.0;
    s->valid = false;
    double sum_v = 0.0, sum_pv = 0.0;
    uint32_t n = s->period;
    if ((uint32_t)s->count < n) {
        n = (uint32_t)s->count;
    }
    for (uint32_t j = 0; j < n; j++) {
        if (s->vol[j] > 0.0) {
            sum_v += s->vol[j];
            sum_pv += s->tp[j] * s->vol[j];
        }
    }
    if (sum_v <= 0.0) {
        return;
    }
    s->center = sum_pv / sum_v;
    double var_sum = 0.0;
    for (uint32_t j = 0; j < n; j++) {
        if (s->vol[j] > 0.0) {
            double d = s->tp[j] - s->center;
            var_sum += d * d * s->vol[j];
        }
    }
    double sd = sqrt(var_sum / sum_v);
    s->upper = s->center + sd * s->band_mult;
    s->lower = s->center - sd * s->band_mult;
    s->valid = true;
}

void tr_fxmkt_eval(tr_fxmkt_t *s, const tr_fxmkt_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    double tp = (in->high + in->low + in->close) / 3.0;
    bool is_new = !s->has_bar || in->bar_open != s->last_open;
    if (is_new) {
        for (int j = TR_FXMKT_CAP - 2; j >= 0; j--) {
            s->tp[j + 1] = s->tp[j];
            s->vol[j + 1] = s->vol[j];
        }
        if (s->count < TR_FXMKT_CAP) {
            s->count++;
        }
        s->last_open = in->bar_open;
        s->has_bar = true;
    }
    s->tp[0] = tp;
    s->vol[0] = in->volume;
    recompute(s);
}
