#include "core/functions/osf_clv_pressure_v1.h"

#include <string.h>

static int state_of(double score, double strong, double interest) {
    if (score >= strong) {
        return 2;
    }
    if (score >= interest) {
        return 1;
    }
    if (score <= -strong) {
        return -2;
    }
    if (score <= -interest) {
        return -1;
    }
    return 0;
}

static double clamp100(double v) {
    if (v > 100.0) {
        return 100.0;
    }
    if (v < -100.0) {
        return -100.0;
    }
    return v;
}

void tr_osf_clv_init(tr_osf_clv_t *s, double interest, double strong, int reverse) {
    if (s == 0) {
        return;
    }
    memset(s, 0, sizeof(*s));
    if (interest < 0) {
        interest = -interest;
    }
    if (strong < 0) {
        strong = -strong;
    }
    if (interest < 1.0) {
        interest = 1.0;
    }
    if (strong < interest) {
        strong = interest;
    }
    s->cfg.interest = interest;
    s->cfg.strong = strong;
    s->cfg.reverse = reverse;
}

void tr_osf_clv_eval(tr_osf_clv_t *s, const tr_osf_clv_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    bool is_new = !s->has_bar || in->bar_open != s->last_open;
    if (!is_new && s->n > 0) {
        s->n--;
    }
    double range = in->high - in->low;
    double raw = 0.0;
    if (range > 0.0) {
        raw = ((in->close - in->low) - (in->high - in->close)) / range * 100.0;
    }
    if (s->cfg.reverse) {
        raw = -raw;
    }
    if (s->n < 8) {
        s->n++;
    }
    for (int i = s->n - 1; i > 0; i--) {
        s->raw[i] = s->raw[i - 1];
    }
    s->raw[0] = raw;
    double avg3 = raw;
    if (s->n >= 3) {
        avg3 = (s->raw[0] + s->raw[1] + s->raw[2]) / 3.0;
    }
    double avg5 = avg3;
    if (s->n >= 5) {
        avg5 = (s->raw[0] + s->raw[1] + s->raw[2] + s->raw[3] + s->raw[4]) / 5.0;
    }
    double score = (raw * 60.0 + avg3 * 25.0 + avg5 * 15.0) / 100.0;
    for (int i = s->n - 1; i > 0; i--) {
        s->score_h[i] = s->score_h[i - 1];
    }
    s->score_h[0] = score;
    double slope = 0.0;
    if (s->n >= 4) {
        slope = clamp100(score - s->score_h[3]);
    }
    s->score = score;
    s->slope3 = slope;
    s->state = state_of(score, s->cfg.strong, s->cfg.interest);
    s->valid = 1;
    s->last_open = in->bar_open;
    s->has_bar = true;
}
