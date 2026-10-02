#include "core/functions/osf_vol_flow_v1.h"

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

void tr_osf_flow_init(tr_osf_flow_t *s, double interest, double strong, int reverse, int32_t period) {
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
    if (period > TR_OSF_FLOW_CAP) {
        period = TR_OSF_FLOW_CAP;
    }
    s->cfg.interest = interest;
    s->cfg.strong = strong;
    s->cfg.reverse = reverse;
    s->cfg.period = period;
}

void tr_osf_flow_eval(tr_osf_flow_t *s, const tr_osf_flow_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    bool is_new = !s->has_bar || in->bar_open != s->last_open;
    if (!is_new && s->n > 0) {
        s->n--;
        if (s->sn > 0) {
            s->sn--;
        }
    }
    double range = in->high - in->low;
    double mfm = 0.0;
    if (range > 0.0) {
        mfm = ((in->close - in->low) - (in->high - in->close)) / range;
    }
    double mfv = mfm * in->volume;
    if (s->n < TR_OSF_FLOW_CAP) {
        s->n++;
    }
    for (int i = s->n - 1; i > 0; i--) {
        s->mfv[i] = s->mfv[i - 1];
        s->vol[i] = s->vol[i - 1];
    }
    s->mfv[0] = mfv;
    s->vol[0] = in->volume;
    int use = s->n < s->cfg.period ? s->n : s->cfg.period;
    double sum_m = 0.0, sum_v = 0.0;
    for (int i = 0; i < use; i++) {
        sum_m += s->mfv[i];
        sum_v += s->vol[i];
    }
    double score = 0.0;
    if (use > 0 && sum_v > 0.0) {
        score = (sum_m / (double)use) / (sum_v / (double)use) * 100.0;
    }
    if (s->cfg.reverse) {
        score = -score;
    }
    if (s->sn < 8) {
        s->sn++;
    }
    for (int i = s->sn - 1; i > 0; i--) {
        s->score_h[i] = s->score_h[i - 1];
    }
    s->score_h[0] = score;
    double slope = 0.0;
    if (s->n >= s->cfg.period + 3 && s->sn >= 4) {
        slope = clamp100(score - s->score_h[3]);
    }
    s->score = score;
    s->slope3 = slope;
    s->state = state_of(score, s->cfg.strong, s->cfg.interest);
    s->valid = s->n >= s->cfg.period;
    s->last_open = in->bar_open;
    s->has_bar = true;
}
