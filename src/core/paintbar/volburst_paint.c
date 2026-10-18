#include "core/paintbar/volburst_paint.h"

#include <string.h>

void tr_vb_default_cfg(tr_vb_cfg_t *cfg) {
    if (cfg == 0) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->rv_len = 20;
    cfg->atr_len = 10;
    cfg->long_len = 390;
    cfg->range_len = 30;
    cfg->strong_rv = 2.5;
    cfg->strong_vol = 1.2;
    cfg->mid_vol = 1.3;
    cfg->mid_range = 8.0;
    cfg->mid_rv = 1.5;
    cfg->cooldown = 30;
    cfg->sig_start = 90000;
    cfg->sig_end = 150000;
    cfg->entry_bars = 3;
    cfg->hold_bars = 30;
    cfg->show_hold = 1;
    cfg->aft_on = 1;
    cfg->aft_start = 123000;
    cfg->aft_len = 120;
    cfg->aft_rv = 2.0;
}

bool tr_vb_init(tr_vb_t *s, const tr_vb_cfg_t *cfg) {
    int atr_keep;
    if (s == 0 || cfg == 0) {
        return false;
    }
    if (cfg->rv_len < 1 || cfg->atr_len < 1 || cfg->long_len < 1 || cfg->range_len < 1 ||
        cfg->aft_len < 1) {
        return false;
    }
    if (cfg->rv_len > TR_VB_VOL_CAP || cfg->range_len > TR_VB_RNG_CAP || cfg->long_len > TR_VB_ATR_CAP ||
        cfg->aft_len > TR_VB_ATR_CAP) {
        return false;
    }
    atr_keep = cfg->long_len > cfg->aft_len ? cfg->long_len : cfg->aft_len;
    if (atr_keep > TR_VB_ATR_CAP) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->cfg = *cfg;
    s->cur.last_sig = -9999;
    s->open.last_sig = -9999;
    return true;
}

static void push_front(double *hist, int *n, int limit, double v) {
    int keep = *n;
    if (limit < 1) {
        return;
    }
    if (keep > limit - 1) {
        keep = limit - 1;
    }
    if (keep > 0) {
        memmove(hist + 1, hist, (size_t)keep * sizeof(double));
    }
    hist[0] = v;
    *n = keep + 1;
}

static double ma_of(const double *hist, int n, int len) {
    double sum = 0.0;
    int i;
    if (len <= 0) {
        return 0.0;
    }
    for (i = 0; i < len; i++) {
        if (i < n) {
            sum += hist[i];
        }
    }
    return sum / (double)len;
}

static double extreme(const double *hist, int n, int len, int want_hi) {
    int i;
    int m;
    double x;
    if (n <= 0) {
        return 0.0;
    }
    m = len < n ? len : n;
    x = hist[0];
    for (i = 1; i < m; i++) {
        if (want_hi ? hist[i] > x : hist[i] < x) {
            x = hist[i];
        }
    }
    return x;
}

uint32_t tr_vb_on_bar(tr_vb_t *s, const tr_vb_bar_t *bar) {
    tr_vb_commit_t b;
    const tr_vb_cfg_t *c;
    double tr;
    double atr;
    double atr_long;
    double atr_aft;
    double base;
    double srv;
    double volreg;
    double rel;
    double rv;
    double rng;
    int bar_cnt;
    int is_aft;
    int in_time;
    int level;
    int since;
    int atr_keep;
    uint32_t rgb = 0;
    if (s == 0 || bar == 0) {
        return 0;
    }
    c = &s->cfg;
    if (bar->is_new_bar || !s->has_open) {
        s->open = s->cur;
        s->has_open = 1;
    } else {
        s->cur = s->open;
    }
    b = s->cur;
    bar_cnt = b.bar_cnt + 1;
    if (!b.has_prev) {
        tr = bar->high - bar->low;
    } else {
        double hi = bar->high > b.prev_c ? bar->high : b.prev_c;
        double lo = bar->low < b.prev_c ? bar->low : b.prev_c;
        tr = hi - lo;
    }
    atr = bar_cnt == 1 ? tr : b.atr + (tr - b.atr) / (double)c->atr_len;
    atr_keep = c->long_len > c->aft_len ? c->long_len : c->aft_len;
    push_front(b.atr_hist, &b.atr_n, atr_keep, atr);
    push_front(b.vol_hist, &b.vol_n, c->rv_len, bar->volume);
    {
        int keep = b.rng_n;
        if (keep > c->range_len - 1) {
            keep = c->range_len - 1;
        }
        if (keep > 0) {
            memmove(b.hi_hist + 1, b.hi_hist, (size_t)keep * sizeof(double));
            memmove(b.lo_hist + 1, b.lo_hist, (size_t)keep * sizeof(double));
        }
        b.hi_hist[0] = bar->high;
        b.lo_hist[0] = bar->low;
        b.rng_n = keep + 1;
    }

    atr_long = ma_of(b.atr_hist, b.atr_n, c->long_len);
    atr_aft = ma_of(b.atr_hist, b.atr_n, c->aft_len);
    is_aft = c->aft_on == 1 && bar->time >= c->aft_start && bar->time < c->sig_end;
    base = is_aft ? atr_aft : atr_long;
    srv = is_aft ? c->aft_rv : c->strong_rv;
    volreg = base > 0.0 ? atr / base : 0.0;
    rv = ma_of(b.vol_hist, b.vol_n, c->rv_len);
    rel = b.rel_v;
    if (bar_cnt > 1 && b.rv_avg > 0.0) {
        rel = bar->volume / b.rv_avg;
    }
    rng = 0.0;
    if (base > 0.0) {
        rng = (extreme(b.hi_hist, b.rng_n, c->range_len, 1) - extreme(b.lo_hist, b.rng_n, c->range_len, 0)) /
              base;
    }
    if (c->sig_start < c->sig_end) {
        in_time = bar->time >= c->sig_start && bar->time < c->sig_end;
    } else {
        in_time = bar->time >= c->sig_start || bar->time < c->sig_end;
    }
    level = 0;
    if (bar_cnt > c->long_len && in_time && bar_cnt - b.last_sig >= c->cooldown) {
        if (rel >= srv && volreg >= c->strong_vol) {
            level = 2;
        } else if (volreg >= c->mid_vol && rng >= c->mid_range && rel >= c->mid_rv) {
            level = 1;
        }
        if (level > 0) {
            b.last_sig = bar_cnt;
            b.last_lvl = level;
        }
    }
    since = bar_cnt - b.last_sig;
    if (since == 0) {
        rgb = b.last_lvl == 2 ? TR_VB_STRONG : TR_VB_MID;
    } else if (since >= 1 && since <= c->entry_bars) {
        rgb = b.last_lvl == 2 ? TR_VB_STRONG_ZONE : TR_VB_MID_ZONE;
    } else if (c->show_hold == 1 && since > c->entry_bars && since <= c->hold_bars) {
        rgb = TR_VB_HOLD;
    }
    b.bar_cnt = bar_cnt;
    b.atr = atr;
    b.prev_c = bar->close;
    b.has_prev = 1;
    b.rv_avg = rv;
    b.rel_v = rel;
    s->cur = b;
    return rgb;
}
