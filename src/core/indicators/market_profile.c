#include "core/indicators/market_profile.h"

#include <math.h>
#include <string.h>

bool tr_market_init(tr_market_t *s, uint32_t period, double band_mult, double price_scale,
                    tr_candle_t *storage, size_t capacity) {
    if (s == 0 || period == 0 || storage == 0 || capacity < period) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->period = period;
    s->band_mult = band_mult;
    s->price_scale = price_scale;
    return tr_ring_init(&s->hist, storage, sizeof(tr_candle_t), capacity);
}

void tr_market_on_bar(tr_market_t *s, const tr_candle_t *bar, bool session_first, double flat_reg_line) {
    if (s == 0 || bar == 0) {
        return;
    }
    if (!s->has_bar || session_first) {
        s->session_bars = 1;
        s->has_bar = true;
    } else {
        s->session_bars++;
    }
    tr_ring_push(&s->hist, bar);

    uint32_t calc = s->session_bars < s->period ? s->session_bars : s->period;
    double prev_center = s->center;

    double sum_v = 0.0, sum_pv = 0.0;
    for (uint32_t j = 0; j < calc; j++) {
        tr_candle_t b;
        if (!tr_ring_at(&s->hist, j, &b)) {
            break;
        }
        double vol = (double)b.volume;
        if (vol > 0.0) {
            double tp = ((double)b.high + (double)b.low + (double)b.close) / 3.0;
            sum_v += vol;
            sum_pv += tp * vol;
        }
    }

    s->valid = false;
    if (sum_v > 0.0 && calc >= 2) {
        s->center = sum_pv / sum_v;
        double var_sum = 0.0;
        for (uint32_t j = 0; j < calc; j++) {
            tr_candle_t b;
            if (!tr_ring_at(&s->hist, j, &b)) {
                break;
            }
            double vol = (double)b.volume;
            if (vol > 0.0) {
                double tp = ((double)b.high + (double)b.low + (double)b.close) / 3.0;
                double d = tp - s->center;
                var_sum += d * d * vol;
            }
        }
        double sd = sqrt(var_sum / sum_v);
        s->upper1 = s->center + sd * s->band_mult;
        s->lower1 = s->center - sd * s->band_mult;
        s->valid = true;
    }
    /* 무효 봉: 중심·밴드 값은 이월 (원본 var 의미) */

    s->upper2 = s->center + (s->upper1 - s->center) * 2.0;
    s->lower2 = s->center - (s->center - s->lower1) * 2.0;
    s->center_slope = s->center - prev_center;

    s->position_strength = 0.0;
    s->stage = 0;
    double dist_base = fmax(s->price_scale, fabs(s->upper1 - s->center));
    if (s->valid && dist_base > 0.0) {
        double c = (double)bar->close;
        double strength = ((c - s->center) / dist_base) * 100.0;
        if (strength > 100.0) {
            strength = 100.0;
        }
        if (strength < -100.0) {
            strength = -100.0;
        }
        s->position_strength = strength;

        if (s->center_slope > 0.0 && c > s->center && c > flat_reg_line) {
            s->stage = strength >= 66.0 ? 3 : (strength >= 33.0 ? 2 : 1);
        } else if (s->center_slope < 0.0 && c < s->center && c < flat_reg_line) {
            s->stage = strength <= -66.0 ? -3 : (strength <= -33.0 ? -2 : -1);
        }
    }
}
