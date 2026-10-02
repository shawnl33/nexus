#include "core/functions/fx_adx_v1.h"

#include <math.h>
#include <string.h>

bool tr_fxadx_init(tr_fxadx_t *s, int32_t period_input) {
    if (s == 0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    int32_t p = (int32_t)llround(fabs((double)period_input));
    if (p < 2) {
        p = 2;
    }
    s->period = (uint32_t)p;
    return true;
}

static void seed(tr_fxadx_t *s, const tr_fxadx_input_t *in) {
    memset(&s->st, 0, sizeof(s->st));
    s->st.bar_count = 1;
    s->st.sm_tr = in->high - in->low;
    s->st.prev_h = in->high;
    s->st.prev_l = in->low;
    s->st.prev_c = in->close;
    s->adx = 0.0;
    s->plus_di = 0.0;
    s->minus_di = 0.0;
    s->valid = false;
}

static void step(tr_fxadx_t *s, const tr_fxadx_input_t *in) {
    s->st.bar_count++;
    double up_move = in->high - s->st.prev_h;
    double down_move = s->st.prev_l - in->low;
    double up_dm = (up_move > down_move && up_move > 0.0) ? up_move : 0.0;
    double down_dm = (down_move > up_move && down_move > 0.0) ? down_move : 0.0;
    double tr = fmax(in->high - in->low,
                     fmax(fabs(in->high - s->st.prev_c), fabs(in->low - s->st.prev_c)));
    double n = (double)s->period;
    if (s->st.bar_count <= s->period) {
        s->st.sm_tr += tr;
        s->st.sm_up_dm += up_dm;
        s->st.sm_dn_dm += down_dm;
    } else {
        s->st.sm_tr = s->st.sm_tr - s->st.sm_tr / n + tr;
        s->st.sm_up_dm = s->st.sm_up_dm - s->st.sm_up_dm / n + up_dm;
        s->st.sm_dn_dm = s->st.sm_dn_dm - s->st.sm_dn_dm / n + down_dm;
    }
    double plus_di = 0.0, minus_di = 0.0;
    if (s->st.sm_tr > 0.0) {
        plus_di = s->st.sm_up_dm / s->st.sm_tr * 100.0;
        minus_di = s->st.sm_dn_dm / s->st.sm_tr * 100.0;
    }
    double dx = 0.0;
    double di_sum = plus_di + minus_di;
    if (di_sum > 0.0) {
        dx = fabs(plus_di - minus_di) / di_sum * 100.0;
    }
    if (s->st.bar_count > s->period && s->st.bar_count < s->period * 2) {
        s->st.dx_sum += dx;
        s->st.adx_state = 0.0;
        s->st.valid_state = false;
    } else if (s->st.bar_count == s->period * 2) {
        s->st.dx_sum += dx;
        s->st.adx_state = s->st.dx_sum / n;
        s->st.valid_state = true;
    } else if (s->st.bar_count > s->period * 2) {
        s->st.adx_state = (s->st.adx_state * (double)(s->period - 1) + dx) / n;
        s->st.valid_state = true;
    }
    s->st.prev_h = in->high;
    s->st.prev_l = in->low;
    s->st.prev_c = in->close;
    if (s->st.valid_state) {
        s->adx = s->st.adx_state;
        s->plus_di = plus_di;
        s->minus_di = minus_di;
        s->valid = true;
    } else {
        s->adx = 0.0;
        s->plus_di = 0.0;
        s->minus_di = 0.0;
        s->valid = false;
    }
}

void tr_fxadx_eval(tr_fxadx_t *s, const tr_fxadx_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    bool is_new = !s->has_bar || in->bar_open != s->last_open;
    if (!is_new) {
        s->st = s->prev;
    } else if (s->has_bar) {
        s->prev = s->st;
    }
    if (!s->has_bar || in->session_reset) {
        seed(s, in);
    } else {
        step(s, in);
    }
    if (is_new) {
        s->last_open = in->bar_open;
        s->has_bar = true;
    }
}
