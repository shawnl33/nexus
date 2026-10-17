#include "core/signals/volburst_strangle.h"

#include <string.h>

void tr_vbstr_default_cfg(tr_vbstr_cfg_t *cfg) {
    if (cfg == 0) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    tr_vb_default_cfg(&cfg->vol);
    cfg->diff_pct = 10.0;
    cfg->min_price = 0.1;
    cfg->take_pct = 15.0;
    cfg->stop_pct = 10.0;
    cfg->use_vol_exit = 1;
    cfg->reentry = 0;
    cfg->use_time_exit = 1;
    cfg->exit_time = 153000;
}

bool tr_vbstr_init(tr_vbstr_t *st, const tr_vbstr_cfg_t *cfg) {
    if (st == 0 || cfg == 0) {
        return false;
    }
    memset(st, 0, sizeof(*st));
    st->cfg = *cfg;
    if (!tr_vb_init(&st->vol, &cfg->vol)) {
        return false;
    }
    return true;
}

bool tr_vbstr_on_bar(tr_vbstr_t *st, const tr_vbstr_bar_t *bar, tr_vbstr_out_t *out) {
    tr_vb_bar_t vb;
    tr_vbstr_book_t b;
    const tr_vbstr_cfg_t *c;
    int prev_zone;
    int since;
    int in_zone;
    int zone_end;
    int price_ok;
    double avg;
    double diff;
    double tot;
    const char *order;
    if (st == 0 || bar == 0) {
        return false;
    }
    c = &st->cfg;
    if (bar->is_new_bar || !st->has_open) {
        if (st->cur.pending == 1) {
            st->cur.pos = 1;
        } else if (st->cur.pending == -1) {
            st->cur.pos = 0;
        }
        st->cur.pending = 0;
        st->open = st->cur;
        st->has_open = 1;
    } else {
        st->cur = st->open;
    }
    b = st->cur;
    prev_zone = st->open.in_zone;
    memset(&vb, 0, sizeof(vb));
    vb.time = bar->time;
    vb.high = bar->high;
    vb.low = bar->low;
    vb.close = bar->close;
    vb.volume = bar->volume;
    vb.is_new_bar = bar->is_new_bar;
    (void)tr_vb_on_bar(&st->vol, &vb);
    since = st->vol.cur.bar_cnt - st->vol.cur.last_sig;
    in_zone = st->vol.cur.last_sig > -9999 && since >= 0 && since <= c->vol.hold_bars;
    zone_end = st->vol.cur.bar_cnt > 1 && prev_zone == 1 && in_zone == 0;
    if (in_zone == 1 && (st->vol.cur.bar_cnt == 1 || prev_zone == 0)) {
        b.traded = 0;
    }
    avg = 0.0;
    diff = 999.0;
    price_ok = 0;
    if (bar->has_call && bar->has_put) {
        avg = (bar->call_c + bar->put_c) / 2.0;
        if (avg > 0.0) {
            double gap = bar->call_c - bar->put_c;
            if (gap < 0.0) {
                gap = -gap;
            }
            diff = gap / avg * 100.0;
        }
        if (bar->call_c >= c->min_price && bar->put_c >= c->min_price && diff <= c->diff_pct) {
            price_ok = 1;
        }
    }
    order = 0;
    tot = 0.0;
    if (b.pos == 0 && in_zone == 1 && price_ok == 1 && (b.traded == 0 || c->reentry == 1) &&
        (c->use_time_exit == 0 || bar->time < c->exit_time)) {
        order = "양매수진입";
        b.call_in = bar->call_c;
        b.put_in = bar->put_c;
        b.traded = 1;
        b.pending = 1;
    } else if (b.pos == 1 && b.call_in > 0.0 && b.put_in > 0.0 && bar->has_call && bar->has_put) {
        tot = ((bar->call_c + bar->put_c) / (b.call_in + b.put_in) - 1.0) * 100.0;
        if (tot >= c->take_pct) {
            order = "익절";
        } else if (tot <= -c->stop_pct) {
            order = "손절";
        } else if (c->use_vol_exit == 1 && zone_end == 1) {
            order = "변동성소멸청산";
        } else if (c->use_time_exit == 1 && c->vol.sig_start < c->vol.sig_end && bar->time >= c->exit_time) {
            order = "시간종료";
        }
        if (order != 0) {
            b.pending = -1;
        }
    }
    b.in_zone = in_zone;
    st->cur = b;
    if (out != 0) {
        out->pos = b.pos;
        out->in_zone = in_zone;
        out->zone_end = zone_end;
        out->level = 0;
        if (since == 0 && st->vol.cur.last_sig == st->vol.cur.bar_cnt) {
            out->level = st->vol.cur.last_lvl;
        }
        out->order = order;
        out->tot_ret = tot;
    }
    return true;
}

static void push_event(tr_vbstr_event_t *ev, size_t cap, int *n, int64_t sec, int kind, const char *name) {
    tr_vbstr_event_t item;
    if (cap == 0 || ev == 0) {
        *n += 1;
        return;
    }
    item.time_sec = sec;
    item.kind = kind;
    item.name = name;
    if ((size_t)*n < cap) {
        ev[*n] = item;
    } else {
        memmove(ev, ev + 1, (cap - 1) * sizeof(*ev));
        ev[cap - 1] = item;
    }
    *n += 1;
}

int tr_vbstr_replay(tr_vbstr_t *st, const tr_vbstr_px_t *fut, size_t nfut, const tr_vbstr_px_t *call,
                    size_t ncall, const tr_vbstr_px_t *put, size_t nput, tr_vbstr_event_t *ev, size_t cap) {
    tr_vbstr_cfg_t cfg;
    size_t i;
    size_t ic = 0;
    size_t ip = 0;
    int n = 0;
    if (st == 0 || (nfut > 0 && fut == 0)) {
        return -1;
    }
    cfg = st->cfg;
    if (!tr_vbstr_init(st, &cfg)) {
        return -1;
    }
    for (i = 0; i < nfut; i++) {
        tr_vbstr_bar_t bar;
        tr_vbstr_out_t out;
        memset(&bar, 0, sizeof(bar));
        bar.time = fut[i].hhmmss;
        bar.open = fut[i].open;
        bar.high = fut[i].high;
        bar.low = fut[i].low;
        bar.close = fut[i].close;
        bar.volume = fut[i].volume;
        bar.is_new_bar = 1;
        while (ic < ncall && call[ic].open_us < fut[i].open_us) {
            ic++;
        }
        while (ip < nput && put[ip].open_us < fut[i].open_us) {
            ip++;
        }
        if (ic < ncall && call[ic].open_us == fut[i].open_us) {
            bar.has_call = 1;
            bar.call_c = call[ic].close;
        }
        if (ip < nput && put[ip].open_us == fut[i].open_us) {
            bar.has_put = 1;
            bar.put_c = put[ip].close;
        }
        if (!tr_vbstr_on_bar(st, &bar, &out)) {
            return -1;
        }
        if (out.order != 0) {
            int kind = strcmp(out.order, "양매수진입") == 0 ? TR_VBSTR_ENTRY : TR_VBSTR_EXIT;
            push_event(ev, cap, &n, fut[i].open_us / 1000000LL, kind, out.order);
        }
    }
    return n;
}
