#include "core/functions/fx_session_profile_v2.h"

#include <math.h>
#include <string.h>

void tr_fxprof_init(tr_fxprof_t *s) {
    if (s == 0) {
        return;
    }
    memset(s, 0, sizeof(*s));
}

static void push_front(tr_fxprof_mem_t *m, double tp, double w) {
    if (m->n < TR_FXPROF_CAP) {
        m->n++;
    }
    for (int i = m->n - 1; i > 0; i--) {
        m->tp[i] = m->tp[i - 1];
        m->w[i] = m->w[i - 1];
    }
    m->tp[0] = tp;
    m->w[0] = w;
}

void tr_fxprof_eval(tr_fxprof_t *s, const tr_fxprof_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    bool is_new = !s->has_bar || in->bar_open != s->last_open;
    if (is_new) {
        s->prev = s->st;
        s->has_bar = true;
        s->last_open = in->bar_open;
    } else {
        s->st = s->prev;
    }

    memset(&s->out, 0, sizeof(s->out));
    double mult = fabs(in->value_mult);
    if (mult <= 0.0) {
        mult = 1.5;
    }
    int32_t min_bars = (int32_t)fabs((double)in->min_bars);
    if (min_bars < 1) {
        min_bars = 1;
    }
    double ps = in->price_scale > 0.0 ? in->price_scale : 1.0;
    double tp = (in->high + in->low + in->close) / 3.0;
    double w = in->volume > 0.0 ? in->volume : 1.0;

    tr_fxprof_mem_t *m = &s->st;
    double center = tp;
    if (in->session_reset) {
        m->n = 0;
        m->bars = 1;
        m->cum_w = w;
        m->cum_pv = tp * w;
        m->cum_dev = 0.0;
        center = tp;
    } else {
        m->bars += 1;
        m->cum_w += w;
        m->cum_pv += tp * w;
        center = m->cum_w > 0.0 ? m->cum_pv / m->cum_w : tp;
        m->cum_dev += fabs(tp - center) * w;
    }
    push_front(m, tp, w);

    double dev = m->cum_w > 0.0 ? m->cum_dev / m->cum_w : 0.0;
    if (in->period > 0 && m->n > 0) {
        int win = in->period;
        if (win > m->bars) {
            win = m->bars;
        }
        if (win > m->n) {
            win = m->n;
        }
        double sw = 0.0, sp = 0.0;
        for (int j = 0; j < win; j++) {
            sw += m->w[j];
            sp += m->tp[j] * m->w[j];
        }
        if (sw > 0.0) {
            center = sp / sw;
            double sd = 0.0;
            for (int j = 0; j < win; j++) {
                sd += fabs(m->tp[j] - center) * m->w[j];
            }
            dev = sd / sw;
        }
    }
    if (dev < ps) {
        dev = ps;
    }

    s->out.center = center;
    s->out.hi1 = center + dev * mult;
    s->out.lo1 = center - dev * mult;
    s->out.hi2 = center + dev * mult * 2.0;
    s->out.lo2 = center - dev * mult * 2.0;
    s->out.session_bars = m->bars;
    if (m->bars >= min_bars) {
        s->out.valid = 1;
        if (in->close > s->out.hi2) {
            s->out.state5 = 2;
        } else if (in->close > s->out.hi1) {
            s->out.state5 = 1;
        } else if (in->close < s->out.lo2) {
            s->out.state5 = -2;
        } else if (in->close < s->out.lo1) {
            s->out.state5 = -1;
        }
    }
}
