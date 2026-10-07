#include "core/functions/fx_squeeze_v1.h"

#include <math.h>
#include <string.h>

void tr_fxsq_init(tr_fxsq_t *s) {
    if (s == 0) {
        return;
    }
    memset(s, 0, sizeof(*s));
    tr_fxprof_init(&s->prof);
}

static void push_width(tr_fxsq_mem_t *m, double w) {
    if (m->n < TR_FXSQ_CAP) {
        m->n++;
    }
    for (int i = m->n - 1; i > 0; i--) {
        m->width[i] = m->width[i - 1];
    }
    m->width[0] = w;
}

void tr_fxsq_eval(tr_fxsq_t *s, const tr_fxsq_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    bool is_new = !s->has_bar || in->bar_open != s->last_open;
    if (is_new) {
        s->prev = s->st;
        s->has_bar = true;
        s->last_open = in->bar_open;
        s->evals++;
    } else {
        s->st = s->prev;
    }
    memset(&s->out, 0, sizeof(s->out));

    tr_fxprof_input_t pin;
    memset(&pin, 0, sizeof(pin));
    pin.session_reset = in->session_reset;
    pin.is_new_bar = in->is_new_bar;
    pin.bar_open = in->bar_open;
    pin.high = in->high;
    pin.low = in->low;
    pin.close = in->close;
    pin.volume = in->volume;
    pin.price_scale = in->price_scale;
    pin.value_mult = in->value_mult;
    pin.min_bars = in->min_bars;
    pin.period = in->width_bars > 0 ? in->width_bars : 1;
    tr_fxprof_eval(&s->prof, &pin);

    double width = 0.0;
    if (s->prof.out.valid) {
        width = s->prof.out.hi1 - s->prof.out.lo1;
    }
    tr_fxsq_mem_t *m = &s->st;
    if (in->session_reset) {
        m->bars = 1;
    } else {
        m->bars += 1;
    }
    push_width(m, width);

    int ratio_n = in->ratio_bars > 0 ? in->ratio_bars : 1;
    if (ratio_n > m->bars) {
        ratio_n = m->bars;
    }
    if (ratio_n > m->n) {
        ratio_n = m->n;
    }
    double max_w = 0.0;
    for (int j = 0; j < ratio_n; j++) {
        if (m->width[j] > max_w) {
            max_w = m->width[j];
        }
    }
    double ratio = max_w > 0.0 ? width / max_w * 100.0 : 0.0;
    int narrow = s->prof.out.valid && max_w > 0.0 && m->bars >= in->width_bars &&
                 ratio < in->narrow_pct;

    int hold_prev = m->hold;
    int hold_prev2 = m->hold_1;
    int narrow_prev = m->narrow;
    int reset_prev = m->reset;
    double hi_prev = m->band_hi;
    double lo_prev = m->band_lo;
    double hi_prev2 = m->band_hi_1;
    double lo_prev2 = m->band_lo_1;
    double close_prev = m->close;

    int hold = 0;
    if (in->session_reset) {
        hold = narrow;
    } else if (narrow) {
        hold = hold_prev + 1;
    }

    int release = 0, dir = 0, len = 0;
    double rel_hi = 0.0, rel_lo = 0.0;
    int cur_bar = s->evals;
    if (in->confirm_closed) {
        if (cur_bar > 2 && !in->session_reset && !reset_prev && !narrow_prev &&
            hold_prev2 >= in->stage1_bars) {
            release = 1;
            len = hold_prev2;
            rel_hi = hi_prev2;
            rel_lo = lo_prev2;
            if (close_prev > rel_hi) {
                dir = 1;
            } else if (close_prev < rel_lo) {
                dir = -1;
            }
        }
    } else if (cur_bar > 1 && !in->session_reset && !narrow && hold_prev >= in->stage1_bars) {
        release = 1;
        len = hold_prev;
        rel_hi = hi_prev;
        rel_lo = lo_prev;
        if (in->close > rel_hi) {
            dir = 1;
        } else if (in->close < rel_lo) {
            dir = -1;
        }
    }

    int wait_on = m->wait_on;
    double wait_hi = m->wait_hi, wait_lo = m->wait_lo;
    int wait_age = m->wait_age, wait_len = m->wait_len;
    if (in->session_reset) {
        wait_on = 0;
    }
    int confirm = 0;
    double judge_px = in->confirm_closed ? close_prev : in->close;
    if (wait_on) {
        wait_age += 1;
        if (judge_px > wait_hi) {
            confirm = 1;
        } else if (judge_px < wait_lo) {
            confirm = -1;
        }
        if (confirm != 0 || wait_age >= in->confirm_bars) {
            wait_on = 0;
        }
    }
    if (release && dir == 0 && in->confirm_bars > 0) {
        wait_on = 1;
        wait_hi = rel_hi;
        wait_lo = rel_lo;
        wait_age = 0;
        wait_len = len;
    }

    m->hold_1 = hold_prev;
    m->hold = hold;
    m->narrow = narrow;
    m->reset_1 = reset_prev;
    m->reset = in->session_reset ? 1 : 0;
    m->band_hi_1 = hi_prev;
    m->band_lo_1 = lo_prev;
    m->band_hi = s->prof.out.hi1;
    m->band_lo = s->prof.out.lo1;
    m->close = in->close;
    m->wait_on = wait_on;
    m->wait_hi = wait_hi;
    m->wait_lo = wait_lo;
    m->wait_age = wait_age;
    m->wait_len = wait_len;

    /* WSF_FXSqueezeV4 박스 돌파. 여유·재무장 입력이 0이어도 박스 기억은 유지한다. */
    int box_on = m->box_on, box_done = m->box_done, box_age = m->box_age, box_len_st = m->box_len;
    double box_hi = m->box_hi, box_lo = m->box_lo;
    int rearm_on = m->rearm_on, rearm_age = m->rearm_age, rearm_len = m->rearm_len;
    double rearm_hi = m->rearm_hi, rearm_lo = m->rearm_lo;
    int brk_prev_dir = m->brk_dir, brk_prev_len = m->brk_len;
    double brk_prev_hi = m->brk_hi, brk_prev_lo = m->brk_lo;
    if (in->session_reset) {
        box_on = 0;
        rearm_on = 0;
    }
    if (rearm_on && !in->session_reset) {
        rearm_age += 1;
        if (in->close <= rearm_hi && in->close >= rearm_lo) {
            box_on = 1;
            box_done = 0;
            box_age = 0;
            box_hi = rearm_hi;
            box_lo = rearm_lo;
            box_len_st = rearm_len;
            rearm_on = 0;
        } else if (rearm_age >= in->rearm_bars) {
            rearm_on = 0;
        }
    }
    int brk_dir = 0, brk_len = 0;
    double brk_hi = 0.0, brk_lo = 0.0;
    double slack = (in->break_ticks > 0.0 ? in->break_ticks : 0.0) *
                   (in->price_scale > 0.0 ? in->price_scale : 1.0);
    if (box_on && !box_done && !in->session_reset) {
        if (in->close > box_hi + slack) {
            brk_dir = 1;
        } else if (in->close < box_lo - slack) {
            brk_dir = -1;
        }
        if (brk_dir != 0) {
            brk_len = box_len_st;
            brk_hi = box_hi;
            brk_lo = box_lo;
            box_done = 1;
            box_on = 0;
            if (in->rearm_bars > 0) {
                rearm_on = 1;
                rearm_age = 0;
                rearm_hi = box_hi;
                rearm_lo = box_lo;
                rearm_len = box_len_st;
            }
        }
    }
    if (hold >= in->stage1_bars) {
        if (hold == in->stage1_bars || !box_on) {
            if (hold == in->stage1_bars || !box_done) {
                box_on = 1;
                box_done = 0;
            }
        }
        if (box_on) {
            box_hi = s->prof.out.hi1;
            box_lo = s->prof.out.lo1;
            box_age = 0;
            box_len_st = hold;
        }
    } else if (box_on) {
        box_age += 1;
        if (box_age > in->confirm_bars) {
            box_on = 0;
        }
    }
    m->box_on = box_on;
    m->box_done = box_done;
    m->box_age = box_age;
    m->box_len = box_len_st;
    m->box_hi = box_hi;
    m->box_lo = box_lo;
    m->rearm_on = rearm_on;
    m->rearm_age = rearm_age;
    m->rearm_len = rearm_len;
    m->rearm_hi = rearm_hi;
    m->rearm_lo = rearm_lo;
    m->brk_dir = brk_dir;
    m->brk_len = brk_len;
    m->brk_hi = brk_hi;
    m->brk_lo = brk_lo;

    s->out.hold = hold;
    s->out.ratio = ratio;
    s->out.narrow = narrow;
    s->out.release = release;
    s->out.release_dir = dir;
    s->out.release_len = confirm != 0 ? wait_len : len;
    s->out.confirm_dir = confirm;
    s->out.confirm_len = wait_len;
    s->out.band_hi = s->prof.out.hi1;
    s->out.band_lo = s->prof.out.lo1;
    s->out.profile_valid = s->prof.out.valid;
    if (in->confirm_closed) {
        s->out.box_dir = (!in->session_reset && s->evals > 1) ? brk_prev_dir : 0;
        s->out.box_len = s->out.box_dir ? brk_prev_len : 0;
        s->out.box_hi = s->out.box_dir ? brk_prev_hi : 0.0;
        s->out.box_lo = s->out.box_dir ? brk_prev_lo : 0.0;
    } else {
        s->out.box_dir = brk_dir;
        s->out.box_len = brk_len;
        s->out.box_hi = brk_hi;
        s->out.box_lo = brk_lo;
    }
}
