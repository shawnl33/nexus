#include "core/indicators/fx_posmon.h"

#include "core/functions/fx_session_key_v1.h"

#include <string.h>

static double basis(const tr_fxpos_in_t *in) {
    double scale = in->price_scale > 0.0 ? in->price_scale : 1.0;
    double mult = in->tick_mult > 0.0 ? in->tick_mult : 1.0;
    return scale * mult;
}

static int iabs(int n) {
    return n < 0 ? -n : n;
}

static void reset_day(tr_fxpos_t *s) {
    s->closed_long = 0;
    s->closed_short = 0;
    s->wins = 0;
    s->losses = 0;
}

static int ring_index(const tr_fxpos_t *s, int ago) {
    int idx = s->hist_i - 1 - ago;
    while (idx < 0) {
        idx += 512;
    }
    return idx;
}

/* CountIF. less 가 1이면 < limit, 아니면 > limit. 이번 봉이 링의 맨 앞이다. */
static int count_open(const tr_fxpos_t *s, int n, int less, double limit) {
    int i;
    int c = 0;
    int lim = n < s->hist_n ? n : s->hist_n;
    if (n <= 0) {
        return 0;
    }
    for (i = 0; i < lim; i++) {
        double v = s->open_ring[ring_index(s, i)];
        if (less ? (v < limit) : (v > limit)) {
            c++;
        }
    }
    return c;
}

static int count_half_mfe(const tr_fxpos_t *s, int n) {
    int i;
    int c = 0;
    int lim = n < s->hist_n ? n : s->hist_n;
    if (n <= 0) {
        return 0;
    }
    for (i = 0; i < lim; i++) {
        int idx = ring_index(s, i);
        if (s->open_ring[idx] > s->mfe_ring[idx] * 0.5) {
            c++;
        }
    }
    return c;
}

static int count_rose(const tr_fxpos_t *s, int n) {
    int i;
    int c = 0;
    int lim = n < s->hist_n ? n : s->hist_n;
    if (n <= 0) {
        return 0;
    }
    for (i = 0; i < lim; i++) {
        if (s->rose_ring[ring_index(s, i)]) {
            c++;
        }
    }
    return c;
}

static void push_hist(tr_fxpos_t *s, double open_pts, double mfe_pts, int rose) {
    s->open_ring[s->hist_i] = open_pts;
    s->mfe_ring[s->hist_i] = mfe_pts;
    s->rose_ring[s->hist_i] = (unsigned char)(rose ? 1 : 0);
    s->hist_i++;
    if (s->hist_i >= 512) {
        s->hist_i = 0;
    }
    if (s->hist_n < 512) {
        s->hist_n++;
    }
}

static double keep_ratio(double ticks, int is_long) {
    if (ticks > 500) return 0.7;
    if (ticks > 400) return 0.65;
    if (ticks > 300) return 0.6;
    if (ticks > 200) return 0.55;
    if (ticks > 150) return 0.5;
    if (ticks > 100) return 0.4;
    return is_long ? 0.3 : 0.0;
}

static void clear_trade(tr_fxpos_t *s) {
    s->bars = 0;
    s->mfe_long = s->mae_long = s->open_long = 0;
    s->mfe_short = s->mae_short = s->open_short = 0;
    s->exit_pnl = 0;
    s->exit_age = 0;
    s->hi = s->lo = 0;
    s->hi_cnt = s->lo_cnt = 0;
    s->hist_i = 0;
    s->hist_n = 0;
}

void tr_fxpos_init(tr_fxpos_t *s) {
    if (s == 0) {
        return;
    }
    memset(s, 0, sizeof(*s));
}

void tr_fxpos_eval(tr_fxpos_t *s, const tr_fxpos_in_t *in, tr_fxpos_out_t *out) {
    int qty;
    int prev_qty;
    int prev_mkt;
    double prev_avg;
    double tick;
    double part_ticks;
    int hi_rose = 0;
    int prev_hi_cnt = 0;
    int lo_broke = 0;
    double cur_pnl = 0;
    if (out != 0) {
        memset(out, 0, sizeof(*out));
    }
    if (s == 0 || in == 0 || out == 0) {
        return;
    }
    tick = basis(in);
    part_ticks = in->partial_ticks < 0.0 ? 10.0 : in->partial_ticks;
    qty = iabs(in->contracts);
    if (in->v2) {
        int64_t key = tr_fx_session_key_v1(in->date, in->time);
        if (!s->has || key != s->session) {
            reset_day(s);
            s->session = key;
        }
        prev_mkt = s->held_mkt;
        prev_qty = s->held_qty;
        prev_avg = s->held_avg;
    } else {
        if (s->has && in->date != s->date) {
            reset_day(s);
        }
        prev_mkt = s->prev_mkt;
        prev_qty = s->prev_qty;
        prev_avg = s->prev_avg;
    }
    if (prev_mkt != 0 && in->market != prev_mkt) {
        double trade = 0;
        double avg = in->v2 ? s->held_avg : prev_avg;
        if (!in->v2 || avg > 0.0) {
            trade = s->exit_pnl + (in->close - avg) * (double)prev_mkt * (double)prev_qty;
        }
        if (prev_mkt > 0) {
            s->closed_long += trade;
        } else {
            s->closed_short += trade;
        }
        if (trade > 0.0) {
            s->wins += 1;
        } else {
            s->losses += 1;
        }
        clear_trade(s);
    }
    if (in->market != 0) {
        double avg = in->avg > 0.0 ? in->avg : (in->v2 ? s->held_avg : prev_avg);
        if (prev_mkt != in->market) {
            s->bars = 1;
            s->hi = in->high;
            s->lo = in->low;
            s->exit_pnl = 0;
            s->exit_age = 0;
            s->mfe_long = s->mae_long = s->open_long = 0;
            s->mfe_short = s->mae_short = s->open_short = 0;
        } else if (avg > 0.0) {
            s->bars += 1;
            prev_hi_cnt = s->hi_cnt;
            hi_rose = in->high > s->hi;
            lo_broke = in->low < s->lo;
            if (in->high > s->hi) {
                s->hi = in->high;
                s->hi_cnt = 0;
            } else {
                s->hi_cnt += 1;
            }
            if (in->low < s->lo) {
                s->lo = in->low;
                s->lo_cnt = 0;
            } else {
                s->lo_cnt += 1;
            }
            if (in->market > 0) {
                double mfe = in->high - avg;
                double mae = in->low - avg;
                if (s->bars == 1 || mfe > s->mfe_long) {
                    s->mfe_long = mfe;
                }
                if (s->bars == 1 || mae < s->mae_long) {
                    s->mae_long = mae;
                }
                s->open_long = in->close - avg;
            } else {
                double mfe = avg - in->low;
                double mae = avg - in->high;
                if (s->bars == 1 || mfe > s->mfe_short) {
                    s->mfe_short = mfe;
                }
                if (s->bars == 1 || mae < s->mae_short) {
                    s->mae_short = mae;
                }
                s->open_short = avg - in->close;
            }
            if (qty < prev_qty && prev_qty > 0) {
                double px = in->close;
                int closed = prev_qty - qty;
                if (part_ticks > 0.0) {
                    px = avg + (double)in->market * part_ticks * (in->price_scale > 0.0 ? in->price_scale : 1.0);
                }
                s->exit_pnl += (px - avg) * (double)in->market * (double)closed;
                s->exit_age = 1;
            }
            /* 원본은 1로 둔 같은 봉에서 한 번 더 올린다. */
            if (s->exit_age > 0) {
                s->exit_age += 1;
            }
        }
    }
    s->has = 1;
    s->date = in->date;
    s->prev_mkt = in->market;
    s->prev_qty = qty;
    s->prev_avg = in->avg;
    if (in->market != 0) {
        s->held_mkt = in->market;
        s->held_qty = qty;
        if (in->avg > 0.0) {
            s->held_avg = in->avg;
        }
    } else {
        s->held_mkt = 0;
        s->held_qty = 0;
        s->held_avg = 0;
    }
    out->side = in->market;
    out->open_ticks = (in->market > 0 ? s->open_long : s->open_short) / tick;
    out->mfe_ticks = (in->market > 0 ? s->mfe_long : s->mfe_short) / tick;
    out->mae_ticks = (in->market > 0 ? s->mae_long : s->mae_short) / tick;
    out->exit_ticks = s->exit_pnl / tick;
    out->exit_on = s->exit_age > 0;
    out->closed_long = s->closed_long / tick;
    out->closed_short = s->closed_short / tick;
    out->wins = s->wins;
    out->losses = s->losses;
    if (in->market > 0) {
        cur_pnl = s->open_long;
    } else if (in->market < 0) {
        cur_pnl = s->open_short;
    }
    if (in->market != 0 && s->bars > 0) {
        push_hist(s, out->open_ticks, in->market < 0 ? out->mfe_ticks : 0.0, hi_rose);
    }
    if (in->market != 0) {
        if (s->prev_pnl <= 0.0 && cur_pnl > 0.0) {
            int neg_life = count_open(s, s->bars, 1, 0.0);
            s->flips += 1;
            if (neg_life > s->bars * 0.7 && s->bars > 20) {
                s->danger += 1;
            }
        }
    } else {
        s->flips = 0;
        s->danger = 0;
    }
    out->flips = s->flips;
    out->danger = s->danger;
    s->prev_pnl = cur_pnl;
    out->mfe_rgb = 0xff0000u;
    out->open_rgb = 0x0000ffu;
    if (in->market > 0) {
        int neg6 = count_open(s, 6, 1, 0.0);
        int neg10 = count_open(s, 10, 1, 0.0);
        int neg3 = count_open(s, 3, 1, 0.0);
        int neg21 = count_open(s, 21, 1, 0.0);
        int pos_all = count_open(s, s->bars, 0, 0.0);
        int neg_all = count_open(s, s->bars, 1, 0.0);
        double pts = out->open_ticks;
        if (s->bars > 90 && pts > 100.0 && out->mfe_ticks > 200.0 && out->mfe_ticks < 300.0 &&
            pts < out->mfe_ticks * (keep_ratio(out->mfe_ticks, 1) + 0.2)) {
            out->mfe_rgb = 0x00ffffu;
        } else if (s->bars > 60 && out->mfe_ticks > 110.0) {
            out->mfe_rgb = 0x0000ffu;
        }
        out->mfe_w = s->hi_cnt > 10 ? 1 : 0;
        out->mae_rgb = out->mae_ticks < -21.0 ? 0x00ffffu : 0x0000ffu;
        out->mae_w = 1;
        if (neg6 > 5 && pts > -10.0 && pos_all < 1) {
            out->open_rgb = 0xffa500u;
        } else if (neg10 > 9 && pts > -50.0 && pos_all < 1 && lo_broke) {
            out->open_rgb = 0x00ffffu;
        } else if (neg3 > 2 && pts > -50.0 && pts < -30.0 && pos_all > 6 && lo_broke) {
            out->open_rgb = 0x00ffffu;
        } else if (neg21 > 20 && pts > -30.0 && pos_all < 1) {
            out->open_rgb = 0x00ffffu;
        } else if (pts > 20.0) {
            out->open_rgb = 0xff00ffu;
        } else if (pts > 15.0) {
            out->open_rgb = 0xbe0000u;
        } else if (pts > 10.0) {
            out->open_rgb = 0xff0000u;
        } else if (pts > 0.0) {
            out->open_rgb = 0xffc8c8u;
        } else if (pts > -10.0) {
            out->open_rgb = 0x82a5ffu;
        }
        if (pos_all > s->bars * 0.9 && s->bars > 30) {
            out->open_w = 6;
        } else if (neg_all > s->bars * 0.9 && s->bars > 30) {
            out->open_w = 5;
        } else if (neg_all > s->bars * 0.7 && s->bars > 20) {
            out->open_w = 2;
        } else {
            out->open_w = 1;
        }
        if (neg_all > s->bars * 0.8 && out->mae_ticks < -25.0) {
            out->keep_mfe = out->mfe_ticks * 0.7;
            out->keep_mfe_on = 1;
        }
        if (neg_all > s->bars * 0.3) {
            out->keep_mae = out->mae_ticks * 0.7;
            out->keep_mae_on = 1;
        }
    } else if (in->market < 0) {
        int half = count_half_mfe(s, s->lo_cnt);
        int rises = count_rose(s, s->exit_age);
        int neg_life = count_open(s, s->bars, 1, 0.0);
        int neg_exit = count_open(s, s->exit_age, 1, 0.0);
        int under10 = count_open(s, s->exit_age, 1, -10.0);
        int above3 = count_open(s, s->exit_age, 0, -3.0);
        double pts = out->open_ticks;
        if (s->bars > 60 && pts > 600.0 && out->mfe_ticks > 600.0 && s->lo_cnt > 25) {
            out->mfe_rgb = 0xffa500u;
        } else if (out->mfe_ticks > 300.0) {
            out->mfe_rgb = 0x00ffffu;
        }
        if (half > s->lo_cnt * 0.5 && s->lo_cnt > 240) {
            out->mfe_w = 2;
        } else if (s->bars < 180 && pts < 0.0 && prev_hi_cnt > 120 && hi_rose) {
            out->mfe_w = 5;
        } else if (s->lo_cnt > 10) {
            out->mfe_w = 1;
        }
        if (out->mfe_ticks > 10.0 && s->lo_cnt > s->hi_cnt + 5 && -out->mae_ticks > out->mfe_ticks * 1.5 &&
            rises > 2) {
            out->mae_rgb = 0xffa500u;
        } else if (out->mae_ticks < -8.0) {
            out->mae_rgb = 0x00ffffu;
        } else {
            out->mae_rgb = 0x0000ffu;
        }
        if (prev_hi_cnt > 240 && hi_rose) {
            out->mae_w = 5;
        } else if (s->hi_cnt > 10) {
            out->mae_w = 1;
        }
        if (under10 > 0 && above3 > 4 && pts < 0.0 && pts > -3.0) {
            out->open_rgb = 0x008000u;
        } else if (pts > 20.0) {
            out->open_rgb = 0xff00ffu;
        } else if (pts > 15.0) {
            out->open_rgb = 0xbe0000u;
        } else if (pts > 10.0) {
            out->open_rgb = 0xff0000u;
        } else if (pts > 0.0) {
            out->open_rgb = 0xffc8c8u;
        }
        if (neg_life > s->bars * 0.9 && s->bars > 30) {
            out->open_w = 6;
        } else if (neg_exit > s->exit_age * 0.9 && s->exit_age > 15) {
            out->open_w = 2;
        } else {
            out->open_w = 1;
        }
    }
    if (s->closed_long + s->closed_short != 0.0) {
        out->total_on = 1;
        out->total_ticks = (s->closed_long + s->closed_short) / tick;
    }
    out->long_rgb = out->closed_long > 300.0 ? 0xff0000u : 0xff7f00u;
}
