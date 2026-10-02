#include "core/functions/osf_clv_vol_flow_v1.h"

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

void tr_osf_combo_init(tr_osf_combo_t *s, double interest, double strong, int reverse, int32_t period) {
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
    if (period < 2) {
        period = 2;
    }
    s->interest = interest;
    s->strong = strong;
    s->reverse = reverse;
    s->period = period;
    tr_osf_clv_init(&s->clv, interest, strong, 0);
    tr_osf_flow_init(&s->flow, interest, strong, 0, period);
}

void tr_osf_combo_eval(tr_osf_combo_t *s, const tr_osf_flow_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    bool is_new = !s->has_bar || in->bar_open != s->last_open;
    if (!is_new && s->sn > 0) {
        s->sn--;
    }
    tr_osf_clv_input_t cin = {in->high, in->low, in->close, in->bar_open};
    tr_osf_clv_eval(&s->clv, &cin);
    tr_osf_flow_eval(&s->flow, in);
    double combined = 0.0;
    if (s->flow.score == 0.0) {
        combined = 0.0;
    } else if ((s->clv.score > 0.0 && s->flow.score > 0.0) || (s->clv.score < 0.0 && s->flow.score < 0.0)) {
        combined = s->flow.score * 1.2;
    } else {
        combined = s->flow.score * 0.5;
    }
    if (s->reverse) {
        combined = -combined;
    }
    combined = clamp100(combined);
    if (s->sn < 8) {
        s->sn++;
    }
    for (int i = s->sn - 1; i > 0; i--) {
        s->score_h[i] = s->score_h[i - 1];
    }
    s->score_h[0] = combined;
    double slope = 0.0;
    if (s->sn >= s->period + 3) {
        slope = clamp100(combined - s->score_h[3]);
    }
    s->score = combined;
    s->slope3 = slope;
    s->state = state_of(combined, s->strong, s->interest);
    s->valid = s->flow.valid;
    s->last_open = in->bar_open;
    s->has_bar = true;
}
