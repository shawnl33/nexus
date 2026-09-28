#include "core/indicators/atr.h"

#include <math.h>
#include <string.h>

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

static double sma_estimate(const tr_atr_t *a, double new_tr) {
    /* 진행 봉 포함 추정: 가장 오래된 값이 밀려나는 것을 반영 */
    double sum = a->sma_sum + new_tr;
    uint32_t count = a->sma_count + 1;
    if (a->sma_count >= a->period) {
        sum -= a->sma_hist[0];
        count = a->period;
    }
    return count > 0 ? sum / (double)count : 0.0;
}

static double wilder_estimate(const tr_atr_t *a, double tr) {
    if (a->wilder_seeded) {
        return (a->wilder_atr * (double)(a->period - 1) + tr) / (double)a->period;
    }
    if (a->wilder_count > 0) {
        return (a->wilder_seed_sum + tr) / (double)(a->wilder_count + 1);
    }
    return tr;
}

bool tr_atr_init_ex(tr_atr_t *a, uint32_t period, tr_atr_mode_t mode) {
    if (a == 0 || period == 0 || period > TR_ATR_MAX_PERIOD) {
        return false;
    }
    memset(a, 0, sizeof(*a));
    a->mode = mode;
    a->period = period;
    return true;
}

bool tr_atr_init(tr_atr_t *a, uint32_t period) {
    return tr_atr_init_ex(a, period, TR_ATR_SMA);
}

double tr_atr_on_bar(tr_atr_t *a, double high, double low, double close) {
    double tr = true_range(a, high, low, close);
    if (a->mode == TR_ATR_SMA) {
        if (a->sma_count >= a->period) {
            a->sma_sum -= a->sma_hist[0];
            memmove(&a->sma_hist[0], &a->sma_hist[1], (a->period - 1) * sizeof(double));
            a->sma_count = a->period - 1;
        }
        a->sma_hist[a->sma_count++] = tr;
        a->sma_sum += tr;
    } else {
        if (a->wilder_seeded) {
            a->wilder_atr = (a->wilder_atr * (double)(a->period - 1) + tr) / (double)a->period;
        } else {
            a->wilder_seed_sum += tr;
            a->wilder_count++;
            if (a->wilder_count >= a->period) {
                a->wilder_atr = a->wilder_seed_sum / (double)a->period;
                a->wilder_seeded = true;
            }
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
    double tr = true_range(a, high, low, close);
    if (a->mode == TR_ATR_SMA) {
        return sma_estimate(a, tr);
    }
    return wilder_estimate(a, tr);
}

double tr_atr_value(const tr_atr_t *a) {
    if (a == 0) {
        return 0.0;
    }
    if (a->mode == TR_ATR_SMA) {
        return a->sma_count > 0 ? a->sma_sum / (double)a->sma_count : 0.0;
    }
    if (a->wilder_seeded) {
        return a->wilder_atr;
    }
    return a->wilder_count > 0 ? a->wilder_seed_sum / (double)a->wilder_count : 0.0;
}
