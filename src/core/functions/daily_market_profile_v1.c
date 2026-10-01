#include "core/functions/daily_market_profile_v1.h"

#include <math.h>
#include <string.h>

bool tr_dmp1_init(tr_dmp1_t *s, uint32_t period, double band_mult,
                  tr_candle_t *storage, size_t capacity) {
    if (s == 0 || storage == 0) {
        return false;
    }
    if (period < 2) {
        period = 2;
    }
    if (capacity < period) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->period = period;
    s->band_mult = fabs(band_mult);
    if (s->band_mult <= 0.0) {
        s->band_mult = 1.0;
    }
    return tr_ring_init(&s->hist, storage, sizeof(tr_candle_t), capacity);
}

void tr_dmp1_on_bar(tr_dmp1_t *s, const tr_candle_t *bar) {
    if (s == 0 || bar == 0) {
        return;
    }
    tr_ring_push(&s->hist, bar);

    double sum_v = 0.0, sum_pv = 0.0;
    for (uint32_t j = 0; j < s->period; j++) {
        tr_candle_t b;
        if (!tr_ring_at(&s->hist, j, &b)) {
            break; /* 가용 봉만 합산 (원본 동작 불명 구간, 기록) */
        }
        double vol = (double)b.volume;
        if (vol > 0.0) {
            double tp = ((double)b.high + (double)b.low + (double)b.close) / 3.0;
            sum_v += vol;
            sum_pv += tp * vol;
        }
    }

    s->center = 0.0;
    s->upper = 0.0;
    s->lower = 0.0;
    s->valid = false;
    if (sum_v > 0.0) {
        s->center = sum_pv / sum_v;
        double var_sum = 0.0;
        for (uint32_t j = 0; j < s->period; j++) {
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
        s->upper = s->center + sd * s->band_mult;
        s->lower = s->center - sd * s->band_mult;
        s->valid = true;
    }
}
