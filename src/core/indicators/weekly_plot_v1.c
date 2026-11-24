#include "core/indicators/weekly_plot_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/model/civil_time.h"

/* 명령 처리와 테스트가 한 스레드에서 순서대로 부른다. 페어 주문의 점수 상태와 분리한다. */
static tr_ksscore_t g_wplot_score;

static int32_t imax(int32_t a, int32_t b) {
    return a > b ? a : b;
}

static int64_t imax64(int64_t a, int64_t b) {
    return a > b ? a : b;
}

static double dmax(double a, double b) {
    return a > b ? a : b;
}

static double dmin(double a, double b) {
    return a < b ? a : b;
}

/* 예스랭귀지 DayOfWeek: 월 1 … 일 7. */
static int yl_dow(int64_t bdate) {
    int year = (int)(bdate / 10000);
    unsigned month = (unsigned)((bdate / 100) % 100);
    unsigned day = (unsigned)(bdate % 100);
    unsigned wd = tr_weekday_from_days(tr_days_from_civil(year, month, day));
    if (wd == 0) {
        return 7;
    }
    return (int)wd;
}

static int long_save_ok(const tr_wplot_config_t *cfg, const tr_wplot_bar_t *b) {
    double sum;
    double gap;
    double px;
    if (!(b->close > 0.0) || !(b->d3_close > 0.0)) {
        return 0;
    }
    sum = b->close + b->d3_close;
    gap = b->close - b->d3_close;
    if (gap < 0.0) {
        gap = -gap;
    }
    if (b->d2_day_index < 30 && gap > sum * 0.075) {
        return 0;
    }
    px = (b->close + b->d3_close) / 2.0;
    if (cfg->meet_mode == 1) {
        px = (dmax(b->low, b->d3_low) + dmin(b->high, b->d3_high)) / 2.0;
    }
    if (!(px >= 2.1) || !(px < 5.0)) {
        return 0;
    }
    if (b->d2_day_index <= 45 && !(px < 4.0) && !(gap < sum * 0.075)) {
        return 0;
    }
    if (b->d2_day_index < 90) {
        double rate;
        if (!b->d2_has_gap || !(b->d2_prev_close > 0.0) || !(b->d2_day_open > 0.0)) {
            return 0;
        }
        rate = b->d2_day_open - b->d2_prev_close;
        if (rate < 0.0) {
            rate = -rate;
        }
        rate = rate / b->d2_prev_close * 100.0;
        if (rate >= 1.0 && px < 2.3) {
            return 0;
        }
    }
    {
        double rate = 0.0;
        int have = 0;
        if (b->d2_has_gap && b->d2_prev_close > 0.0 && b->d2_day_open > 0.0) {
            rate = b->d2_day_open - b->d2_prev_close;
            if (rate < 0.0) {
                rate = -rate;
            }
            rate = rate / b->d2_prev_close * 100.0;
            have = 1;
        }
        if (have && rate >= 2.0 && px < 2.15) {
            return 0;
        }
        if (!(px < 4.2) && (!have || rate >= 2.0)) {
            return 0;
        }
    }
    if (px < 3.0) {
        if (!(gap < sum * 0.125)) {
            return 0;
        }
    } else if (!(gap < sum * 0.10)) {
        return 0;
    }
    return 1;
}

static int minute_of(int64_t hhmmss) {
    int64_t hour = hhmmss / 10000;
    int64_t minute = (hhmmss / 100) % 100;
    return (int)(hour * 60 + minute);
}

static int self_1m(const tr_wplot_bar_t *b) {
    return b->compress == 2 && b->interval == 1;
}

static int d3_1m(const tr_wplot_bar_t *b) {
    return b->d3_compress == 2 && b->d3_interval == 1;
}

static int d2_1m(const tr_wplot_bar_t *b) {
    return b->d2_compress == 2 && b->d2_interval == 1;
}

/* 프라이스링크 양매수 1481~1503. 오전 60 이하는 삼선 상한만, 60 초과는 가격비율 100 미만.
 * 오후는 오전 고가가 20을 넘은 적이 있으면 70 이하, 없으면 오후 상한 이하. */
static int long_three_ok(const tr_wplot_config_t *cfg, const tr_wplot_bar_t *b, int morn_high) {
    double applied;
    if (!(b->d2_date == b->sdate && d2_1m(b) && b->three_ready == 1 && b->three_peak > 0.0)) {
        return 0;
    }
    if (b->d2_day_index < imax(0, cfg->afternoon_switch)) {
        int under;
        applied = dmin(70.0, cfg->morning_three_max);
        under = b->three_ratio < applied;
        if (b->three_ratio <= 60.0) {
            return under;
        }
        return under && b->d2_time == b->stime && b->calc_ready == 1 && b->break_ready == 1 &&
               b->two_max > 0.0 && b->price_ratio < 100.0;
    }
    applied = morn_high ? 70.0 : dmin(50.0, cfg->afternoon_three_max);
    return b->three_ratio <= applied;
}

void tr_wplot_default_config(tr_wplot_config_t *cfg) {
    if (cfg == 0) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->meet_mode = 1;
    cfg->entry_start_bar = 15;
    cfg->afternoon_switch = 195;
    cfg->morning_three_max = 70.0;
    cfg->afternoon_three_max = 50.0;
    cfg->entry_start_time = 90000;
    cfg->flat_time = 151500;
}

int tr_wplot_init(tr_wplot_t *st, int side, const tr_wplot_config_t *cfg) {
    if (st == 0 || cfg == 0 || (side != TR_WPLOT_LONG && side != TR_WPLOT_SHORT)) {
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->side = side;
    st->cfg = *cfg;
    return 0;
}

int tr_wplot_eval(tr_wplot_t *st, const tr_wplot_bar_t *bar, tr_wplot_out_t *out) {
    const tr_wplot_config_t *cfg;
    double tol;
    int track = 0;
    int meet = 0;

    if (st == 0 || bar == 0 || out == 0) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    cfg = &st->cfg;
    if (!st->has_day || st->saved_bdate != bar->bdate) {
        st->stored = 0.0;
        st->self_meet = 0.0;
        st->opp_meet = 0.0;
        st->valid = 0;
        st->saved_bdate = bar->bdate;
        st->has_day = 1;
        st->open_target_min = -1;
        st->self_open = 0.0;
        st->opp_open = 0.0;
    }
    if (st->side == TR_WPLOT_LONG && st->high_date != bar->bdate) {
        st->high_date = bar->bdate;
        st->morn_hi_self = 0;
        st->morn_hi_opp = 0;
        st->morn_hi_any = 0;
        st->high_block = 0;
    }
    if (st->side == TR_WPLOT_LONG && bar->d2_date == bar->sdate &&
        bar->d2_day_index < imax(0, cfg->afternoon_switch)) {
        if (bar->high > 20.0) {
            st->morn_hi_self++;
            st->morn_hi_any = 1;
        }
        if (bar->d3_date == bar->sdate && bar->d3_high > 20.0) {
            st->morn_hi_opp++;
            st->morn_hi_any = 1;
        }
        if (st->morn_hi_self >= 3 || st->morn_hi_opp >= 3) {
            st->high_block = 1;
        }
    }
    tol = cfg->tolerance > 0.0 ? cfg->tolerance : 0.0;
    if (st->side == TR_WPLOT_LONG) {
        const int three_ok = long_three_ok(cfg, bar, st->morn_hi_any);
        const int release = bar->sq_release && bar->three_ready == 1 && bar->three_peak > 0.0 &&
                            bar->three_ratio <= 100.0;
        const int wide = bar->sq_ready && bar->sq_ratio >= 50.0;
        const int start = imax(30, imax(0, cfg->entry_start_bar));
        track = cfg->trade_off == 0 && self_1m(bar) && wide && (three_ok || release) &&
                bar->d2_date == bar->sdate && bar->d2_time == bar->stime &&
                bar->d2_day_index >= start && bar->stime < cfg->flat_time &&
                bar->next_sdate == bar->sdate && bar->next_stime < cfg->flat_time;
    } else {
        const int dow = yl_dow(bar->bdate);
        const int day_ok = (dow == 2 || dow == 3 || dow == 5) && d2_1m(bar);
        track = cfg->trade_off == 0 && self_1m(bar) && day_ok &&
                bar->stime >= imax64(INT64_C(90000), cfg->entry_start_time);
    }
    if (track && d3_1m(bar) && bar->d3_close > 0.0 && bar->d3_date == bar->sdate &&
        bar->d3_time == bar->stime) {
        if (cfg->meet_mode == 1) {
            meet = dmax(bar->low, bar->d3_low) <= dmin(bar->high, bar->d3_high) + tol;
        } else if (cfg->meet_mode == 2) {
            meet = bar->close - tol <= bar->d3_close && bar->close + tol >= bar->d3_close;
        }
    }
    if (st->side == TR_WPLOT_LONG) {
        double rate = 0.0;
        int have = bar->d2_has_gap && bar->d2_prev_close > 0.0 && bar->d2_day_open > 0.0;
        if (st->range_date != bar->sdate) {
            st->range_block = 0;
            st->range_date = bar->sdate;
        }
        if (have) {
            rate = bar->d2_day_open - bar->d2_prev_close;
            if (rate < 0.0) {
                rate = -rate;
            }
            rate = rate / bar->d2_prev_close * 100.0;
            if (rate >= 2.0 && bar->d2_range_valid && bar->d2_today_range > bar->d2_yday_range) {
                st->range_block = 1;
            }
        }
        if (!long_save_ok(cfg, bar) || st->high_block || st->range_block ||
            (have && rate >= 2.0 && !bar->d2_range_valid) ||
            (bar->d2_day_index < imax(0, cfg->afternoon_switch) && st->morn_hi_any)) {
            meet = 0;
        }
    }
    if (track && !st->valid && meet && bar->close > 0.0 &&
        (st->side != TR_WPLOT_LONG || (bar->close > 1.5 && bar->d3_close > 1.5))) {
        if (cfg->meet_mode == 1) {
            st->stored = (dmax(bar->low, bar->d3_low) + dmin(bar->high, bar->d3_high)) / 2.0;
        } else {
            st->stored = (bar->close + bar->d3_close) / 2.0;
        }
        st->self_meet = bar->close;
        st->opp_meet = bar->d3_close;
        st->valid = 1;
        st->open_target_min = minute_of(bar->stime) + 1;
        st->self_open = 0.0;
        st->opp_open = 0.0;
    }
    if (st->side == TR_WPLOT_LONG && st->valid && st->open_target_min >= 0) {
        if (!(st->self_open > 0.0) && minute_of(bar->stime) == st->open_target_min && bar->open > 0.0) {
            st->self_open = bar->open;
            st->open_date = bar->sdate;
            st->open_time = bar->stime;
        }
        if (st->self_open > 0.0 && !(st->opp_open > 0.0) && bar->d3_date == st->open_date &&
            bar->d3_time == st->open_time && bar->d3_open > 0.0) {
            st->opp_open = bar->d3_open;
        }
    }
    if (self_1m(bar) && st->valid) {
        out->link_on = 1;
        out->link = st->stored;
    }
    if (self_1m(bar) && d3_1m(bar) && st->valid && bar->close > 0.0 && bar->d3_close > 0.0 &&
        bar->d3_date == bar->sdate && bar->d3_time == bar->stime &&
        ((st->side == TR_WPLOT_LONG && st->self_open > 0.0 && st->opp_open > 0.0) ||
         (st->side != TR_WPLOT_LONG && st->self_meet > 0.0 && st->opp_meet > 0.0))) {
        out->ret_on = 1;
        if (st->side == TR_WPLOT_LONG) {
            out->ret = ((bar->close / st->self_open - 1.0) + (bar->d3_close / st->opp_open - 1.0)) * 50.0;
        } else {
            out->ret =
                ((1.0 - bar->close / st->self_meet) + (1.0 - bar->d3_close / st->opp_meet)) * 50.0;
        }
    }
    return 0;
}

static int64_t px_key(const tr_wpair_pxbar_t *b) {
    return b->ymd * INT64_C(1000000) + b->hhmmss;
}

static int find_stamp(const tr_wpair_pxbar_t *a, size_t n, size_t *cursor, int64_t key) {
    while (*cursor < n) {
        int64_t k = px_key(&a[*cursor]);
        if (k < key) {
            (*cursor)++;
            continue;
        }
        if (k == key) {
            return (int)(*cursor)++;
        }
        return -1;
    }
    return -1;
}

static void apply_score(tr_wplot_bar_t *b, const tr_ksscore_out_t *o) {
    b->calc_ready = o->calc_ready;
    b->three_ready = o->three_ready;
    b->three_peak = o->three_peak;
    b->three_ratio = o->three_ratio;
    b->break_ready = o->break_ready;
    b->two_max = o->two_max;
    b->price_ratio = o->price_ratio;
}

static int push_pt(tr_wplot_pt_t *dst, size_t cap, size_t *n, int64_t t, double v) {
    if (dst == 0 || n == 0 || *n >= cap) {
        return -1;
    }
    dst[*n].time_sec = t;
    dst[*n].value = v;
    (*n)++;
    return 0;
}

int tr_wplot_replay(tr_wplot_t *st, const tr_wpair_pxbar_t *self, size_t nself,
                    const tr_wpair_pxbar_t *opp, size_t nopp, const tr_wpair_pxbar_t *fut,
                    size_t nfut, const tr_ksscore_config_t *score_cfg, tr_wplot_pt_t *link,
                    size_t link_cap, size_t *nlink, tr_wplot_pt_t *ret, size_t ret_cap,
                    size_t *nret) {
    tr_wplot_config_t cfg;
    tr_ksscore_config_t fallback;
    const tr_ksscore_config_t *scfg;
    size_t fed = 0;
    size_t opp_at = 0;
    size_t fut_at = 0;
    int64_t gap_ymd = 0;
    double gap_open = 0.0;
    double gap_prev = 0.0;
    double gap_last = 0.0;
    int gap_has_prev = 0;
    int gap_has_today = 0;
    int long_side;
    tr_wsq_point_t *sq = 0;

    if (st == 0 || (nself > 0 && self == 0) || (nopp > 0 && opp == 0) || (nfut > 0 && fut == 0) ||
        nlink == 0 || nret == 0 || (link_cap > 0 && link == 0) || (ret_cap > 0 && ret == 0)) {
        return -1;
    }
    cfg = st->cfg;
    if (tr_wplot_init(st, st->side, &cfg) != 0) {
        return -1;
    }
    *nlink = 0;
    *nret = 0;
    long_side = st->side == TR_WPLOT_LONG;
    scfg = score_cfg;
    if (long_side) {
        if (scfg == 0) {
            tr_ksscore_default_config(&fallback, 0.05);
            scfg = &fallback;
        }
        if (!tr_ksscore_init(&g_wplot_score, scfg)) {
            return -1;
        }
    }
    if (nfut > 0) {
        sq = calloc(nfut, sizeof(*sq));
        if (sq == 0 || tr_wsq_eval(fut, nfut, 0, sq) != 0) {
            free(sq);
            return -1;
        }
    }

    for (size_t i = 0; i < nself; i++) {
        const tr_wpair_pxbar_t *s = &self[i];
        tr_wplot_bar_t bar;
        tr_wplot_out_t out;
        int oi;
        int fi;

        if (long_side) {
            while (fed < nfut && fut[fed].open_us <= s->open_us) {
                tr_ksscore_input_t in;
                if (fut[fed].ymd != gap_ymd) {
                    if (gap_has_today) {
                        gap_prev = gap_last;
                        gap_has_prev = 1;
                    }
                    gap_ymd = fut[fed].ymd;
                    gap_open = fut[fed].open;
                    gap_has_today = 1;
                }
                gap_last = fut[fed].close;
                memset(&in, 0, sizeof(in));
                in.bdate = fut[fed].ymd;
                in.day_index = fut[fed].day_index;
                in.current_bar = (int32_t)(fed + 1);
                in.bar_open = fut[fed].open_us;
                in.cur_time = fut[fed].hhmmss;
                in.high = fut[fed].high;
                in.low = fut[fed].low;
                in.close = fut[fed].close;
                in.volume = fut[fed].volume;
                tr_ksscore_eval(&g_wplot_score, &in);
                fed++;
            }
        }

        memset(&bar, 0, sizeof(bar));
        bar.bdate = s->ymd;
        bar.sdate = s->ymd;
        bar.stime = s->hhmmss;
        bar.next_sdate = i + 1 < nself ? self[i + 1].ymd : s->ymd;
        bar.next_stime = i + 1 < nself ? self[i + 1].hhmmss : s->hhmmss;
        bar.compress = 2;
        bar.interval = 1;
        bar.open = s->open;
        bar.high = s->high;
        bar.low = s->low;
        bar.close = s->close;

        oi = find_stamp(opp, nopp, &opp_at, px_key(s));
        if (oi >= 0) {
            const tr_wpair_pxbar_t *o = &opp[oi];
            bar.d3_date = o->ymd;
            bar.d3_time = o->hhmmss;
            bar.d3_compress = 2;
            bar.d3_interval = 1;
            bar.d3_open = o->open;
            bar.d3_high = o->high;
            bar.d3_low = o->low;
            bar.d3_close = o->close;
        }
        fi = find_stamp(fut, nfut, &fut_at, px_key(s));
        if (fi >= 0) {
            const tr_wpair_pxbar_t *f = &fut[fi];
            bar.d2_date = f->ymd;
            bar.d2_time = f->hhmmss;
            bar.d2_compress = 2;
            bar.d2_interval = 1;
            bar.d2_day_index = f->day_index;
            if (gap_has_prev && gap_has_today && f->ymd == gap_ymd) {
                bar.d2_has_gap = 1;
                bar.d2_prev_close = gap_prev;
                bar.d2_day_open = gap_open;
            }
            if (sq != 0) {
                bar.sq_ready = sq[fi].ready;
                bar.sq_ratio = sq[fi].ratio;
                bar.sq_release = sq[fi].release;
            }
            if (long_side && fed > 0 && px_key(&fut[fed - 1]) == px_key(s)) {
                apply_score(&bar, &g_wplot_score.out);
            }
        }
        if (tr_wplot_eval(st, &bar, &out) != 0) {
            free(sq);
            return -1;
        }
        if (out.link_on && push_pt(link, link_cap, nlink, s->open_us / INT64_C(1000000), out.link) != 0) {
            free(sq);
            return -1;
        }
        if (out.ret_on && push_pt(ret, ret_cap, nret, s->open_us / INT64_C(1000000), out.ret) != 0) {
            free(sq);
            return -1;
        }
    }
    free(sq);
    return 0;
}

static int same_val(double a, double b) {
    double d = a - b;
    double s = a < 0.0 ? -a : a;
    if (d < 0.0) {
        d = -d;
    }
    return d <= 1e-9 * (1.0 + s);
}

static int appendf(char *buf, size_t cap, int off, const char *fmt, ...) {
    va_list ap;
    int n;
    if (off < 0 || (size_t)off >= cap) {
        return -1;
    }
    va_start(ap, fmt);
    n = vsnprintf(buf + off, cap - (size_t)off, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)off + (size_t)n >= cap) {
        return -1;
    }
    return off + n;
}

static int emit_links(char *buf, size_t cap, int off, const tr_wplot_pt_t *pt, size_t begin, size_t n) {
    int any = 0;
    size_t i = begin;
    while (pt != 0 && i < n) {
        size_t j = i + 1;
        while (j < n && pt[j].time_sec == pt[j - 1].time_sec + 60 && same_val(pt[j].value, pt[i].value)) {
            j++;
        }
        off = appendf(buf, cap, off, "%s[%lld,%lld,%.8g]", any ? "," : "", (long long)pt[i].time_sec,
                      (long long)pt[j - 1].time_sec, pt[i].value);
        if (off < 0) {
            return -1;
        }
        any = 1;
        i = j;
    }
    return off;
}

static int emit_rets(char *buf, size_t cap, int off, const tr_wplot_pt_t *pt, size_t begin, size_t n) {
    int any = 0;
    size_t i = begin;
    while (pt != 0 && i < n) {
        size_t j = i + 1;
        while (j < n && pt[j].time_sec == pt[j - 1].time_sec + 60) {
            j++;
        }
        off = appendf(buf, cap, off, "%s[%lld", any ? "," : "", (long long)pt[i].time_sec);
        if (off < 0) {
            return -1;
        }
        for (size_t k = i; k < j; k++) {
            off = appendf(buf, cap, off, ",%.8g", pt[k].value);
            if (off < 0) {
                return -1;
            }
        }
        off = appendf(buf, cap, off, "]");
        if (off < 0) {
            return -1;
        }
        any = 1;
        i = j;
    }
    return off;
}

static int emit_all(char *buf, size_t cap, int64_t t0, int64_t t1, const tr_wplot_pt_t *ll, size_t bll,
                    size_t nll, const tr_wplot_pt_t *lr, size_t blr, size_t nlr, const tr_wplot_pt_t *sl,
                    size_t bsl, size_t nsl, const tr_wplot_pt_t *sr, size_t bsr, size_t nsr) {
    int off = appendf(buf, cap, 0, "{\"ll\":[");
    if (off < 0) {
        return -1;
    }
    off = emit_links(buf, cap, off, ll, bll, nll);
    if (off < 0) {
        return -1;
    }
    off = appendf(buf, cap, off, "],\"lr\":[");
    if (off < 0) {
        return -1;
    }
    off = emit_rets(buf, cap, off, lr, blr, nlr);
    if (off < 0) {
        return -1;
    }
    off = appendf(buf, cap, off, "],\"sl\":[");
    if (off < 0) {
        return -1;
    }
    off = emit_links(buf, cap, off, sl, bsl, nsl);
    if (off < 0) {
        return -1;
    }
    off = appendf(buf, cap, off, "],\"sr\":[");
    if (off < 0) {
        return -1;
    }
    off = emit_rets(buf, cap, off, sr, bsr, nsr);
    if (off < 0) {
        return -1;
    }
    return appendf(buf, cap, off, "],\"t0\":%lld,\"t1\":%lld}", (long long)t0, (long long)t1);
}

int tr_wplot_pack(char *buf, size_t cap, int64_t t0, int64_t t1, const tr_wplot_pt_t *ll, size_t nll,
                  const tr_wplot_pt_t *lr, size_t nlr, const tr_wplot_pt_t *sl, size_t nsl,
                  const tr_wplot_pt_t *sr, size_t nsr) {
    size_t bll = 0;
    size_t blr = 0;
    size_t bsl = 0;
    size_t bsr = 0;

    if (buf == 0 || cap == 0 || (nll > 0 && ll == 0) || (nlr > 0 && lr == 0) || (nsl > 0 && sl == 0) ||
        (nsr > 0 && sr == 0)) {
        return -1;
    }
    for (int attempt = 0; attempt < 64; attempt++) {
        size_t left[4];
        size_t best = 0;
        int which = -1;
        size_t drop;
        if (emit_all(buf, cap, t0, t1, ll, bll, nll, lr, blr, nlr, sl, bsl, nsl, sr, bsr, nsr) >= 0) {
            return 0;
        }
        left[0] = nll - bll;
        left[1] = nlr - blr;
        left[2] = nsl - bsl;
        left[3] = nsr - bsr;
        for (int k = 0; k < 4; k++) {
            if (left[k] > best) {
                best = left[k];
                which = k;
            }
        }
        if (which < 0) {
            return -1;
        }
        drop = best / 2;
        if (drop < 1) {
            drop = 1;
        }
        if (which == 0) {
            bll += drop;
        } else if (which == 1) {
            blr += drop;
        } else if (which == 2) {
            bsl += drop;
        } else {
            bsr += drop;
        }
    }
    return -1;
}
