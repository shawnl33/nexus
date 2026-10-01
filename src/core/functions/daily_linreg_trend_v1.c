#include "core/functions/daily_linreg_trend_v1.h"

#include <math.h>
#include <string.h>

void tr_dlrt1_eval(const tr_dlrt1_input_t *in, tr_dlrt1_output_t *out) {
    memset(out, 0, sizeof(*out));
    if (in == 0) {
        return;
    }
    double base_resid = fmax(in->price_scale, in->residual);
    double min_slope = fmax(in->price_scale, base_resid * 0.02);

    if (in->reg_valid && in->r2 >= in->min_r2 && fabs(in->slope) >= min_slope) {
        out->valid = true;
        double slope_ratio = fabs(in->slope) / base_resid;
        double slope_strength = fmin(1.0, slope_ratio * 5.0);
        double conf_strength = fmin(1.0, fmax(0.0, in->r2));
        double strength = (conf_strength * 0.60 + slope_strength * 0.40) * 100.0;

        if (in->slope > 0.0) {
            out->dir = 1;
            if (in->price >= in->line) {
                out->state = 2;
            } else {
                out->state = 1;
                strength *= 0.65;
            }
        } else {
            out->dir = -1;
            if (in->price <= in->line) {
                out->state = -2;
            } else {
                out->state = -1;
                strength *= 0.65;
            }
        }
        out->strength = strength;
    }
}
