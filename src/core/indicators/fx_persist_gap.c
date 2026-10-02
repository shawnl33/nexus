#include "core/indicators/fx_persist_gap.h"

#include <math.h>
#include <string.h>

#define RGB_(r, g, b) ((uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))

void tr_fxpgap_init(tr_fxpgap_t *s, int n_lines, double emphasize) {
    if (s == 0) {
        return;
    }
    memset(s, 0, sizeof(*s));
    s->cfg.n_lines = (n_lines == 4 || n_lines == 5) ? n_lines : 3;
    s->cfg.emphasize = emphasize > 0.0 ? emphasize : 25.0;
}

void tr_fxpgap_eval(tr_fxpgap_t *s, const tr_fxpgap_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    bool is_new = !s->has_bar || in->bar_open != s->last_open;
    if (is_new) {
        s->snap_peak = s->peak;
        s->snap_prev_peak = s->prev_peak;
        s->snap_sess_low = s->sess_low;
        s->snap_gap = s->gap;
        s->snap_cnt = s->cnt;
        s->snap_ready = s->ready;
        s->snap_has = s->has_bar;
        s->snap_below = s->below;
        s->snap_above = s->above;
        s->snap_ratio = s->out.ratio;
    } else {
        s->peak = s->snap_peak;
        s->prev_peak = s->snap_prev_peak;
        s->sess_low = s->snap_sess_low;
        s->gap = s->snap_gap;
        s->cnt = s->snap_cnt;
        s->ready = s->snap_ready;
        s->has_bar = s->snap_has;
        s->below = s->snap_below;
        s->above = s->snap_above;
    }
    bool first = !s->has_bar;
    if (in->session_reset) {
        s->peak = 0.0;
        s->prev_peak = first ? 0.0 : s->snap_peak;
        s->sess_low = in->low;
        s->cnt = 0.0;
    } else {
        s->peak = s->snap_peak;
        s->prev_peak = s->snap_prev_peak;
        s->sess_low = s->has_bar ? fmin(s->snap_sess_low, in->low) : in->low;
    }
    int n = s->cfg.n_lines;
    int ready = 1;
    for (int i = 0; i < n; i++) {
        if (in->target[i] == 0.0) {
            ready = 0;
        }
    }
    double hi = 0.0, lo = 0.0, gap = 0.0, ratio = 0.0, prev_ratio = 0.0;
    if (ready) {
        hi = in->target[0];
        lo = in->target[0];
        for (int i = 1; i < n; i++) {
            if (in->target[i] > hi) {
                hi = in->target[i];
            }
            if (in->target[i] < lo) {
                lo = in->target[i];
            }
        }
        gap = hi - lo;
        s->peak = fmax(s->peak, gap);
        if (s->peak > 0.0) {
            ratio = gap / s->peak * 100.0;
        }
        if (s->prev_peak > 0.0) {
            prev_ratio = gap / s->prev_peak * 100.0;
        }
        if (in->session_reset || s->snap_ready == 0 || gap != s->snap_gap) {
            s->cnt = 1.0;
        } else {
            s->cnt = s->snap_cnt + 1.0;
        }
    } else {
        s->cnt = 0.0;
    }
    s->gap = gap;
    s->ready = ready;

    uint32_t c4 = RGB_(128, 128, 128);
    int w4 = ratio < s->cfg.emphasize ? 2 : 1;
    if (s->sess_low > lo) {
        c4 = RGB_(255, 0, 255);
    } else if (ratio < s->cfg.emphasize && in->high < lo) {
        c4 = RGB_(0, 0, 255);
    } else if (ratio < s->cfg.emphasize && in->low > hi) {
        c4 = RGB_(255, 0, 0);
    }
    double gate = in->target[n - 1];
    uint32_t c5 = (s->sess_low > gate && in->low > hi) ? RGB_(255, 0, 0) : RGB_(128, 128, 128);

    memset(&s->out, 0, sizeof(s->out));
    s->out.ready = ready;
    s->out.gap = gap;
    s->out.peak = s->peak;
    s->out.prev_peak = s->prev_peak;
    s->out.ratio = ratio;
    s->out.prev_ratio = prev_ratio;
    s->out.cnt = s->cnt;
    s->out.plot3 = ready && s->prev_peak > 0.0;
    s->out.plot5 = ready && s->prev_peak > 0.0;
    s->out.rgb4 = c4;
    s->out.rgb5 = c5;
    s->out.width4 = w4;
    s->out.hi = hi;
    s->out.lo = lo;
    if (in->session_reset || s->snap_ready == 0 || ratio != s->snap_ratio) {
        s->below = 0;
        s->above = 0;
    } else {
        s->below = s->snap_below;
        s->above = s->snap_above;
    }
    if (ready && in->close < hi) {
        s->below = 1;
    }
    if (ready && in->close > lo) {
        s->above = 1;
    }
    s->last_open = in->bar_open;
    s->has_bar = true;
}

void tr_fxunion_paint(tr_fxpgap_t *s, int cross, int other_a_ready, double other_a_ratio,
                      int other_b_ready, double other_b_ratio, double high, double low) {
    if (s == 0 || !s->out.ready) {
        return;
    }
    uint32_t col = RGB_(128, 128, 128);
    int w = 0;
    if (s->below == 0) {
        col = RGB_(255, 0, 0);
    } else if (s->above == 0) {
        col = RGB_(0, 0, 255);
    }
    if (s->out.ratio < 30.0) {
        w = 2;
    } else if (cross && (s->below == 0 || s->above == 0) && other_a_ready && other_b_ready &&
               other_a_ratio < 30.0 && other_b_ratio < 30.0) {
        w = 1;
    }
    if (cross && s->out.ratio < 25.0) {
        if (low > s->out.hi) {
            col = RGB_(180, 0, 0);
            w = 3;
        } else if (high < s->out.lo) {
            col = RGB_(0, 0, 150);
            w = 3;
        }
    }
    s->out.union_rgb = col;
    s->out.union_w = w;
}
