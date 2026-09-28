#include "core/indicators/auto_session_adx_v1.h"

#include <math.h>
#include <string.h>

bool tr_adx1_init(tr_adx1_t *s, int32_t period_input) {
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

void tr_adx1_on_bar(tr_adx1_t *s, double h, double l, double c, bool session_first) {
    if (s == 0) {
        return;
    }
    if (session_first || s->bar_count == 0) {
        /* 세션 첫 봉: 평활TR = H−L 시드, DM 0 (원본 47~62) */
        s->bar_count = 1;
        s->sm_tr = h - l;
        s->sm_up_dm = 0.0;
        s->sm_dn_dm = 0.0;
        s->dx_sum = 0.0;
        s->adx_state = 0.0;
        s->valid_state = false;
        s->prev_h = h;
        s->prev_l = l;
        s->prev_c = c;
        s->adx = 0.0;
        s->plus_di = 0.0;
        s->minus_di = 0.0;
        s->valid = false;
        return;
    }

    s->bar_count++;
    double up_move = h - s->prev_h;
    double down_move = s->prev_l - l;
    double up_dm = (up_move > down_move && up_move > 0.0) ? up_move : 0.0;
    double down_dm = (down_move > up_move && down_move > 0.0) ? down_move : 0.0;
    double hl = h - l;
    double hc = fabs(h - s->prev_c);
    double lc = fabs(l - s->prev_c);
    double tr = fmax(hl, fmax(hc, lc));

    if (s->bar_count <= s->period) {
        s->sm_tr += tr;
        s->sm_up_dm += up_dm;
        s->sm_dn_dm += down_dm;
    } else {
        double n = (double)s->period;
        s->sm_tr = s->sm_tr - s->sm_tr / n + tr;
        s->sm_up_dm = s->sm_up_dm - s->sm_up_dm / n + up_dm;
        s->sm_dn_dm = s->sm_dn_dm - s->sm_dn_dm / n + down_dm;
    }

    double plus_di = 0.0, minus_di = 0.0;
    if (s->sm_tr > 0.0) {
        plus_di = s->sm_up_dm / s->sm_tr * 100.0;
        minus_di = s->sm_dn_dm / s->sm_tr * 100.0;
    }
    double dx = 0.0;
    double di_sum = plus_di + minus_di;
    if (di_sum > 0.0) {
        dx = fabs(plus_di - minus_di) / di_sum * 100.0;
    }

    if (s->bar_count > s->period && s->bar_count <= s->period * 2) {
        s->dx_sum += dx;
    }
    if (s->bar_count == s->period * 2) {
        s->adx_state = s->dx_sum / (double)s->period;
        s->valid_state = true;
    } else if (s->bar_count > s->period * 2) {
        s->adx_state = (s->adx_state * (double)(s->period - 1) + dx) / (double)s->period;
        s->valid_state = true;
    }

    s->prev_h = h;
    s->prev_l = l;
    s->prev_c = c;

    if (s->valid_state) {
        s->adx = s->adx_state;
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
