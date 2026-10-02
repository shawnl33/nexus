#include "core/indicators/fx_yangmae.h"

#include <math.h>
#include <string.h>

static void snap_save(tr_fxymae_t *s) {
    s->s_cnt = s->cnt;
    s->s_prev_cnt = s->prev_cnt;
    s->s_cur_hi = s->cur_hi;
    s->s_cur_lo = s->cur_lo;
    s->s_prev_hi = s->prev_hi;
    s->s_prev_lo = s->prev_lo;
    s->s_width = s->width;
    s->s_prev_valid = s->prev_valid;
    s->s_broke = s->broke;
    s->s_ready = s->ready;
    s->s_two_max = s->two_max;
    s->s_vol_cur = s->vol_cur;
    s->s_vol_prev = s->vol_prev;
    s->s_vol_base = s->vol_base;
    s->s_h2_wait = s->h2_wait;
    s->s_h2_elapsed = s->h2_elapsed;
    s->s_h2_up = s->h2_up;
    s->s_h2_dn = s->h2_dn;
    s->s_h1 = s->h1;
    s->s_h2 = s->h2;
    s->s_h3 = s->h3;
    s->s_h4 = s->h4;
    s->s_first = s->first;
    s->s_cond = s->cond;
    s->s_low = s->prev_low;
    s->s_high = s->prev_high;
    s->s_pvc_price_on = s->pvc_price_on;
    s->s_pvc_vol_on = s->pvc_vol_on;
    s->s_pvc_both = s->pvc_both;
    s->s_pvc_price_ratio = s->pvc_price_ratio;
    s->s_pvc_vol_ratio = s->pvc_vol_ratio;
    s->s_has = s->has_bar;
}

static void snap_load(tr_fxymae_t *s) {
    s->cnt = s->s_cnt;
    s->prev_cnt = s->s_prev_cnt;
    s->cur_hi = s->s_cur_hi;
    s->cur_lo = s->s_cur_lo;
    s->prev_hi = s->s_prev_hi;
    s->prev_lo = s->s_prev_lo;
    s->width = s->s_width;
    s->prev_valid = s->s_prev_valid;
    s->broke = s->s_broke;
    s->ready = s->s_ready;
    s->two_max = s->s_two_max;
    s->vol_cur = s->s_vol_cur;
    s->vol_prev = s->s_vol_prev;
    s->vol_base = s->s_vol_base;
    s->h2_wait = s->s_h2_wait;
    s->h2_elapsed = s->s_h2_elapsed;
    s->h2_up = s->s_h2_up;
    s->h2_dn = s->s_h2_dn;
    s->h1 = s->s_h1;
    s->h2 = s->s_h2;
    s->h3 = s->s_h3;
    s->h4 = s->s_h4;
    s->first = s->s_first;
    s->cond = s->s_cond;
    s->prev_low = s->s_low;
    s->prev_high = s->s_high;
    s->pvc_price_on = s->s_pvc_price_on;
    s->pvc_vol_on = s->s_pvc_vol_on;
    s->pvc_both = s->s_pvc_both;
    s->pvc_price_ratio = s->s_pvc_price_ratio;
    s->pvc_vol_ratio = s->s_pvc_vol_ratio;
    s->has_bar = s->s_has;
}

void tr_fxymae_default_config(tr_fxymae_config_t *cfg, double price_scale) {
    if (cfg == 0) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->price_scale = price_scale;
    cfg->slack_ticks = 0;
    cfg->price_limit = 40;
    cfg->reg_limit = 30;
    cfg->mkt_limit = 30;
    cfg->wait_bars = 10;
    cfg->vol_drop_limit = 100;
    cfg->vol_rise_limit = 100;
    cfg->vol_min_bars = 1;
    cfg->mark_ticks = 3;
}

void tr_fxymae_init(tr_fxymae_t *s, const tr_fxymae_config_t *cfg) {
    if (s == 0) {
        return;
    }
    memset(s, 0, sizeof(*s));
    if (cfg != 0) {
        s->cfg = *cfg;
    }
    if (s->cfg.wait_bars < 1) {
        s->cfg.wait_bars = 1;
    }
    if (s->cfg.vol_min_bars < 1) {
        s->cfg.vol_min_bars = 1;
    }
}

void tr_fxymae_eval(tr_fxymae_t *s, const tr_fxymae_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    bool is_new = !s->has_bar || in->bar_open != s->last_open;
    if (is_new) {
        snap_save(s);
        s->bar_reset = in->session_reset ? 1 : 0;
    } else {
        snap_load(s);
    }
    int show_h1 = s->h1;
    int show_h2 = s->h2;
    int show_h3 = s->h3;
    int show_h4 = s->h4;
    /* Plot22/23. 직전 봉 첫이탈. 세션 경계와 목표 미준비는 숨긴다. */
    int show_first = (in->session_reset || !in->targets_ready) ? 0 : s->first;
    int show_price = s->bar_reset ? 0 : s->pvc_price_on;
    int show_vol = s->bar_reset ? 0 : s->pvc_vol_on;
    int show_both = s->bar_reset ? 0 : s->pvc_both;
    double show_price_ratio = show_price ? s->pvc_price_ratio : 0.0;
    double show_vol_ratio = show_vol ? s->pvc_vol_ratio : 0.0;
    double mark_low = s->prev_low;
    double mark_high = s->prev_high;
    double slack = fmax(0.0, s->cfg.slack_ticks) * s->cfg.price_scale;
    double gap = fmax(0.0, s->cfg.mark_ticks) * s->cfg.price_scale;
    double vol = fmax(0.0, in->volume);

    if (in->session_reset || !in->targets_ready) {
        s->cnt = 0;
        s->prev_cnt = 0;
        s->cur_hi = 0;
        s->cur_lo = 0;
        s->prev_hi = 0;
        s->prev_lo = 0;
        s->prev_valid = 0;
        s->broke = 0;
        s->width = 0;
        s->two_max = in->session_reset ? 0 : s->s_two_max;
        s->vol_cur = 0;
        s->vol_prev = 0;
        s->vol_base = 0;
    } else {
        s->cnt = s->s_cnt;
        s->prev_cnt = s->s_prev_cnt;
        s->cur_hi = s->s_cur_hi;
        s->cur_lo = s->s_cur_lo;
        s->prev_hi = s->s_prev_hi;
        s->prev_lo = s->s_prev_lo;
        s->prev_valid = s->s_prev_valid;
        s->broke = s->s_broke;
        s->two_max = s->s_two_max;
        s->vol_cur = s->s_vol_cur;
        s->vol_prev = s->s_vol_prev;
        s->vol_base = s->s_vol_base;
    }

    double hi = fmax(in->t1, fmax(in->t2, in->t3));
    double lo = fmin(in->t1, fmin(in->t2, in->t3));
    double width = hi - lo;
    int pos = 0, first = 0;
    if (in->targets_ready) {
        if (in->session_reset || s->s_ready == 0) {
            s->cnt = 1;
            s->prev_cnt = 0;
            s->cur_hi = in->high;
            s->cur_lo = in->low;
            s->prev_hi = 0;
            s->prev_lo = 0;
            s->prev_valid = 0;
            s->broke = 0;
            s->vol_cur = vol;
            s->vol_prev = 0;
        } else if (width != s->s_width) {
            s->prev_cnt = s->s_cnt;
            s->prev_hi = s->s_cur_hi;
            s->prev_lo = s->s_cur_lo;
            s->prev_valid = 1;
            s->cnt = 1;
            s->cur_hi = in->high;
            s->cur_lo = in->low;
            s->broke = 0;
            s->vol_prev = s->s_vol_cur;
            s->vol_cur = vol;
        } else {
            s->cnt = s->s_cnt + 1;
            s->cur_hi = fmax(s->s_cur_hi, in->high);
            s->cur_lo = fmin(s->s_cur_lo, in->low);
            s->vol_cur = s->s_vol_cur + vol;
        }
        s->width = width;
        if (s->prev_valid) {
            if (in->close > s->prev_hi + slack) {
                pos = 1;
            } else if (in->close < s->prev_lo - slack) {
                pos = -1;
            }
            if (s->broke == 0 && pos != 0) {
                first = pos;
                s->broke = 1;
            }
        }
    }
    s->ready = in->targets_ready;

    double cur_range = 0, two_hi = 0, two_lo = 0, two_range = 0;
    if (in->targets_ready) {
        cur_range = s->cur_hi - s->cur_lo;
        two_hi = s->cur_hi;
        two_lo = s->cur_lo;
        if (s->prev_valid) {
            two_hi = fmax(s->cur_hi, s->prev_hi);
            two_lo = fmin(s->cur_lo, s->prev_lo);
        }
        two_range = two_hi - two_lo;
        s->two_max = fmax(s->two_max, two_range);
    }
    double price_ratio = (s->two_max > 0.0 && in->targets_ready) ? two_range / s->two_max * 100.0 : 0.0;
    double vol_cur_avg = s->cnt > 0 ? s->vol_cur / s->cnt : 0;
    double vol_prev_avg = s->prev_cnt > 0 ? s->vol_prev / s->prev_cnt : 0;
    int vol_ready = vol_cur_avg > 0 && vol_prev_avg > 0 && s->cnt >= s->cfg.vol_min_bars &&
                    s->prev_cnt >= s->cfg.vol_min_bars;
    double vol_drop = vol_ready ? vol_cur_avg / vol_prev_avg * 100.0 : 0;
    double vol_exit_base = s->vol_base;
    int pvc_price_on = 0;
    double pvc_price_ratio = 0.0;
    if (in->targets_ready && s->two_max > 0.0) {
        pvc_price_on = 1;
        pvc_price_ratio = two_range / s->two_max * 100.0;
    }
    int pvc_vol_on = 0;
    double pvc_vol_ratio = 0.0;
    if (in->targets_ready && vol_prev_avg > 0.0 && s->cnt >= s->cfg.vol_min_bars &&
        s->prev_cnt >= s->cfg.vol_min_bars) {
        pvc_vol_on = 1;
        pvc_vol_ratio = vol_cur_avg / vol_prev_avg * 100.0;
    }
    int pvc_both = (pvc_price_on && pvc_vol_on && pvc_price_ratio < s->cfg.price_limit &&
                    pvc_vol_ratio < s->cfg.vol_drop_limit)
                       ? 1
                       : 0;

    int data_ready = in->targets_ready && s->ready && in->reg_ready && in->mkt_ready && s->two_max > 0.0;
    /* 삼선 목표 준비는 targets_ready 와 같다 */
    int h1_cond = data_ready && in->union_w > 0 && price_ratio < s->cfg.price_limit &&
                  in->reg_ratio < s->cfg.reg_limit && in->mkt_ratio < s->cfg.mkt_limit;
    int h1_fire = h1_cond && (s->cond == 0 || in->session_reset) ? 1 : 0;

    if (in->session_reset || !data_ready) {
        s->h2_wait = 0;
        s->h2_elapsed = 0;
        s->h2_up = 0;
        s->h2_dn = 0;
    } else {
        s->h2_wait = s->s_h2_wait;
        s->h2_elapsed = s->s_h2_elapsed;
        s->h2_up = s->s_h2_up;
        s->h2_dn = s->s_h2_dn;
    }
    int h2 = 0;
    if (s->h2_wait) {
        s->h2_elapsed += 1;
        if (s->h2_elapsed > s->cfg.wait_bars) {
            s->h2_wait = 0;
        } else if (in->close > s->h2_up + slack) {
            h2 = 1;
            s->h2_wait = 0;
        } else if (in->close < s->h2_dn - slack) {
            h2 = -1;
            s->h2_wait = 0;
        }
    }
    if (h1_fire && s->h2_wait == 0) {
        s->h2_wait = 1;
        s->h2_elapsed = 0;
        s->h2_up = two_hi;
        s->h2_dn = two_lo;
        s->vol_base = (vol_cur_avg > 0 && s->cnt >= s->cfg.vol_min_bars) ? vol_cur_avg : 0;
    }
    int h3 = (h1_fire && vol_ready && vol_drop < s->cfg.vol_drop_limit) ? 1 : 0;
    int h4 = 0;
    if (h2 != 0 && vol_exit_base > 0.0) {
        double rise = vol / vol_exit_base * 100.0;
        if (rise > s->cfg.vol_rise_limit) {
            h4 = h2;
        }
    }
    s->h1 = h1_fire;
    s->first = first;
    s->cond = h1_cond ? 1 : 0;
    s->h2 = h2;
    s->h3 = h3;
    s->h4 = h4;
    s->pvc_price_on = pvc_price_on;
    s->pvc_vol_on = pvc_vol_on;
    s->pvc_both = pvc_both;
    s->pvc_price_ratio = pvc_price_ratio;
    s->pvc_vol_ratio = pvc_vol_ratio;
    s->prev_low = in->low;
    s->prev_high = in->high;

    memset(&s->out, 0, sizeof(s->out));
    s->out.ready = in->targets_ready;
    s->out.pos = pos;
    s->out.first_break = first;
    s->out.show_first = show_first;
    s->out.prev_valid = s->prev_valid;
    s->out.prev_hi = s->prev_hi;
    s->out.prev_lo = s->prev_lo;
    s->out.two_hi = two_hi;
    s->out.two_lo = two_lo;
    s->out.cnt = s->cnt;
    s->out.prev_cnt = s->prev_cnt;
    s->out.h1 = h1_fire;
    s->out.h2 = h2;
    s->out.h3 = h3;
    s->out.h4 = h4;
    s->out.show_h1 = show_h1;
    s->out.show_h2 = show_h2;
    s->out.show_h3 = show_h3;
    s->out.show_h4 = show_h4;
    if (!in->session_reset && show_h1) {
        s->out.mark_h1 = mark_low - gap;
    }
    if (!in->session_reset && show_h2 > 0) {
        s->out.mark_h2 = mark_low - gap * 2;
    }
    if (!in->session_reset && show_h2 < 0) {
        s->out.mark_h2 = mark_high + gap * 2;
    }
    if (!in->session_reset && show_h3) {
        s->out.mark_h3 = mark_low - gap;
    }
    if (!in->session_reset && show_h4 > 0) {
        s->out.mark_h4 = mark_low - gap * 2;
    }
    if (!in->session_reset && show_h4 < 0) {
        s->out.mark_h4 = mark_high + gap * 2;
    }
    s->out.show_price = show_price;
    s->out.show_vol = show_vol;
    s->out.show_both = show_both;
    s->out.show_price_ratio = show_price_ratio;
    s->out.show_vol_ratio = show_vol_ratio;
    s->last_open = in->bar_open;
    s->has_bar = true;
    (void)cur_range;
    (void)h1_fire;
}
