#include "core/functions/fx_trend_v1.h"

#include <math.h>
#include <string.h>

#include "core/functions/fx_trend_state_v1.h"

static int32_t clamp_n(int32_t n) {
    if (n < 5) {
        return 5;
    }
    if (n > 100) {
        return 100;
    }
    return n;
}

void tr_fxtrend_init(tr_fxtrend_t *s, const tr_fxtrend_config_t *cfg) {
    if (s == 0) {
        return;
    }
    memset(s, 0, sizeof(*s));
    if (cfg != 0) {
        s->cfg = *cfg;
    }
    s->cfg.reg_period = clamp_n(s->cfg.reg_period);
}

static void shift_mid(tr_fxtrend_t *s, double mid) {
    for (int j = 98; j >= 0; j--) {
        s->mids[j + 1] = s->mids[j];
    }
    s->mids[0] = mid;
    if (s->completed < 100) {
        s->completed++;
    }
}

static void publish(tr_fxtrend_t *s, const tr_fxtrend_input_t *in) {
    int n = s->cfg.reg_period;
    s->line = in->close;
    s->slope = 0.0;
    s->r2 = 0.0;
    s->residual = 0.0;
    s->link_valid = 0;
    if (s->completed >= n) {
        double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0, sum_y2 = 0;
        for (int j = 0; j < n; j++) {
            double x = (double)j;
            double y = s->mids[n - 1 - j];
            sum_x += x;
            sum_y += y;
            sum_xy += x * y;
            sum_x2 += x * x;
            sum_y2 += y * y;
        }
        double den = (double)n * sum_x2 - sum_x * sum_x;
        if (den != 0.0) {
            double slope = ((double)n * sum_xy - sum_x * sum_y) / den;
            double b = (sum_y * sum_x2 - sum_x * sum_xy) / den;
            double last = slope * (double)(n - 1) + b;
            s->line = last + slope;
            s->slope = slope;
            double r2_num = (double)n * sum_xy - sum_x * sum_y;
            double r2_den = sqrt(fmax(0.0, ((double)n * sum_x2 - sum_x * sum_x) *
                                              ((double)n * sum_y2 - sum_y * sum_y)));
            if (r2_den > 0.0) {
                double r = r2_num / r2_den;
                s->r2 = fmin(1.0, fmax(0.0, r * r));
            }
            double sst = sum_y2 - sum_y * sum_y / (double)n;
            double sse = fmax(0.0, sst * (1.0 - s->r2));
            if (n > 2) {
                s->residual = sqrt(sse / (double)(n - 2));
            }
            s->link_valid = 1;
        }
    }
    tr_fxtrend_state_input_t tin = {
        .line = s->line,
        .slope = s->slope,
        .reg_valid = s->link_valid,
        .r2 = s->r2,
        .residual = s->residual,
        .price = in->close,
        .min_r2 = in->min_r2,
        .price_scale = in->price_scale,
    };
    tr_fxtrend_state_output_t tout;
    tr_fxtrend_state_eval(&tin, &tout);
    s->dir = tout.dir;
    s->state = tout.state;
    s->strength = tout.strength;
    s->valid = tout.valid;
}

void tr_fxtrend_eval(tr_fxtrend_t *s, const tr_fxtrend_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    if (!s->initialized) {
        s->sess_high = in->high;
        s->sess_low = in->low;
        s->full_start = in->time_hhmmss == 70000 || in->time_hhmmss == 153000;
        s->last_start_bar = in->bar_index;
        s->initialized = true;
    } else if (in->session_reset && in->bar_index != s->last_start_bar) {
        if (s->full_start) {
            shift_mid(s, (s->sess_high + s->sess_low) / 2.0);
        }
        s->sess_high = in->high;
        s->sess_low = in->low;
        s->full_start = true;
        s->last_start_bar = in->bar_index;
    } else {
        if (in->high > s->sess_high) {
            s->sess_high = in->high;
        }
        if (in->low < s->sess_low) {
            s->sess_low = in->low;
        }
    }
    publish(s, in);
}
