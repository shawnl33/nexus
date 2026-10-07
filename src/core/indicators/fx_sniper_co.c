#include "core/indicators/fx_sniper_co.h"

#include "core/functions/fx_session_key_v1.h"

#include <math.h>
#include <string.h>

#define RGB_(r, g, b) ((uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))

static void push(double *h, int *n, double x) {
    if (*n < TR_SNCO_CAP) {
        (*n)++;
    }
    for (int i = *n - 1; i > 0; i--) {
        h[i] = h[i - 1];
    }
    h[0] = x;
}

static double window_max(const double *h, int n, int win) {
    if (win > n) {
        win = n;
    }
    double m = 0.0;
    for (int i = 0; i < win; i++) {
        if (h[i] > m) {
            m = h[i];
        }
    }
    return m;
}

static void plot_set(tr_snco_plot_t *p, int id, bool on, double v, uint32_t rgb, int w) {
    p->id = id;
    p->on = on;
    p->value = v;
    p->rgb = rgb;
    p->width = w;
}

void tr_snco_init(tr_snco_t *s) {
    if (s != 0) {
        memset(s, 0, sizeof(*s));
    }
}

void tr_snco_eval(tr_snco_t *s, const tr_snco_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    bool is_new = !s->has_bar || in->bar_open != s->last_open;
    if (is_new) {
        s->prev = s->st;
        s->has_bar = true;
        s->last_open = in->bar_open;
        int64_t key = tr_fx_session_key_v1(in->date, in->time);
        s->reset_bar = !s->has_key || key != s->key;
        s->has_key = true;
        s->key = key;
    } else {
        s->st = s->prev;
    }
    memset(s->plots, 0, sizeof(s->plots));
    const tr_snco_mem_t *old = &s->prev;
    tr_snco_mem_t *m = &s->st;
    int reset = s->reset_bar;
    m->bars = reset ? 1 : old->bars + 1;
    int ratio_win = m->bars < 120 ? m->bars : 120;
    int three_win = 0;

    int three_ready = in->fv_ok && in->target[0] != 0.0 && in->target[1] != 0.0 && in->target[2] != 0.0;
    double three_hi = 0.0, three_lo = 0.0, three_gap = 0.0, three_ratio = 0.0;
    m->three_peak = reset ? 0.0 : old->three_peak;
    int below = 0, above = 0;
    if (three_ready) {
        three_hi = fmax(in->target[0], fmax(in->target[1], in->target[2]));
        three_lo = fmin(in->target[0], fmin(in->target[1], in->target[2]));
        three_gap = three_hi - three_lo;
        m->three_peak = fmax(m->three_peak, three_gap);
        push(m->h3, &m->n3, three_gap);
        double basis = three_win > 0 ? window_max(m->h3, m->n3, three_win) : m->three_peak;
        if (basis > 0.0) {
            three_ratio = three_gap / basis * 100.0;
        }
        if (reset || !old->three_ready || three_gap != old->three_gap) {
            below = 0;
            above = 0;
        } else {
            below = old->below;
            above = old->above;
        }
        if (in->close < three_hi) {
            below = 1;
        }
        if (in->close > three_lo) {
            above = 1;
        }
    } else {
        push(m->h3, &m->n3, 0.0);
    }
    m->three_ready = three_ready;
    m->three_gap = three_gap;
    m->below = below;
    m->above = above;

    int reg_ready = in->fv_ok && in->reg_flat != 0.0 && in->reg_ok[0] && in->reg_ok[1] && in->reg_ok[2];
    double reg_gap = 0.0, reg_ratio = 0.0;
    m->reg_peak = reset ? 0.0 : old->reg_peak;
    if (reg_ready) {
        double hi = fmax(in->reg_flat, fmax(in->reg_px[0], fmax(in->reg_px[1], in->reg_px[2])));
        double lo = fmin(in->reg_flat, fmin(in->reg_px[0], fmin(in->reg_px[1], in->reg_px[2])));
        reg_gap = hi - lo;
        m->reg_peak = fmax(m->reg_peak, reg_gap);
        push(m->hr, &m->nr, reg_gap);
        double basis = ratio_win > 0 ? window_max(m->hr, m->nr, ratio_win) : m->reg_peak;
        if (basis > 0.0) {
            reg_ratio = reg_gap / basis * 100.0;
        }
    } else {
        push(m->hr, &m->nr, 0.0);
    }

    int mkt_ready = in->fv_ok && in->market != 0.0 && in->mkt_ok[0] && in->mkt_ok[1] &&
                    (in->mkt30 == 0 || in->mkt_ok[2]);
    double mkt_gap = 0.0, mkt_ratio = 0.0;
    m->mkt_peak = reset ? 0.0 : old->mkt_peak;
    if (mkt_ready) {
        double hi = fmax(in->market, fmax(in->mkt_px[0], in->mkt_px[1]));
        double lo = fmin(in->market, fmin(in->mkt_px[0], in->mkt_px[1]));
        if (in->mkt30) {
            hi = fmax(hi, in->mkt_px[2]);
            lo = fmin(lo, in->mkt_px[2]);
        }
        mkt_gap = hi - lo;
        m->mkt_peak = fmax(m->mkt_peak, mkt_gap);
        push(m->hm, &m->nm, mkt_gap);
        double basis = ratio_win > 0 ? window_max(m->hm, m->nm, ratio_win) : m->mkt_peak;
        if (basis > 0.0) {
            mkt_ratio = mkt_gap / basis * 100.0;
        }
    } else {
        push(m->hm, &m->nm, 0.0);
    }

    double slack = fmax(0.0, 1.0) * (in->price_scale > 0.0 ? in->price_scale : 1.0);
    int leg_n = 0, leg_prev = 0, prev_ok = 0, broke = 0, first = 0, pos = 0, changed = 0;
    double cur_hi = 0, cur_lo = 0, prev_hi = 0, prev_lo = 0;
    if (reset || !three_ready) {
        leg_n = 0;
    } else {
        leg_n = old->leg_n;
        leg_prev = old->leg_n_prev;
        cur_hi = old->cur_hi;
        cur_lo = old->cur_lo;
        prev_hi = old->prev_hi;
        prev_lo = old->prev_lo;
        prev_ok = old->prev_ok;
        broke = old->broke;
    }
    if (three_ready) {
        if (reset || !old->three_ready) {
            leg_n = 1;
            leg_prev = 0;
            cur_hi = in->high;
            cur_lo = in->low;
            prev_hi = 0;
            prev_lo = 0;
            prev_ok = 0;
            broke = 0;
        } else if (three_gap != old->three_gap && old->leg_n >= 3) {
            changed = 1;
            leg_prev = old->leg_n;
            prev_hi = old->cur_hi;
            prev_lo = old->cur_lo;
            prev_ok = 1;
            leg_n = 1;
            cur_hi = in->high;
            cur_lo = in->low;
            broke = 0;
        } else {
            leg_n = old->leg_n + 1;
            cur_hi = fmax(old->cur_hi, in->high);
            cur_lo = fmin(old->cur_lo, in->low);
        }
        if (prev_ok) {
            if (in->close > prev_hi + slack) {
                pos = 1;
            } else if (in->close < prev_lo - slack) {
                pos = -1;
            }
            if (!broke && pos != 0) {
                first = pos;
                broke = 1;
            }
        }
    }
    m->leg_n = leg_n;
    m->leg_n_prev = leg_prev;
    m->cur_hi = cur_hi;
    m->cur_lo = cur_lo;
    m->prev_hi = prev_hi;
    m->prev_lo = prev_lo;
    m->prev_ok = prev_ok;
    m->broke = broke;
    m->first_break = first;

    double span = 0.0, span_ratio = 0.0;
    m->span_peak = reset ? 0.0 : old->span_peak;
    if (three_ready) {
        double hi = cur_hi, lo = cur_lo;
        if (prev_ok) {
            hi = fmax(cur_hi, prev_hi);
            lo = fmin(cur_lo, prev_lo);
        }
        span = hi - lo;
        m->span_peak = fmax(m->span_peak, span);
        push(m->hs, &m->ns, span);
        double basis = ratio_win > 0 ? window_max(m->hs, m->ns, ratio_win) : m->span_peak;
        if (basis > 0.0) {
            span_ratio = span / basis * 100.0;
        }
    } else {
        push(m->hs, &m->ns, 0.0);
    }
    m->span = span;

    int scope = 0;
    if (three_ready && three_ratio >= 30.0) {
        if (!below) {
            scope = 1;
        } else if (!above) {
            scope = -1;
        }
    }
    int pos_use = pos;
    int scope_use = scope;

    int compound = 0, px_exit = 0;
    if (in->fv_ok && three_ready && m->span_peak > 0.0 && span_ratio < 40.0) {
        if (reg_ready && mkt_ready && three_ratio <= 30.0 && reg_ratio < 30.0 && mkt_ratio < 30.0) {
            compound = 1;
        }
        if (prev_ok) {
            px_exit = pos;
        }
    }
    m->compound = compound;
    m->px_exit = px_exit;

    int up_sig = 0, dn_sig = 0;
    if (pos_use == -1) {
        up_sig = 1;
    } else if (pos_use == 1) {
        dn_sig = 1;
    }
    if (!reset && three_ready) {
        if (old->first_break == 1) {
            dn_sig = 1;
        }
        if (old->first_break == -1) {
            up_sig = 1;
        }
    }
    if (!reset && in->fv_ok) {
        if (old->compound == 1 || old->px_exit == 1) {
            dn_sig = 1;
        }
        if (old->px_exit == -1) {
            up_sig = 1;
        }
    }
    if (scope_use == 1) {
        dn_sig = 1;
    } else if (scope_use == -1) {
        up_sig = 1;
    }
    uint32_t dot_rgb = RGB_(255, 0, 0);
    if (up_sig) {
        dot_rgb = RGB_(0, 0, 255);
    }
    if (up_sig && dn_sig) {
        dot_rgb = RGB_(0, 128, 0);
    }

    int score = 0;
    if (pos_use == -1) {
        score += 1;
    } else if (pos_use == 1) {
        score -= 1;
    }
    if (!reset && three_ready) {
        if (old->first_break == 1) {
            score -= 1;
        }
        if (old->first_break == -1) {
            score += 1;
        }
    }
    int show_compound = 0;
    if (!reset && in->fv_ok) {
        if (old->px_exit == 1) {
            score -= 1;
        }
        if (old->px_exit == -1) {
            score += 1;
        }
        if (old->compound == 1) {
            show_compound = 1;
        }
    }
    int score_ex = score;
    if (scope_use == 1) {
        score -= 1;
    } else if (scope_use == -1) {
        score += 1;
    }
    int scope_sum = -score;
    if (scope_sum > 2) {
        scope_sum = 2;
    }
    if (scope_sum < -2) {
        scope_sum = -2;
    }
    s->scope_state = scope_sum;

    int ratio_score = 0;
    if (three_ready && m->span_peak > 0.0 && span_ratio < 40.0) {
        ratio_score++;
    }
    if (reg_ready && reg_ratio < 30.0) {
        ratio_score++;
    }
    if (mkt_ready && mkt_ratio < 30.0) {
        ratio_score++;
    }
    if ((three_ready && m->span_peak > 0.0 && span_ratio > 80.0) || (reg_ready && reg_ratio > 80.0) ||
        (mkt_ready && mkt_ratio > 80.0)) {
        ratio_score = 0;
    }
    if (three_ready && m->span_peak > 0.0 && span_ratio < 40.0) {
        ratio_score = ratio_score < 1 ? 1 : ratio_score;
    }

    uint32_t three_rgb = RGB_(220, 220, 220);
    if (score_ex == 1) three_rgb = RGB_(128, 160, 255);
    else if (score_ex == 2) three_rgb = RGB_(0, 0, 255);
    else if (score_ex == 3) three_rgb = RGB_(0, 0, 150);
    else if (score_ex == -1) three_rgb = RGB_(255, 160, 160);
    else if (score_ex == -2) three_rgb = RGB_(255, 0, 0);
    else if (score_ex == -3) three_rgb = RGB_(180, 0, 0);
    else if (show_compound) three_rgb = RGB_(0, 128, 0);
    if (three_rgb == RGB_(220, 220, 220) && ratio_score > 0) {
        three_rgb = RGB_(160, 220, 160);
    }

    double vol_sum = 0.0, vol_prev = 0.0;
    if (!(reset || !three_ready)) {
        vol_sum = old->vol_sum;
        vol_prev = old->vol_prev;
    }
    if (three_ready) {
        double v = in->volume > 0.0 ? in->volume : 0.0;
        if (reset || !old->three_ready) {
            vol_sum = v;
            vol_prev = 0.0;
        } else if (changed) {
            vol_prev = old->vol_sum;
            vol_sum = v;
        } else {
            vol_sum = old->vol_sum + v;
        }
    }
    m->vol_sum = vol_sum;
    m->vol_prev = vol_prev;
    int vol_ready = 0;
    double vol_ratio = 0.0;
    double cur_avg = leg_n > 0 ? vol_sum / (double)leg_n : 0.0;
    double prev_avg = leg_prev > 0 ? vol_prev / (double)leg_prev : 0.0;
    if (prev_avg > 0.0 && leg_n >= 3 && leg_prev >= 3) {
        vol_ready = 1;
        vol_ratio = cur_avg / prev_avg * 100.0;
    }
    m->vol_ready = vol_ready;
    m->vol_ratio = vol_ratio;
    int price_ready = m->span_peak > 0.0;
    int both = price_ready && vol_ready && span_ratio < 40.0 && vol_ratio < 100.0;
    m->both = both;

    int i = 0;
    if (three_ready) {
        int w = (three_ratio <= 30.0 ? 8 : 5) + ratio_score;
        plot_set(&s->plots[i++], 10, true, three_ratio, three_rgb, w);
    }
    if (reg_ready) {
        plot_set(&s->plots[i++], 11, true, reg_ratio, RGB_(128, 128, 128), reg_ratio < 30.0 ? 2 : 0);
    }
    if (mkt_ready) {
        plot_set(&s->plots[i++], 12, true, mkt_ratio, RGB_(128, 128, 128), mkt_ratio < 30.0 ? 2 : 0);
    }
    if (three_ready && m->span_peak > 0.0) {
        plot_set(&s->plots[i++], 13, true, span_ratio, RGB_(128, 128, 128), span_ratio <= 50.0 ? 1 : 0);
    }
    if (pos_use == -1) {
        plot_set(&s->plots[i++], 21, true, 103, RGB_(0, 0, 255), 1);
    } else if (pos_use == 1) {
        plot_set(&s->plots[i++], 21, true, -3, RGB_(255, 0, 0), 1);
    }
    if (!reset && three_ready && old->first_break == 1) {
        plot_set(&s->plots[i++], 22, true, -6, RGB_(180, 0, 0), 1);
    }
    if (!reset && three_ready && old->first_break == -1) {
        plot_set(&s->plots[i++], 23, true, 106, RGB_(0, 0, 150), 1);
    }
    if (!reset && in->fv_ok && old->compound == 1) {
        plot_set(&s->plots[i++], 24, true, -9, RGB_(255, 140, 0), 1);
    }
    if (!reset && in->fv_ok && old->px_exit == 1) {
        plot_set(&s->plots[i++], 25, true, -12, RGB_(255, 0, 255), 1);
    }
    if (!reset && in->fv_ok && old->px_exit == -1) {
        plot_set(&s->plots[i++], 26, true, 109, RGB_(0, 160, 160), 1);
    }
    if (scope_use == 1) {
        plot_set(&s->plots[i++], 27, true, -15, RGB_(255, 0, 0), 1);
    } else if (scope_use == -1) {
        plot_set(&s->plots[i++], 28, true, 112, RGB_(0, 0, 255), 1);
    }
    if (three_ready && three_ratio < 30.0 && (up_sig || dn_sig)) {
        plot_set(&s->plots[i++], 29, true, three_ratio, dot_rgb, 3);
    }
    if (three_ready && ratio_score > 0) {
        uint32_t emph = 0;
        if (three_rgb == RGB_(255, 160, 160) || three_rgb == RGB_(255, 0, 0) || three_rgb == RGB_(180, 0, 0)) {
            emph = RGB_(255, 255, 0);
        } else if (three_rgb == RGB_(128, 160, 255) || three_rgb == RGB_(0, 0, 255) || three_rgb == RGB_(0, 0, 150)) {
            emph = RGB_(0, 255, 255);
        } else if (three_rgb == RGB_(0, 128, 0)) {
            emph = RGB_(255, 255, 255);
        }
        if (emph != 0) {
            plot_set(&s->plots[i++], 30, true, three_ratio, emph, 3);
        }
    }
    if (in->fv_ok && !reset && old->vol_ready) {
        plot_set(&s->plots[i++], 31, true, old->vol_ratio, RGB_(0, 0, 255), 1);
    }
    if (in->fv_ok && !reset && old->both) {
        plot_set(&s->plots[i++], 32, true, -18, RGB_(255, 140, 0), 1);
    }
    plot_set(&s->plots[i++], 33, true, (double)scope_sum, RGB_(100, 100, 100), 1);
}
