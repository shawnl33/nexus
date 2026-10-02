#include "core/functions/fx_gap_v1.h"

#include <math.h>
#include <string.h>

static int32_t clamp_n(int32_t n) {
    if (n < 5) {
        return 5;
    }
    if (n > 10) {
        return 10;
    }
    return n;
}

static int32_t hhmmss_min(int64_t t) {
    int hh = (int)(t / 10000);
    int mm = (int)((t / 100) % 100);
    return hh * 60 + mm;
}

static int64_t session_start_time(int64_t stime) {
    if (stime >= 70000 && stime < 153000) {
        return 70000;
    }
    return 153000;
}

void tr_fxgap_init(tr_fxgap_t *s, const tr_fxgap_config_t *cfg) {
    if (s == 0) {
        return;
    }
    memset(s, 0, sizeof(*s));
    if (cfg != 0) {
        s->cfg = *cfg;
    }
    s->cfg.volatility_period = clamp_n(s->cfg.volatility_period);
}

static void shift_tr(tr_fxgap_t *s, double tr) {
    for (int j = 18; j >= 0; j--) {
        s->trs[j + 1] = s->trs[j];
    }
    s->trs[0] = tr;
    if (s->tr_count < 20) {
        s->tr_count++;
    }
    s->completed++;
}

static void start_session(tr_fxgap_t *s, const tr_fxgap_input_t *in, bool full) {
    s->sess_open = in->open;
    s->sess_high = in->high;
    s->sess_low = in->low;
    s->sess_close = in->close;
    s->sess_start_hhmmss = session_start_time(in->time_hhmmss);
    s->full_start = full;
    s->last_start_bar = in->bar_index;
}

static void publish(tr_fxgap_t *s, const tr_fxgap_input_t *in) {
    s->gap_ratio = 0.0;
    s->gap_grade = 0;
    s->daily_weight = 0.0;
    s->gap_dir = 0;
    s->valid = false;
    int32_t cur = hhmmss_min(in->time_hhmmss);
    int32_t start = hhmmss_min(s->sess_start_hhmmss);
    int32_t elapsed = cur - start;
    if (elapsed < 0) {
        elapsed += 1440;
    }
    s->elapsed_min = elapsed;
    int n = s->cfg.volatility_period;
    if (s->tr_count < n || s->prev_close <= 0.0) {
        return;
    }
    double avg = 0.0;
    for (int j = 0; j < n; j++) {
        avg += s->trs[j];
    }
    avg /= (double)n;
    if (avg <= 0.0) {
        return;
    }
    s->gap_ratio = fabs(s->sess_open - s->prev_close) / avg;
    if (s->sess_open > s->prev_close) {
        s->gap_dir = 1;
    } else if (s->sess_open < s->prev_close) {
        s->gap_dir = -1;
    }
    s->gap_grade = 0;
    s->daily_weight = 1.0;
    if (s->gap_ratio >= s->cfg.mid_gap) {
        s->gap_grade = 1;
        s->daily_weight = 0.5;
    }
    if (s->gap_ratio >= s->cfg.big_gap) {
        s->gap_grade = 2;
        s->daily_weight = 0.0;
    }
    s->valid = true;
}

void tr_fxgap_eval(tr_fxgap_t *s, const tr_fxgap_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    if (!s->initialized) {
        start_session(s, in, in->time_hhmmss == 70000 || in->time_hhmmss == 153000);
        s->initialized = true;
    } else if (in->session_reset && in->bar_index != s->last_start_bar) {
        if (s->full_start) {
            s->prev_prev_close = s->prev_close;
            s->prev_close = s->sess_close;
            double tr = s->sess_high - s->sess_low;
            if (s->completed != 0) {
                tr = fmax(tr, fmax(fabs(s->sess_high - s->prev_prev_close),
                                   fabs(s->sess_low - s->prev_prev_close)));
            }
            shift_tr(s, tr);
        }
        start_session(s, in, true);
    } else {
        if (in->high > s->sess_high) {
            s->sess_high = in->high;
        }
        if (in->low < s->sess_low) {
            s->sess_low = in->low;
        }
        s->sess_close = in->close;
    }
    publish(s, in);
}
