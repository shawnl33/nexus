#include "core/functions/fx_wave_adj_v6.h"

#include <string.h>

/* WSF_FXWaveAdjV6_CO.txt 72–278. 분기 순서를 유지한다. */

static double dabs(double v) {
    return v < 0.0 ? -v : v;
}

static double dmax(double a, double b) {
    return a > b ? a : b;
}

static double dmin(double a, double b) {
    return a < b ? a : b;
}

static int iabs(int v) {
    return v < 0 ? -v : v;
}

void tr_fxadj_init(tr_fxadj_t *s) {
    if (s != 0) {
        memset(s, 0, sizeof(*s));
    }
}

static void clear_run(tr_fxadj_bar_t *b) {
    b->run_hi = 0;
    b->run_lo = 0;
    b->trend = 0;
    b->trend_len = 0;
    b->trend_start = 0;
    b->trend_ext = 0;
    b->in_adj = 0;
    b->adj_bars = 0;
    b->adj_ext = 0;
    b->opp_max = 0;
    b->bent = 0;
    b->trend_age = 0;
    b->recent_swing = 0;
    b->up_spent = 0;
    b->dn_spent = 0;
}

void tr_fxadj_eval(tr_fxadj_t *s, const tr_fxadj_in_t *in, tr_fxadj_out_t *out) {
    tr_fxadj_bar_t b;
    tr_fxflat_out_t flat;
    double prev_up_hi, prev_up_lo, prev_dn_hi, prev_dn_lo;
    int slope_sign, slope_len;
    int up_struct = 0, dn_struct = 0;
    int up_ok = 0, dn_ok = 0;
    int up_chg = 0, dn_chg = 0;
    double time_r = 0, price_r = 0, opp_r = 0;
    int sig_dir = 0, sig_kind = 0, sig_grade = 0, flipped = 0;
    int in_struct = 0;
    int rev;
    if (s == 0 || in == 0 || out == 0) {
        return;
    }
    rev = in->rev <= 0 ? 6 : in->rev;
    memset(out, 0, sizeof(*out));
    /* 새 봉의 첫 평가에서만 직전 평가를 [1]로 확정한다. */
    if (in->new_bar && s->has_cur) {
        s->closed = s->cur;
        s->has_closed = 1;
    }
    if (s->has_closed) {
        b = s->closed;
    } else {
        memset(&b, 0, sizeof(b));
    }
    prev_up_hi = b.up_hi_v;
    prev_up_lo = b.up_lo_v;
    prev_dn_hi = b.dn_hi_v;
    prev_dn_lo = b.dn_lo_v;
    if (in->session_reset) {
        clear_run(&b);
    }

    tr_fxflat_step(s->has_closed ? &s->closed.flat : 0, in->session_reset, in->flat, in->flat_prev,
                   in->high, in->low, in->close, in->close_prev, in->price_scale, in->min_leg, &flat);
    b.flat = flat;

    sig_dir = 0;
    sig_kind = 0;
    sig_grade = 0;
    flipped = 0;
    /* 이탈구조. 네 값이 모두 양수일 때만. */
    if (in->up_hi > 0.0 && in->up_lo > 0.0 && in->dn_hi > 0.0 && in->dn_lo > 0.0) {
        if (in->up_382 > in->dn_hi) {
            up_struct = 1;
        }
        if (in->dn_618 < in->up_lo) {
            dn_struct = 1;
        }
    }
    /* 최근 스윙은 V4부터. 같은 봉에 둘 다 바뀌면 직전 판정 유지. */
    if (rev >= 4 && s->has_closed && !in->session_reset) {
        up_chg = (in->up_hi > 0.0 && (in->up_hi != prev_up_hi || in->up_lo != prev_up_lo)) ? 1 : 0;
        dn_chg = (in->dn_hi > 0.0 && (in->dn_hi != prev_dn_hi || in->dn_lo != prev_dn_lo)) ? 1 : 0;
        if (up_chg) {
            b.up_spent = 0;
        }
        if (dn_chg) {
            b.dn_spent = 0;
        }
        if (up_chg && !dn_chg) {
            b.recent_swing = 1;
        } else if (dn_chg && !up_chg) {
            b.recent_swing = -1;
        }
    }
    if (b.recent_swing == 1) {
        up_ok = up_struct;
    } else if (b.recent_swing == -1) {
        dn_ok = dn_struct;
    } else {
        if (up_struct && !dn_struct) {
            up_ok = 1;
        }
        if (dn_struct && !up_struct) {
            dn_ok = 1;
        }
    }
    /* V5: 종가가 상승최저 이하, 하락최고 이상이면 그 구조를 무효로 한다. */
    if (rev >= 5 && up_ok && in->close <= in->up_lo) {
        up_ok = 0;
    }
    if (rev >= 5 && dn_ok && in->close >= in->dn_hi) {
        dn_ok = 0;
    }
    /* V6: 전환으로 소진된 구조는 다시 쓰지 않는다. */
    if (rev >= 6 && b.up_spent) {
        up_ok = 0;
    }
    if (rev >= 6 && b.dn_spent) {
        dn_ok = 0;
    }

    time_r = 0;
    price_r = 0;
    opp_r = 0;
    slope_sign = flat.slope > 0 ? 1 : (flat.slope < 0 ? -1 : 0);
    slope_len = iabs(flat.slope);
    if (flat.valid && !in->session_reset && slope_sign != 0) {
        if (slope_len == 1) {
            b.run_hi = in->high;
            b.run_lo = in->low;
        } else {
            b.run_hi = dmax(b.run_hi, in->high);
            b.run_lo = dmin(b.run_lo, in->low);
        }
        if (b.trend == 0) {
            if (slope_len >= in->min_trend &&
                (slope_sign != 1 || !dn_ok) && (slope_sign != -1 || !up_ok)) {
                b.trend = slope_sign;
                b.trend_len = slope_len;
                b.trend_start = slope_sign == 1 ? b.run_lo : b.run_hi;
                b.trend_ext = slope_sign == 1 ? b.run_hi : b.run_lo;
                b.in_adj = 0;
                b.trend_age = 0;
            }
        } else if (!b.in_adj) {
            if (b.trend == 1) {
                b.trend_ext = dmax(b.trend_ext, in->high);
            } else {
                b.trend_ext = dmin(b.trend_ext, in->low);
            }
            if (slope_sign == b.trend && slope_len >= in->min_trend) {
                b.trend_len = slope_len;
            }
            if (flat.pos * b.trend < 0 || slope_sign == -b.trend) {
                b.in_adj = 1;
                b.adj_bars = 0;
                b.adj_ext = b.trend == 1 ? in->low : in->high;
                b.opp_max = 0;
                b.bent = 0;
            }
        }
        if (b.trend != 0 && b.in_adj) {
            int turn = 0;
            double span;
            b.adj_bars += 1;
            if (b.trend == 1) {
                b.adj_ext = dmin(b.adj_ext, in->low);
            } else {
                b.adj_ext = dmax(b.adj_ext, in->high);
            }
            if (slope_sign == -b.trend) {
                b.bent = 1;
                b.opp_max = dmax(b.opp_max, (double)slope_len);
            }
            if (b.trend_len > 0) {
                time_r = (double)b.adj_bars / (double)b.trend_len * 100.0;
                opp_r = b.opp_max / (double)b.trend_len * 100.0;
            }
            span = dabs(b.trend_ext - b.trend_start);
            if (span > 0.0) {
                price_r = dabs(b.trend_ext - b.adj_ext) / span * 100.0;
            }
            if (rev <= 1) {
                /* V1: 조정극값이 추세시작가를 넘으면 전환. 종가와 무관하다. */
                turn = b.trend == 1 ? (b.adj_ext < b.trend_start) : (b.adj_ext > b.trend_start);
            } else {
                int struct_up = rev == 2 ? up_struct : up_ok;
                int struct_dn = rev == 2 ? dn_struct : dn_ok;
                in_struct = (b.trend == 1 && struct_up) || (b.trend == -1 && struct_dn);
                if (b.trend == 1) {
                    turn = in_struct ? (in->close < in->dn_hi) : (in->close < b.trend_start);
                } else {
                    turn = in_struct ? (in->close > in->up_lo) : (in->close > b.trend_start);
                }
            }
            if (in->flip_opp_pct > 0.0 && opp_r >= in->flip_opp_pct) {
                turn = 1;
            }
            if (turn) {
                flipped = b.trend;
                b.trend = 0;
                b.in_adj = 0;
                b.bent = 0;
            } else if ((b.trend == 1 && in->close > b.trend_ext) ||
                       (b.trend == -1 && in->close < b.trend_ext)) {
                if (!in->start_b_only || b.bent) {
                    b.trend_start = b.adj_ext;
                }
                b.in_adj = 0;
                b.bent = 0;
                if (b.trend == 1) {
                    b.trend_ext = dmax(b.trend_ext, in->high);
                } else {
                    b.trend_ext = dmin(b.trend_ext, in->low);
                }
            } else if (!b.bent && flat.pos * b.trend >= in->confirm_back) {
                sig_dir = b.trend;
                sig_kind = 1;
            } else if (b.bent && slope_sign == b.trend && slope_len == 1) {
                if (b.opp_max >= (double)in->min_opp) {
                    sig_dir = b.trend;
                    sig_kind = 2;
                } else {
                    b.bent = 0;
                    b.opp_max = 0;
                }
            }
            if (sig_dir != 0) {
                double mid = in->mid_px;
                if (in->struct_boost && in_struct) {
                    mid = dmax(in->mid_px, in->struct_px);
                }
                sig_grade = 1;
                if (price_r <= in->strong_px && time_r >= in->strong_time) {
                    sig_grade = 3;
                } else if (price_r <= mid) {
                    sig_grade = 2;
                }
                if (!in->start_b_only || sig_kind == 2) {
                    b.trend_start = b.adj_ext;
                }
                b.in_adj = 0;
                b.bent = 0;
            }
        }
    }

    /* 유효한 반대 이탈구조가 생기면 즉시 전환. 추세·조정 모두. */
    if (flat.valid && !in->session_reset && b.trend != 0 && flipped == 0 &&
        ((b.trend == 1 && dn_ok) || (b.trend == -1 && up_ok))) {
        flipped = b.trend;
        b.trend = 0;
        b.in_adj = 0;
        b.bent = 0;
        sig_dir = 0;
        sig_kind = 0;
        sig_grade = 0;
    }
    /* V6만 전환된 추세의 구조를 소진한다. 이번 봉 판정에는 쓰지 않는다. */
    if (rev >= 6 && flipped == 1) {
        b.up_spent = 1;
    }
    if (rev >= 6 && flipped == -1) {
        b.dn_spent = 1;
    }
    if (b.trend != 0) {
        b.trend_age += 1;
    } else {
        b.trend_age = 0;
    }
    if (sig_dir != 0 && in->signal_limit > 0 && b.trend_age >= in->signal_limit) {
        sig_dir = 0;
        sig_kind = 0;
        sig_grade = 0;
    }

    b.up_hi_v = in->up_hi;
    b.up_lo_v = in->up_lo;
    b.dn_hi_v = in->dn_hi;
    b.dn_lo_v = in->dn_lo;
    s->cur = b;
    s->has_cur = 1;

    out->trend = b.trend;
    out->in_adj = b.in_adj;
    out->time_ratio = time_r;
    out->price_ratio = price_r;
    out->opp_ratio = opp_r;
    out->sig_dir = sig_dir;
    out->sig_kind = sig_kind;
    out->sig_grade = sig_grade;
    out->flipped = flipped;
    out->valid = flat.valid;
}
