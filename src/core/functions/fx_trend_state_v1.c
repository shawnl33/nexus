#include "core/functions/fx_trend_state_v1.h"

#include <math.h>
#include <string.h>

void tr_fxtrend_state_eval(const tr_fxtrend_state_input_t *in, tr_fxtrend_state_output_t *out) {
    if (out == 0) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (in == 0) {
        return;
    }
    double base_res = fmax(in->price_scale, in->residual);
    double min_slope = fmax(in->price_scale, base_res * 0.02);
    if (in->reg_valid != 1 || in->r2 < in->min_r2 || fabs(in->slope) < min_slope || base_res <= 0.0) {
        return;
    }
    out->valid = 1;
    double slope_ratio = fabs(in->slope) / base_res;
    double slope_str = fmin(1.0, slope_ratio * 5.0);
    double r2_str = fmin(1.0, fmax(0.0, in->r2));
    out->strength = (r2_str * 0.60 + slope_str * 0.40) * 100.0;
    if (in->slope > 0.0) {
        out->dir = 1;
        if (in->price >= in->line) {
            out->state = 2;
        } else {
            out->state = 1;
            out->strength *= 0.65;
        }
    } else {
        out->dir = -1;
        if (in->price <= in->line) {
            out->state = -2;
        } else {
            out->state = -1;
            out->strength *= 0.65;
        }
    }
}
