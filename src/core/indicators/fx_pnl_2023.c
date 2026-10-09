#include "core/indicators/fx_pnl_2023.h"

#include <string.h>

/* 2023_우드스탁_N선물_수익관리.txt 18–301. 주석 블록은 제외. */

#define RGB_(r, g, b) ((uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))
#define YL_RED RGB_(255, 0, 0)
#define YL_BLUE RGB_(0, 0, 255)
#define YL_CYAN RGB_(0, 255, 255)
#define YL_MAGENTA RGB_(255, 0, 255)
#define YL_ORANGE RGB_(255, 127, 0)
#define YL_GREEN RGB_(0, 128, 0)

static double dmax(double a, double b) {
    return a > b ? a : b;
}

static double dmin(double a, double b) {
    return a < b ? a : b;
}

static const tr_fxpnl_hist_t *hist_ago(const tr_fxpnl_t *s, int ago) {
    int idx;
    if (ago < 1 || ago > s->hist_n) {
        return 0;
    }
    idx = s->hist_i - ago;
    if (idx < 0) {
        idx += TR_FXPNL_HIST;
    }
    return &s->hist[idx % TR_FXPNL_HIST];
}

/* CountIF(pnl_pts ? limit, n). less=1 이면 <, 아니면 >. 현재 봉 포함. */
static int count_pnl(const tr_fxpnl_t *s, int n, double cur, int less, double limit) {
    int c = 0;
    int i, lim;
    if (n <= 0) {
        return 0;
    }
    if (less ? (cur < limit) : (cur > limit)) {
        c++;
    }
    lim = n - 1;
    if (lim > s->hist_n) {
        lim = s->hist_n;
    }
    for (i = 1; i <= lim; i++) {
        const tr_fxpnl_hist_t *h = hist_ago(s, i);
        if (h == 0) {
            break;
        }
        if (less ? (h->pnl_pts < limit) : (h->pnl_pts > limit)) {
            c++;
        }
    }
    return c;
}

/* CountIF(진입손익/ps > 그 봉의 진입수익_매도/ps*0.5, n). */
static int count_half_mfe(const tr_fxpnl_t *s, int n, double cur_pnl, double cur_mfe) {
    int c = 0;
    int i, lim;
    if (n <= 0) {
        return 0;
    }
    if (cur_pnl > cur_mfe * 0.5) {
        c++;
    }
    lim = n - 1;
    if (lim > s->hist_n) {
        lim = s->hist_n;
    }
    for (i = 1; i <= lim; i++) {
        const tr_fxpnl_hist_t *h = hist_ago(s, i);
        if (h == 0) {
            break;
        }
        if (h->pnl_pts > h->short_mfe_pts * 0.5) {
            c++;
        }
    }
    return c;
}

static int count_hi_rose(const tr_fxpnl_t *s, int n, int cur_rose) {
    int c = 0;
    int i, lim;
    if (n <= 0) {
        return 0;
    }
    if (cur_rose) {
        c++;
    }
    lim = n - 1;
    if (lim > s->hist_n) {
        lim = s->hist_n;
    }
    for (i = 1; i <= lim; i++) {
        const tr_fxpnl_hist_t *h = hist_ago(s, i);
        if (h == 0) {
            break;
        }
        if (h->hi_rose) {
            c++;
        }
    }
    return c;
}

static int count_flag(const tr_fxpnl_t *s, int n, int cur, int which) {
    int c = 0;
    int i, lim;
    if (n <= 0) {
        return 0;
    }
    if (cur) {
        c++;
    }
    lim = n - 1;
    if (lim > s->hist_n) {
        lim = s->hist_n;
    }
    for (i = 1; i <= lim; i++) {
        const tr_fxpnl_hist_t *h = hist_ago(s, i);
        if (h == 0) {
            break;
        }
        if (which == 0 ? h->went_flat : h->drop60) {
            c++;
        }
    }
    return c;
}

static double long_keep(double pts) {
    if (pts > 500) return 0.7;
    if (pts > 400) return 0.65;
    if (pts > 300) return 0.6;
    if (pts > 200) return 0.55;
    if (pts > 150) return 0.5;
    if (pts > 100) return 0.4;
    return 0.3;
}

static double short_keep(double pts) {
    if (pts > 500) return 0.7;
    if (pts > 400) return 0.65;
    if (pts > 300) return 0.6;
    if (pts > 200) return 0.55;
    if (pts > 150) return 0.5;
    if (pts > 100) return 0.4;
    return 0.0;
}

static uint32_t long_open_rgb(double pts, int neg6, int neg10, int neg3, int neg21, int pos_all, int lo_broke) {
    if (neg6 > 5 && pts > -10.0 && pos_all < 1) {
        return YL_ORANGE;
    }
    if (neg10 > 9 && pts > -50.0 && pos_all < 1 && lo_broke) {
        return YL_CYAN;
    }
    if (neg3 > 2 && pts > -50.0 && pts < -30.0 && pos_all > 6 && lo_broke) {
        return YL_CYAN;
    }
    if (neg21 > 20 && pts > -30.0 && pos_all < 1) {
        return YL_CYAN;
    }
    if (pts > 20.0) return YL_MAGENTA;
    if (pts > 15.0) return RGB_(190, 0, 0);
    if (pts > 10.0) return RGB_(255, 0, 0);
    if (pts > 0.0) return RGB_(255, 200, 200);
    if (pts > -10.0) return RGB_(130, 165, 255);
    return YL_BLUE;
}

static int long_open_w(int pos_n, int neg_n, int bars) {
    if (pos_n > bars * 0.9 && bars > 30) return 6;
    if (neg_n > bars * 0.9 && bars > 30) return 5;
    if (neg_n > bars * 0.7 && bars > 20) return 2;
    return 1;
}

void tr_fxpnl_init(tr_fxpnl_t *s) {
    if (s != 0) {
        memset(s, 0, sizeof(*s));
    }
}

void tr_fxpnl_eval(tr_fxpnl_t *s, const tr_fxpnl_in_t *in, tr_fxpnl_out_t *out) {
    tr_fxpnl_bar_t prev, cur;
    double ps, pts, mae_pts, short_mfe_pts, long_mfe_pts;
    int day_index, lo_broke, hi_rose;
    const double qty_full = in->qty_full > 0.0 ? in->qty_full : 2.0;
    const double qty_part = in->qty_part > 0.0 ? in->qty_part : 1.0;
    if (s == 0 || in == 0 || out == 0) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (in->new_bar && s->has_cur) {
        s->hist[s->hist_i % TR_FXPNL_HIST] = s->cur_hist;
        s->hist_i++;
        if (s->hist_n < TR_FXPNL_HIST) {
            s->hist_n++;
        }
        s->closed = s->cur;
        s->has_closed = 1;
    }
    if (s->has_closed) {
        prev = s->closed;
    } else {
        memset(&prev, 0, sizeof(prev));
    }
    cur = prev;
    ps = in->price_scale > 0.0 ? in->price_scale : 1.0;
    day_index = (s->has_closed && prev.date == in->date) ? prev.day_index + 1 : 0;
    cur.date = in->date;
    cur.day_index = day_index;

    /* bdate != Bdate[1] */
    if (!s->has_closed || prev.date != in->date) {
        cur.closed_long = 0;
        cur.closed_short = 0;
        cur.entries_long = 0;
        cur.entries_short = 0;
        cur.wins = 0;
        cur.losses = 0;
        cur.flat_age = 0;
    }

    if (in->market != 0) {
        if (prev.market != in->market) {
            cur.bars = 1;
            cur.pnl = 0;
            cur.hi = in->high;
            cur.lo = in->low;
            cur.lo_cnt = 0;
            cur.hi_cnt = 0;
            cur.mfe_long = cur.mae_long = cur.open_long = 0;
            cur.mfe_short = cur.mae_short = cur.open_short = 0;
            cur.exit_n = 0;
            cur.exit_pnl = 0;
            cur.exit_age = 0;
            cur.flat_age = 0;
            /* 원본은 I_AvgEntryPrice. 시스템 체결가가 오면 그 값, 없으면 이 봉 종가. */
            cur.entry_px = in->avg_entry > 0.0 ? in->avg_entry : in->close;
            if (in->market > 0) {
                cur.entries_long += 1;
            } else {
                cur.entries_short += 1;
            }
        } else {
            cur.bars = prev.bars + 1;
            if (in->avg_entry > 0.0) {
                cur.entry_px = in->avg_entry;
            }
            cur.pnl = in->market > 0 ? in->close - cur.entry_px : cur.entry_px - in->close;
            cur.hi = dmax(in->high, prev.hi);
            cur.lo = dmin(in->low, prev.lo);
            cur.lo_cnt = (cur.lo != prev.lo) ? 0 : prev.lo_cnt + 1;
            cur.hi_cnt = (cur.hi != prev.hi) ? 0 : prev.hi_cnt + 1;
            if (in->market > 0) {
                cur.mfe_long = dmax(in->high - cur.entry_px, prev.mfe_long);
                cur.mae_long = dmin(in->low - cur.entry_px, prev.mae_long);
                cur.open_long = in->close - cur.entry_px;
            }
            if (in->market < 0) {
                cur.mfe_short = dmax(cur.entry_px - in->low, prev.mfe_short);
                cur.mae_short = dmin(cur.entry_px - in->high, prev.mae_short);
                cur.open_short = cur.entry_px - in->close;
            }
            if (in->contracts != prev.contracts) {
                cur.exit_n = prev.exit_n + 1;
                cur.exit_pnl = prev.exit_pnl + cur.pnl;
                cur.exit_age = 1;
            }
            if (cur.exit_age > 0) {
                cur.exit_age += 1;
            }
        }
    }

    if (in->market == 0 && prev.market != 0) {
        double booked = prev.exit_pnl +
                        (prev.market > 0 ? prev.open_long : prev.open_short) *
                            (prev.exit_n == 0 ? qty_full : qty_part);
        cur.bars = 0;
        cur.pnl = 0;
        cur.hi = 0;
        cur.lo = 0;
        cur.hi_cnt = 0;
        cur.lo_cnt = 0;
        cur.mfe_long = cur.mae_long = cur.open_long = 0;
        cur.mfe_short = cur.mae_short = cur.open_short = 0;
        /* DayIndex==0(날짜의 첫 봉)이면 청산손익을 더하지 않는다. */
        if (prev.market > 0 && day_index > 0) {
            cur.closed_long += booked;
        }
        if (prev.market < 0 && day_index > 0) {
            cur.closed_short += booked;
        }
        if (cur.closed_long + cur.closed_short > 0.0) {
            cur.wins = cur.wins + 1;
        } else {
            cur.losses = cur.losses + 1;
        }
        cur.exit_n = 0;
        cur.exit_pnl = 0;
        cur.exit_age = 0;
        cur.flat_age = 1;
    }
    if (cur.flat_age > 0) {
        cur.flat_age += 1;
    }
    cur.market = in->market;
    cur.contracts = in->contracts;

    pts = cur.pnl / ps;
    long_mfe_pts = cur.mfe_long / ps;
    short_mfe_pts = cur.mfe_short / ps;
    mae_pts = in->market > 0 ? cur.mae_long / ps : cur.mae_short / ps;
    lo_broke = cur.lo < prev.lo;
    hi_rose = cur.hi > prev.hi;

    if (in->market != 0) {
        if (prev.pnl <= 0.0 && cur.pnl > 0.0) {
            int neg_life = count_pnl(s, cur.bars, pts, 1, 0.0);
            cur.flips = prev.flips + 1;
            if (neg_life > cur.bars * 0.7 && cur.bars > 20) {
                cur.danger = prev.danger + 1;
            }
        }
    } else {
        cur.flips = 0;
        cur.danger = 0;
    }

    out->side = in->market;
    out->bars = cur.bars;
    out->entries = cur.entries_long + cur.entries_short;
    out->wins = cur.wins;
    out->closed_pts = (cur.closed_long + cur.closed_short) / ps;
    if (cur.closed_long != 0.0) {
        out->closed_long_pts = cur.closed_long / ps;
        out->closed_long_on = 1;
        out->closed_long_rgb = out->closed_long_pts > 300.0 ? YL_RED : YL_ORANGE;
    }
    if (cur.closed_short != 0.0) {
        out->closed_short_pts = cur.closed_short / ps;
        out->closed_short_on = 1;
    }
    if (cur.exit_n > 0) {
        out->exit_pts = cur.exit_pnl / ps;
        out->exit_on = 1;
    }
    out->flips = cur.flips;
    out->danger = cur.danger;

    if (in->market > 0) {
        double kr = long_keep(long_mfe_pts);
        int pos_all = count_pnl(s, cur.bars, pts, 0, 0.0);
        int neg_all = count_pnl(s, cur.bars, pts, 1, 0.0);
        int neg6 = count_pnl(s, 6, pts, 1, 0.0);
        int neg10 = count_pnl(s, 10, pts, 1, 0.0);
        int neg3 = count_pnl(s, 3, pts, 1, 0.0);
        int neg21 = count_pnl(s, 21, pts, 1, 0.0);
        out->open_pts = pts;
        out->mfe_pts = long_mfe_pts;
        out->mae_pts = mae_pts;
        out->open_rgb = long_open_rgb(pts, neg6, neg10, neg3, neg21, pos_all, lo_broke);
        out->open_w = long_open_w(pos_all, neg_all, cur.bars);
        out->open_on = out->open_w > 0;
        out->mfe_rgb = YL_RED;
        if (cur.bars > 90 && pts > 100.0 && long_mfe_pts > 200.0 && long_mfe_pts < 300.0 &&
            pts < long_mfe_pts * (kr + 0.2)) {
            out->mfe_rgb = YL_CYAN;
        } else if (cur.bars > 60 && long_mfe_pts > 110.0) {
            out->mfe_rgb = YL_BLUE;
        }
        out->mfe_w = cur.hi_cnt > 10 ? 1 : 0;
        out->mfe_on = out->mfe_w > 0;
        out->mae_rgb = mae_pts < -21.0 ? YL_CYAN : YL_BLUE;
        out->mae_w = 1;
        out->mae_on = 1;
        if (neg_all > cur.bars * 0.8 && mae_pts < -25.0) {
            out->p4 = long_mfe_pts * 0.7;
            out->p4_on = 1;
        }
        if (neg_all > cur.bars * 0.3) {
            out->p5 = mae_pts * 0.7;
            out->p5_on = 1;
        }
        if (long_mfe_pts > 100.0) {
            out->keep_pts = long_mfe_pts * kr;
            out->keep_on = 1;
            out->keep_rgb = YL_RED;
        }
    } else if (in->market < 0) {
        double kr = short_keep(short_mfe_pts);
        int neg_life = count_pnl(s, cur.bars, pts, 1, 0.0);
        int neg_exit = count_pnl(s, cur.exit_age, pts, 1, 0.0);
        int half = count_half_mfe(s, cur.lo_cnt, pts, short_mfe_pts);
        int rises = count_hi_rose(s, cur.exit_age, hi_rose);
        int under10 = count_pnl(s, cur.exit_age, pts, 1, -10.0);
        int above3 = count_pnl(s, cur.exit_age, pts, 0, -3.0);
        out->open_pts = pts;
        out->mfe_pts = short_mfe_pts;
        out->mae_pts = mae_pts;
        out->mfe_rgb = YL_RED;
        if (cur.bars > 60 && pts > 600.0 && short_mfe_pts > 600.0 && cur.lo_cnt > 25) {
            out->mfe_rgb = YL_ORANGE;
        } else if (short_mfe_pts > 300.0) {
            out->mfe_rgb = YL_CYAN;
        }
        if (half > cur.lo_cnt * 0.5 && cur.lo_cnt > 240) {
            out->mfe_w = 2;
        } else if (cur.bars < 180 && pts < 0.0 && prev.hi_cnt > 120 && hi_rose) {
            out->mfe_w = 5;
        } else if (cur.lo_cnt > 10) {
            out->mfe_w = 1;
        }
        out->mfe_on = out->mfe_w > 0;
        if (short_mfe_pts > 10.0 && cur.lo_cnt > cur.hi_cnt + 5 && -mae_pts > short_mfe_pts * 1.5 && rises > 2) {
            out->mae_rgb = YL_ORANGE;
        } else if (mae_pts < -8.0) {
            out->mae_rgb = YL_CYAN;
        } else {
            out->mae_rgb = YL_BLUE;
        }
        if (prev.hi_cnt > 240 && hi_rose) {
            out->mae_w = 5;
        } else if (cur.hi_cnt > 10) {
            out->mae_w = 1;
        }
        out->mae_on = out->mae_w > 0;
        if (under10 > 0 && above3 > 4 && pts < 0.0 && pts > -3.0) {
            out->open_rgb = YL_GREEN;
        } else if (pts > 20.0) {
            out->open_rgb = YL_MAGENTA;
        } else if (pts > 15.0) {
            out->open_rgb = RGB_(190, 0, 0);
        } else if (pts > 10.0) {
            out->open_rgb = YL_RED;
        } else if (pts > 0.0) {
            out->open_rgb = RGB_(255, 200, 200);
        } else {
            out->open_rgb = YL_BLUE;
        }
        if (neg_life > cur.bars * 0.9 && cur.bars > 30) {
            out->open_w = 6;
        } else if (neg_exit > cur.exit_age * 0.9 && cur.exit_age > 15) {
            out->open_w = 2;
        } else {
            out->open_w = 1;
        }
        out->open_on = out->open_w > 0;
        if (short_mfe_pts > 100.0) {
            out->keep_pts = short_mfe_pts * kr;
            out->keep_on = 1;
            out->keep_rgb = YL_RED;
        }
        if (short_mfe_pts > 200.0) {
            out->keep2_pts = short_mfe_pts * 0.7;
            out->keep2_on = 1;
        }
    }

    {
        double exit_pts = cur.exit_pnl / ps;
        if (exit_pts < -5.0 && short_mfe_pts > -exit_pts * 1.5 && pts < -exit_pts * 1.5) {
            out->p30 = pts;
            out->p30_on = 1;
        }
    }
    {
        double closed_pts = cur.closed_long / ps;
        double prev_closed_pts = prev.closed_long / ps;
        int drop = (closed_pts - prev_closed_pts) < -60.0;
        int flats = count_flag(s, day_index, prev.market != 0 && in->market == 0, 0);
        int drops = count_flag(s, 30, drop, 1);
        if (drops > 0) {
            out->p26_on = 1;
        }
        if (flats > 3) {
            out->p27_on = 1;
        }
        s->cur_hist.drop60 = (unsigned char)(drop ? 1 : 0);
        s->cur_hist.went_flat = (unsigned char)(prev.market != 0 && in->market == 0 ? 1 : 0);
    }
    s->cur_hist.pnl_pts = pts;
    s->cur_hist.short_mfe_pts = short_mfe_pts;
    s->cur_hist.hi_rose = (unsigned char)(hi_rose ? 1 : 0);
    s->cur = cur;
    s->has_cur = 1;
}
