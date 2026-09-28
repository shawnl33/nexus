#include "core/indicators/orderbook_dir_v2.h"

#include <math.h>
#include <string.h>

static double clamp_abs(double v, double limit) {
    if (v > limit) {
        return limit;
    }
    if (v < -limit) {
        return -limit;
    }
    return v;
}

static void push(double *hist, size_t cap, size_t *len, double v) {
    size_t n = *len < cap ? *len + 1 : cap;
    for (size_t i = n - 1; i > 0; i--) {
        hist[i] = hist[i - 1];
    }
    hist[0] = v;
    *len = n;
}

static void reset_daily(tr_obd2_t *s) {
    s->started = false;
    s->quote_no = 0;
    s->cum_total = 0.0;
    s->core_len = 0;
    s->dir_len = 0;
    s->score = 0.0;
    s->slope3 = 0.0;
    s->state = 0;
    s->validity = TR_VALIDITY_MISSING;
}

bool tr_obd2_init(tr_obd2_t *s, double interest_level, double strong_level,
                  bool sign_reverse, bool is_futures) {
    if (s == 0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->interest_level = fmax(fabs(interest_level), 1.0);
    s->strong_level = fmax(fabs(strong_level), s->interest_level);
    s->sign_reverse = sign_reverse;
    s->is_futures = is_futures;
    s->last_day = -1;
    reset_daily(s);
    s->last_day = -1;
    return true;
}

void tr_obd2_eval(tr_obd2_t *s, double bids, double asks, int64_t trading_day) {
    if (s == 0) {
        return;
    }
    if (trading_day != s->last_day) {
        reset_daily(s);
        s->last_day = trading_day;
    }

    double total = bids + asks;
    if (total <= 0.0) {
        /* 호가 없음: 0으로 채우지 않고 무효 표시. 시계열은 갱신하지 않는다 */
        s->score = 0.0;
        s->slope3 = 0.0;
        s->state = 0;
        s->validity = TR_VALIDITY_MISSING;
        return;
    }

    double avg_total;
    if (!s->started) {
        s->started = true;
        s->quote_no = 0;
        s->cum_total = total;
        avg_total = total;
    } else {
        s->quote_no++;
        s->cum_total += total;
        avg_total = s->cum_total / (double)(s->quote_no + 1);
    }

    double diff = s->is_futures ? (bids - asks) : (asks - bids);
    double diff_score = clamp_abs(diff / avg_total * 100.0, 100.0);
    double ratio_score = clamp_abs(diff / total * 100.0, 100.0); /* 원본 클랩프 유지(도달 불가) */
    if (s->sign_reverse) {
        diff_score = -diff_score;
        ratio_score = -ratio_score;
    }
    double core = (diff_score * 60.0 + ratio_score * 40.0) / 100.0;
    push(s->core_hist, 5, &s->core_len, core);

    /* 방향평균3: 유효 평가 3회 미만이면 핵심점수 (원본 132줄) */
    double ma3 = core;
    if (s->core_len >= 3) {
        ma3 = (s->core_hist[0] + s->core_hist[1] + s->core_hist[2]) / 3.0;
    }
    /* 방향평균5: 유효 평가 5회 미만이면 방향평균3 (원본 141줄) */
    double ma5 = ma3;
    if (s->core_len >= 5) {
        ma5 = (s->core_hist[0] + s->core_hist[1] + s->core_hist[2] +
               s->core_hist[3] + s->core_hist[4]) / 5.0;
    }

    double dir;
    if (s->quote_no == 0) {
        dir = core; /* 첫 유효 평가 (원본 96줄) */
    } else {
        dir = (core * 60.0 + ma3 * 25.0 + ma5 * 15.0) / 100.0;
    }
    push(s->dir_hist, 4, &s->dir_len, dir);

    double slope3 = 0.0;
    if (s->dir_len >= 4) {
        slope3 = clamp_abs(dir - s->dir_hist[3], 100.0);
    }

    int state = 0;
    if (dir >= s->strong_level) {
        state = 2;
    } else if (dir >= s->interest_level) {
        state = 1;
    } else if (dir <= -s->strong_level) {
        state = -2;
    } else if (dir <= -s->interest_level) {
        state = -1;
    }

    s->score = dir;
    s->slope3 = slope3;
    s->state = state;
    s->validity = TR_VALIDITY_VALID;
}
