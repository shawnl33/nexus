#include "core/functions/atr.h"

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
    uint32_t count = (uint32_t)yls_count(&a->sma_hist) + 1;
    if (yls_count(&a->sma_hist) >= a->period) {
        double oldest = 0.0;
        yls_at(&a->sma_hist, a->period - 1, &oldest); /* 가장 오래된 TR */
        sum -= oldest;
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
    /* memset 다음에 시계열 저장소를 연결한다 (순서 고정). 유효 용량은 period개. */
    return yls_init(&a->sma_hist, a->sma_hist_buf, period);
}

bool tr_atr_init(tr_atr_t *a, uint32_t period) {
    return tr_atr_init_ex(a, period, TR_ATR_SMA);
}

double tr_atr_on_bar(tr_atr_t *a, double high, double low, double close) {
    double tr = true_range(a, high, low, close);
    if (a->mode == TR_ATR_SMA) {
        if (yls_count(&a->sma_hist) >= a->period) {
            double oldest = 0.0;
            yls_at(&a->sma_hist, a->period - 1, &oldest); /* 가장 오래된 TR */
            a->sma_sum -= oldest;
        }
        yls_push(&a->sma_hist, tr);
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
        size_t n = yls_count(&a->sma_hist);
        return n > 0 ? a->sma_sum / (double)n : 0.0;
    }
    if (a->wilder_seeded) {
        return a->wilder_atr;
    }
    return a->wilder_count > 0 ? a->wilder_seed_sum / (double)a->wilder_count : 0.0;
}

bool tr_atr_relink(tr_atr_t *a) {
    if (a == 0) {
        return false;
    }
    return yls_relink(&a->sma_hist, a->sma_hist_buf);
}
