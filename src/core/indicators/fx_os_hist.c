#include "core/indicators/fx_os_hist.h"

#include "core/indicators/fx_os_filter.h"

#include <string.h>

static double dmax(double a, double b) {
    return a > b ? a : b;
}

static double dmin(double a, double b) {
    return a < b ? a : b;
}

static int imin(int a, int b) {
    return a < b ? a : b;
}

static void shift(tr_fxos_hist_t *h) {
    int n = h->bars;
    int i;
    if (n < TR_FXOS_HIST_CAP) {
        n++;
    }
    for (i = n - 1; i > 0; i--) {
        h->open_r[i] = h->open_r[i - 1];
        h->high_r[i] = h->high_r[i - 1];
        h->low_r[i] = h->low_r[i - 1];
        h->close_r[i] = h->close_r[i - 1];
        h->sq_r[i] = h->sq_r[i - 1];
        h->range_r[i] = h->range_r[i - 1];
        h->flat_r[i] = h->flat_r[i - 1];
        h->mkt_r[i] = h->mkt_r[i - 1];
        h->up_r[i] = h->up_r[i - 1];
        h->dn_r[i] = h->dn_r[i - 1];
        h->hold_r[i] = h->hold_r[i - 1];
        h->color_r[i] = h->color_r[i - 1];
        h->unified_r[i] = h->unified_r[i - 1];
    }
    h->bars = n;
}

static double window_max(const double *a, int n, int window, double session_max) {
    int i;
    int lim;
    double m = 0;
    if (window <= 0) {
        return session_max;
    }
    lim = imin(window, n);
    for (i = 0; i < lim; i++) {
        m = dmax(m, a[i]);
    }
    return m;
}

int tr_fxos_box_color(double close, double hi, double lo, int valid) {
    double t;
    if (!valid || !(hi > lo)) {
        return 0;
    }
    t = (close - lo) / (hi - lo);
    if (t >= 2.0 / 3.0) {
        return 1;
    }
    if (t <= 1.0 / 3.0) {
        return -1;
    }
    return 0;
}

void tr_fxos_hist_init(tr_fxos_hist_t *h) {
    if (h == 0) {
        return;
    }
    memset(h, 0, sizeof(*h));
}

void tr_fxos_hist_bar(tr_fxos_hist_t *h, const tr_fxos_hist_in_t *in) {
    int ready;
    double spread;
    double range;
    int min_bars;
    int i;
    int span = 0;
    if (h == 0 || in == 0) {
        return;
    }
    shift(h);
    h->open_r[0] = in->open;
    h->high_r[0] = in->high;
    h->low_r[0] = in->low;
    h->close_r[0] = in->close;
    h->sq_r[0] = in->sq_ratio;
    h->up_r[0] = in->prev_up_slope;
    h->dn_r[0] = in->prev_dn_slope;
    h->hold_r[0] = in->sq_hold;
    h->color_r[0] = in->sq_color;
    h->unified_r[0] = in->unified;

    if (in->session_first) {
        h->three_max = 0;
        h->two_max = 0;
        h->flat_max = 0;
        h->swing_max = 0;
        h->mkt_max = 0;
        h->break_armed = 0;
        h->prev_first = 0;
        h->prev_px_exit = 0;
        h->prev_compound = 0;
        h->session_bars = 1;
        h->seg_count = 0;
        h->prev_ok = 0;
        h->was_ready = 0;
    } else {
        h->session_bars += 1;
    }

    ready = in->t1 != 0 && in->t2 != 0 && in->t3 != 0;
    spread = 0;
    if (ready) {
        double hi = dmax(in->t1, dmax(in->t2, in->t3));
        double lo = dmin(in->t1, dmin(in->t2, in->t3));
        spread = hi - lo;
        h->three_max = dmax(h->three_max, spread);
        h->three_ratio = h->three_max > 0 ? spread / h->three_max * 100.0 : 0;
    } else {
        h->three_ratio = 0;
    }

    min_bars = in->two_min_bars > 0 ? in->two_min_bars : 3;
    if (in->session_first || !ready) {
        h->seg_count = 0;
        h->seg_hi = 0;
        h->seg_lo = 0;
        h->prev_hi = 0;
        h->prev_lo = 0;
        h->prev_ok = 0;
    }
    if (ready) {
        if (in->session_first || !h->was_ready) {
            h->seg_count = 1;
            h->seg_hi = in->high;
            h->seg_lo = in->low;
            h->prev_ok = 0;
        } else if (spread != h->spread && h->seg_count >= min_bars) {
            h->prev_hi = h->seg_hi;
            h->prev_lo = h->seg_lo;
            h->prev_ok = 1;
            h->seg_count = 1;
            h->seg_hi = in->high;
            h->seg_lo = in->low;
        } else {
            h->seg_count += 1;
            h->seg_hi = dmax(h->seg_hi, in->high);
            h->seg_lo = dmin(h->seg_lo, in->low);
        }
    }
    h->was_ready = ready;
    h->spread = spread;
    range = 0;
    if (ready) {
        double hi = h->seg_hi;
        double lo = h->seg_lo;
        if (h->prev_ok) {
            hi = dmax(hi, h->prev_hi);
            lo = dmin(lo, h->prev_lo);
        }
        range = hi - lo;
        h->two_max = dmax(h->two_max, range);
    }
    h->range_r[0] = range;
    if (!ready || range <= 0) {
        h->two_ratio = 0;
    } else {
        double basis = window_max(h->range_r, h->bars, in->two_window > 0 ? imin(in->two_window, h->session_bars) : 0,
                                   h->two_max);
        h->two_ratio = basis > 0 ? range / basis * 100.0 : 0;
    }

    if (in->flat_ready) {
        double hi = dmax(dmax(in->flat_1, in->flat_5), dmax(in->flat_15, in->flat_30));
        double lo = dmin(dmin(in->flat_1, in->flat_5), dmin(in->flat_15, in->flat_30));
        double width = hi - lo;
        h->flat_r[0] = width;
        h->flat_max = dmax(h->flat_max, width);
        if (width <= 0) {
            h->flat_ratio = 0;
        } else {
            double basis = window_max(h->flat_r, h->bars, in->flat_window > 0 ? imin(in->flat_window, h->session_bars) : 0,
                                       h->flat_max);
            h->flat_ratio = basis > 0 ? width / basis * 100.0 : 0;
        }
    } else {
        h->flat_r[0] = 0;
        h->flat_ratio = 0;
    }

    if (in->fv_ok && in->rise_hi > 0 && in->rise_lo > 0 && in->fall_hi > 0 && in->fall_lo > 0) {
        double width = dmax(in->rise_hi, in->fall_hi) - dmin(in->rise_lo, in->fall_lo);
        h->swing_max = dmax(h->swing_max, width);
        h->swing = h->swing_max > 0 ? width / h->swing_max * 100.0 : 0;
    } else {
        h->swing = 0;
    }

    h->prior_low = 0;
    if (in->prev_dn_len > 0 && h->bars > 1) {
        int n = imin(in->prev_dn_len, h->bars - 1);
        h->prior_low = h->low_r[1];
        for (i = 2; i <= n; i++) {
            h->prior_low = dmin(h->prior_low, h->low_r[i]);
        }
    }
    h->prior_high = 0;
    if (in->prev_up_len > 0 && h->bars > 1) {
        int n = imin(in->prev_up_len, h->bars - 1);
        h->prior_high = h->high_r[1];
        for (i = 2; i <= n; i++) {
            h->prior_high = dmax(h->prior_high, h->high_r[i]);
        }
    }

    h->width_seen = 0;
    if (in->slope < -20) {
        span = imin(-in->slope, h->bars);
        for (i = 0; i < span; i++) {
            if (h->sq_r[i] >= 100.0) {
                h->width_seen = 1;
            }
        }
    }

    h->tall_buy = 0;
    h->tall_sell = 0;
    if (h->bars >= 23) {
        tr_fxos_tall_flags(h->open_r, h->high_r, h->low_r, h->close_r, 20, 3, 3, 60, 85, &h->tall_buy, &h->tall_sell);
    }

    h->hot_up = 0;
    h->hot_dn = 0;
    span = imin(15, h->bars);
    for (i = 0; i < span; i++) {
        if (h->up_r[i] > 30) {
            h->hot_up = 1;
        }
        if (h->dn_r[i] > 30) {
            h->hot_dn = 1;
        }
    }
    h->red_n = 0;
    span = imin(10, h->bars);
    for (i = 0; i < span; i++) {
        if (h->hold_r[i] > 0 && h->color_r[i] == 1) {
            h->red_n += 1;
        }
    }
    h->had_m100 = 0;
    h->had_p100 = 0;
    span = imin(10, h->bars - 1);
    for (i = 1; i <= span; i++) {
        if (h->unified_r[i] == -100) {
            h->had_m100 = 1;
        }
        if (h->unified_r[i] == 100) {
            h->had_p100 = 1;
        }
    }

    if (in->session_first) {
        h->bars_since_buy = 999;
    } else {
        h->bars_since_buy += 1;
    }
    if (in->turn_buy) {
        h->bars_since_buy = 0;
    }

    /* 삼선점수색상 회색. 삼선이 준비되고 신호합계·직전 복합압축·비율점수가 0. */
    {
        int three_ready = ready;
        int pos = 0;
        int first = 0;
        int mkt_ready = 0;
        double mkt_ratio = 0;
        int compound = 0;
        int score = 0;
        int ratio = 0;
        int price_pts = 0;
        double slack = (double)(in->slack_ticks > 0 ? in->slack_ticks : 1) *
                       (in->price_scale > 0.0 ? in->price_scale : 1.0);
        if (in->session_first || !three_ready || h->seg_count == 1) {
            h->break_armed = 0;
        }
        if (three_ready && h->prev_ok) {
            if (in->close > h->prev_hi + slack) {
                pos = 1;
            } else if (in->close < h->prev_lo - slack) {
                pos = -1;
            }
        }
        if (h->break_armed == 0 && pos != 0) {
            first = pos;
            h->break_armed = 1;
        }
        mkt_ready = in->fv_ok && in->mkt_1 != 0.0 && in->mkt5_ok && in->mkt15_ok && in->mkt30_ok;
        if (mkt_ready) {
            double hi = dmax(dmax(in->mkt_1, in->mkt_5), dmax(in->mkt_15, in->mkt_30));
            double lo = dmin(dmin(in->mkt_1, in->mkt_5), dmin(in->mkt_15, in->mkt_30));
            double width = hi - lo;
            int win = in->flat_window > 0 ? imin(in->flat_window, h->session_bars) : 0;
            h->mkt_r[0] = width;
            h->mkt_max = dmax(h->mkt_max, width);
            if (width > 0.0) {
                double basis = window_max(h->mkt_r, h->bars, win, h->mkt_max);
                mkt_ratio = basis > 0.0 ? width / basis * 100.0 : 0.0;
            }
        } else {
            h->mkt_r[0] = 0;
        }
        compound = three_ready && h->two_max > 0.0 && h->two_ratio < 40.0 && in->flat_ready && mkt_ready &&
                   h->three_ratio <= 30.0 && h->flat_ratio < 30.0 && mkt_ratio < 30.0;
        if (pos == -1) {
            score++;
        } else if (pos == 1) {
            score--;
        }
        if (!in->session_first && three_ready) {
            if (h->prev_first == 1) {
                score--;
            } else if (h->prev_first == -1) {
                score++;
            }
        }
        if (!in->session_first && in->fv_ok) {
            if (h->prev_px_exit == 1) {
                score--;
            } else if (h->prev_px_exit == -1) {
                score++;
            }
        }
        price_pts = three_ready && h->two_max > 0.0 && h->two_ratio < 40.0;
        if (price_pts) {
            ratio++;
        }
        if (in->flat_ready && h->flat_ratio < 30.0) {
            ratio++;
        }
        if (mkt_ready && mkt_ratio < 30.0) {
            ratio++;
        }
        if ((price_pts && h->two_ratio > 80.0) || (in->flat_ready && h->flat_ratio > 80.0) ||
            (mkt_ready && mkt_ratio > 80.0)) {
            ratio = 0;
        }
        if (price_pts) {
            ratio = ratio < 1 ? 1 : ratio;
        }
        h->gray = three_ready && score == 0 && h->prev_compound == 0 && ratio == 0;
        h->prev_first = first;
        h->prev_compound = compound;
        h->prev_px_exit = (three_ready && h->two_max > 0.0 && h->two_ratio < 40.0 && h->prev_ok) ? pos : 0;
    }
}
