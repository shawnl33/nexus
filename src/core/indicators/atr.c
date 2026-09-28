#include "core/indicators/atr.h"

#include <math.h>

static double true_range(const tr_atr_t *a, double high, double low, double close) {
    double tr = high - low;
    if (a->has_prev_close) {
        double d1 = fabs(high - a->prev_close);
        double d2 = fabs(low - a->prev_close);
        if (d1 > tr) {
            tr = d1;
        }
        if (d2 > tr) {
            tr = d2;
        }
    }
    (void)close;
    return tr;
}

static double current_estimate(const tr_atr_t *a, double tr) {
    if (a->seeded) {
        return (a->atr * (double)(a->period - 1) + tr) / (double)a->period;
    }
    if (a->count > 0) {
        return (a->tr_sum + tr) / (double)(a->count + 1);
    }
    return tr;
}

bool tr_atr_init(tr_atr_t *a, uint32_t period) {
    if (a == 0 || period == 0) {
        return false;
    }
    a->period = period;
    a->prev_close = 0.0;
    a->has_prev_close = false;
    a->atr = 0.0;
    a->tr_sum = 0.0;
    a->count = 0;
    a->seeded = false;
    return true;
}

double tr_atr_on_bar(tr_atr_t *a, double high, double low, double close) {
    double tr = true_range(a, high, low, close);
    if (a->seeded) {
        a->atr = (a->atr * (double)(a->period - 1) + tr) / (double)a->period;
    } else {
        a->tr_sum += tr;
        a->count++;
        if (a->count >= a->period) {
            a->atr = a->tr_sum / (double)a->period;
            a->seeded = true;
        }
    }
    a->prev_close = close;
    a->has_prev_close = true;
    return tr_atr_value(a);
}

double tr_atr_candidate(const tr_atr_t *a, double high, double low, double close) {
    if (a == 0) {
        return 0.0;
    }
    return current_estimate(a, true_range(a, high, low, close));
}

double tr_atr_value(const tr_atr_t *a) {
    if (a == 0) {
        return 0.0;
    }
    if (a->seeded) {
        return a->atr;
    }
    if (a->count > 0) {
        return a->tr_sum / (double)a->count;
    }
    return 0.0;
}
