#include "core/signals/fx_wr_yl.h"

#include "core/backtest/yl_fill.h"
#include "core/functions/fx_session_key_v1.h"
#include "core/functions/fx_curve_v1.h"
#include "core/functions/fx_adx_v1.h"
#include "core/functions/fx_session_profile_v2.h"
#include "core/functions/fx_decision_v2.h"
#include "core/functions/fx_future_values_v1.h"
#include "core/functions/fx_flat_count_v2.h"
#include "core/functions/fx_wave_adj_v6.h"
#include "core/functions/fx_squeeze_v1.h"
#include "core/indicators/fx_entry_cand.h"
#include "core/model/civil_time.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int field_ok(int id) {
    return id >= 0 && id < YL_F_N;
}

double yl_lag(const tr_fxwr_t *s, int id, double k) {
    int i = (int)k;
    if (!field_ok(id) || i < 1 || i > YL_LAG) {
        return 0;
    }
    return s->lag[i - 1][id];
}

double yl_px(const tr_fxwr_t *s, char which, double k) {
    int i = (int)k;
    const double *h = s->ch;
    if (i < 1 || i > s->hist_n || i > 64) {
        return 0;
    }
    if (which == 'O') h = s->ph;
    else if (which == 'H') h = s->hh;
    else if (which == 'L') h = s->lh;
    else if (which == 'V') h = s->vh;
    return h[i - 1];
}

double yl_arr(const tr_fxwr_t *s, int id, int i) {
    if (id < 0 || id >= YL_A_N || i < 0 || i >= 391) {
        return 0;
    }
    return s->arr[id][i];
}

void yl_arr_set(tr_fxwr_t *s, int id, int i, double v) {
    if (id < 0 || id >= YL_A_N || i < 0 || i >= 391) {
        return;
    }
    s->arr[id][i] = v;
}

static const double *series_hist(const tr_fxwr_t *s, const char *which) {
    if (which && which[0] == 'V') return s->vh;
    if (which && which[0] == 'H') return s->hh;
    if (which && which[0] == 'L') return s->lh;
    if (which && which[0] == 'O') return s->ph;
    return s->ch;
}

static double window(const tr_fxwr_t *s, const char *which, double n, int mode) {
    int k, lim = (int)n;
    const double *h = series_hist(s, which);
    double acc = 0;
    int cnt = 0;
    if (lim < 1) return 0;
    if (lim > s->hist_n) lim = s->hist_n;
    if (lim > 64) lim = 64;
    for (k = 0; k < lim; k++) {
        if (mode == 0) acc += h[k];
        else if (cnt == 0 || (mode > 0 ? h[k] > acc : h[k] < acc)) acc = h[k];
        cnt++;
    }
    if (cnt == 0) return 0;
    return mode == 0 ? acc / (double)cnt : acc;
}

double yl_avg(const tr_fxwr_t *s, const char *which, double n) { return window(s, which, n, 0); }
double yl_hi(const tr_fxwr_t *s, const char *which, double n) { return window(s, which, n, 1); }
double yl_lo(const tr_fxwr_t *s, const char *which, double n) { return window(s, which, n, -1); }

double yl_rgb(double r, double g, double b) {
    return (double)(((int)r << 16) | ((int)g << 8) | (int)b);
}

double yl_dow(double yyyymmdd) {
    int d = (int)yyyymmdd;
    int month = (d / 100) % 100;
    int day = d % 100;
    return (double)tr_weekday_from_days(tr_days_from_civil(d / 10000, (unsigned)month, (unsigned)day));
}

double yl_max(double a, double b) { return a > b ? a : b; }
double yl_min(double a, double b) { return a < b ? a : b; }

void yl_order(tr_fxwr_t *s, int action, int price_type, double price, int qty, const char *name) {
    tr_fxwr_ord_t *o;
    if (s->ord_n >= 24 || name == 0) return;
    o = &s->ords[s->ord_n++];
    memset(o, 0, sizeof(*o));
    o->action = action;
    o->price_type = price_type;
    o->price = price;
    o->qty = qty;
    snprintf(o->name, sizeof(o->name), "%s", name);
}

double yl_WSF_FXSessionKeyV1(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
    double k = (double)tr_fx_session_key_v1((int64_t)b->date, (int64_t)b->time);
    s->cur[YL_F_FX세션키] = k;
    return k;
}

typedef struct {
    tr_fxc_t curve;
    tr_fxadx_t adx;
    tr_fxprof_t prof;
    tr_fxsq_t squeeze;
    tr_fxec_t entry;
    tr_fxfv_t fv;
    tr_fxflat_out_t flat_prev;
    tr_fxadj_t adj;
    double prev_flat;
    double prev_close;
    int ready;
} tr_fxwr_eng_t;

static tr_fxwr_eng_t *eng_of(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
    tr_fxwr_eng_t *e;
    tr_fxfv_config_t fc;
    if (s->eng != 0) {
        return (tr_fxwr_eng_t *)s->eng;
    }
    e = (tr_fxwr_eng_t *)calloc(1, sizeof(*e));
    if (e == 0) {
        return 0;
    }
    tr_fxc_init(&e->curve);
    tr_fxadx_init(&e->adx, 14);
    tr_fxprof_init(&e->prof);
    tr_fxsq_init(&e->squeeze);
    tr_fxec_init(&e->entry, b->price_scale > 0 ? b->price_scale : 0.25);
    tr_fxadj_init(&e->adj);
    memset(&fc, 0, sizeof(fc));
    fc.predict_bars[0] = 5;
    fc.predict_bars[1] = 10;
    fc.predict_bars[2] = 15;
    fc.predict_bars[3] = 30;
    fc.predict_bars[4] = 60;
    fc.predict_ticks = 10;
    fc.min_r2 = 0.4;
    fc.persist_bars = 5;
    fc.market_period = 20;
    fc.min_hold_bars = 3;
    fc.price_scale = b->price_scale > 0 ? b->price_scale : 0.25;
    tr_fxfv_init(&e->fv, &fc);
    e->ready = 1;
    s->eng = e;
    return e;
}

static int reset_of(const tr_fxwr_t *s) {
    return (int)s->cur[YL_F_FX초기화];
}

double yl_WSF_FXCurveV1(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
    tr_fxwr_eng_t *e = eng_of(s, b);
    tr_fxc_input_t in;
    if (e == 0) return 0;
    memset(&in, 0, sizeof(in));
    in.session_reset = reset_of(s);
    in.session_bars = (int)s->cur[YL_F_FX세션봉수];
    in.high = b->high;
    in.low = b->low;
    in.ticks = 10;
    in.is_new_bar = b->is_new_bar;
    tr_fxc_eval(&e->curve, &in);
    s->cur[YL_F_곡선예측방향] = e->curve.direction;
    return 1;
}

double yl_WSF_FXSessionProfileV1_CO(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
    return yl_WSF_FXSessionProfileV2_CO(s, b);
}

double yl_WSF_FXADXV1(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
    tr_fxwr_eng_t *e = eng_of(s, b);
    tr_fxadx_input_t in;
    if (e == 0) return 0;
    memset(&in, 0, sizeof(in));
    in.session_reset = reset_of(s);
    in.high = b->high;
    in.low = b->low;
    in.close = b->close;
    in.bar_open = b->bar_open;
    tr_fxadx_eval(&e->adx, &in);
    (void)e;
    return 1;
}

double yl_WSF_FXDecisionV2_CO(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
    tr_fxwr_eng_t *e = eng_of(s, b);
    tr_fxdec_input_t in;
    tr_fxdec_out_t out;
    int dir;
    if (e == 0) return 0;
    memset(&in, 0, sizeof(in));
    dir = e->curve.direction > 0 ? 1 : (e->curve.direction < 0 ? -1 : 0);
    if (reset_of(s)) dir = 0;
    in.future_dir = dir;
    in.state5 = e->prof.out.state5;
    in.profile_valid = e->prof.out.valid;
    in.center = e->prof.out.center;
    in.close = b->close;
    in.mode = 1;
    in.adx = e->adx.adx;
    in.plus_di = e->adx.plus_di;
    in.minus_di = e->adx.minus_di;
    in.adx_valid = e->adx.valid;
    in.adx_trend = 20;
    in.adx_strong = 30;
    out = tr_fxdec_eval(&in);
    s->cur[YL_F_통합상태] = out.unified;
    s->cur[YL_F_DI방향] = out.di;
    s->cur[YL_F_매물대5상태] = e->prof.out.state5;
    return 1;
}

double yl_WSF_FXFutureValuesV3_CO(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
    tr_fxwr_eng_t *e = eng_of(s, b);
    tr_fxfv_input_t in;
    const tr_fxfv_output_t *o;
    if (e == 0) return 0;
    memset(&in, 0, sizeof(in));
    in.date = b->date;
    in.time = b->time;
    in.bar_open = b->bar_open;
    in.high = b->high;
    in.low = b->low;
    in.close = b->close;
    in.volume = b->volume;
    tr_fxfv_eval(&e->fv, &in);
    o = &e->fv.out;
    s->cur[YL_F_곡선회귀선_평탄] = o->reg_flat;
    s->cur[YL_F_지속저장목표1] = o->persist_target[0];
    s->cur[YL_F_지속저장목표2] = o->persist_target[1];
    s->cur[YL_F_지속저장목표3] = o->persist_target[2];
    s->cur[YL_F_지속저장목표4] = o->persist_target[3];
    s->cur[YL_F_지속저장목표5] = o->persist_target[4];
    s->cur[YL_F_핵심지난상승최고가] = o->lup_high;
    s->cur[YL_F_핵심지난상승최저가] = o->lup_low;
    s->cur[YL_F_핵심지난상승_382] = o->lup_382;
    s->cur[YL_F_핵심지난상승_500] = o->lup_500;
    s->cur[YL_F_핵심지난상승_618] = o->lup_618;
    s->cur[YL_F_핵심지난하락최고가] = o->ldn_high;
    s->cur[YL_F_핵심지난하락최저가] = o->ldn_low;
    s->cur[YL_F_핵심지난하락_382] = o->ldn_382;
    s->cur[YL_F_핵심지난하락_500] = o->ldn_500;
    s->cur[YL_F_핵심지난하락_618] = o->ldn_618;
    return o->reg_flat != 0 || o->lup_high > 0;
}

double yl_WSF_FXFutureValuesV1(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
    return yl_WSF_FXFutureValuesV3_CO(s, b);
}

double yl_WSF_FXEntryCandV16_CO(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
    tr_fxwr_eng_t *e = eng_of(s, b);
    tr_fxec_input_t in;
    if (e == 0) return 0;
    memset(&in, 0, sizeof(in));
    in.date = b->date;
    in.time = b->time;
    in.bar_open = b->bar_open;
    in.is_new_bar = b->is_new_bar;
    in.high = b->high;
    in.low = b->low;
    in.close = b->close;
    in.volume = b->volume;
    in.fv = &e->fv.out;
    tr_fxec_eval(&e->entry, &in);
    s->cur[YL_F_후보방향] = e->entry.out.dir;
    s->cur[YL_F_후보등급] = e->entry.out.grade;
    return e->entry.out.dir != 0;
}

double yl_WSF_FXFlatCountV2_CO(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
    tr_fxwr_eng_t *e = eng_of(s, b);
    tr_fxflat_out_t out;
    double flat;
    if (e == 0) return 0;
    flat = s->cur[YL_F_곡선회귀선_평탄];
    memset(&out, 0, sizeof(out));
    tr_fxflat_step(&e->flat_prev, reset_of(s), flat, e->prev_flat, b->high, b->low,
                   b->close, e->prev_close, b->price_scale > 0 ? b->price_scale : 0.25, 3, &out);
    e->flat_prev = out;
    e->prev_flat = flat;
    e->prev_close = b->close;
    s->cur[YL_F_선행위치Cnt] = out.pos;
    s->cur[YL_F_선행구간진폭] = out.range_ticks;
    s->cur[YL_F_선행기울기Cnt] = out.slope;
    s->cur[YL_F_선행직전상승기울기] = out.prev_up_slope;
    s->cur[YL_F_선행직전하락기울기] = out.prev_dn_slope;
    s->cur[YL_F_선행유효] = out.valid;
    return out.valid;
}

double yl_WSF_FXWaveAdjV6_CO(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
    tr_fxwr_eng_t *e = eng_of(s, b);
    tr_fxadj_in_t in;
    tr_fxadj_out_t out;
    if (e == 0) return 0;
    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.flat = s->cur[YL_F_곡선회귀선_평탄];
    in.flat_prev = e->prev_flat;
    in.high = b->high;
    in.low = b->low;
    in.close = b->close;
    in.close_prev = e->prev_close;
    in.price_scale = b->price_scale > 0 ? b->price_scale : 0.25;
    in.up_hi = s->cur[YL_F_핵심지난상승최고가];
    in.up_lo = s->cur[YL_F_핵심지난상승최저가];
    in.up_382 = s->cur[YL_F_핵심지난상승_382];
    in.dn_hi = s->cur[YL_F_핵심지난하락최고가];
    in.dn_lo = s->cur[YL_F_핵심지난하락최저가];
    in.dn_618 = s->cur[YL_F_핵심지난하락_618];
    in.session_reset = reset_of(s);
    tr_fxadj_eval(&e->adj, &in, &out);
    s->cur[YL_F_조정추세방향] = out.trend;
    s->cur[YL_F_조정중] = out.in_adj;
    s->cur[YL_F_시간조정비] = out.time_ratio;
    s->cur[YL_F_가격조정비] = out.price_ratio;
    s->cur[YL_F_조정신호방향] = out.sig_dir;
    s->cur[YL_F_조정신호종류] = out.sig_kind;
    s->cur[YL_F_조정신호등급] = out.sig_grade;
    return out.valid;
}

double yl_WSF_FXSqueezeV1_CO(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
    tr_fxwr_eng_t *e = eng_of(s, b);
    tr_fxsq_input_t in;
    if (e == 0) return 0;
    memset(&in, 0, sizeof(in));
    in.session_reset = reset_of(s);
    in.is_new_bar = b->is_new_bar;
    in.bar_open = b->bar_open;
    in.high = b->high;
    in.low = b->low;
    in.close = b->close;
    in.volume = b->volume;
    in.price_scale = b->price_scale > 0 ? b->price_scale : 0.25;
    in.value_mult = 1;
    in.min_bars = 3;
    in.width_bars = 20;
    in.ratio_bars = 20;
    in.narrow_pct = 70;
    tr_fxsq_eval(&e->squeeze, &in);
    return e->squeeze.out.release;
}

double yl_WSF_FXSessionProfileV2_CO(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
    tr_fxwr_eng_t *e = eng_of(s, b);
    tr_fxprof_input_t in;
    if (e == 0) return 0;
    memset(&in, 0, sizeof(in));
    in.session_reset = reset_of(s);
    in.bar_open = b->bar_open;
    in.high = b->high;
    in.low = b->low;
    in.close = b->close;
    in.volume = b->volume;
    in.price_scale = b->price_scale > 0 ? b->price_scale : 0.25;
    in.value_mult = 1;
    in.min_bars = 3;
    tr_fxprof_eval(&e->prof, &in);
    s->cur[YL_F_매물대5상태] = e->prof.out.state5;
    return e->prof.out.valid;
}

double yl_WSF_FXSyntheticLinesV1(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
    (void)s;
    (void)b;
    return 0;
}

#include "core/signals/fx_wr_init.inc"

static void shift_lag(tr_fxwr_t *s) {
    memmove(&s->lag[1], &s->lag[0], (YL_LAG - 1) * sizeof(s->lag[0]));
    memcpy(s->lag[0], s->cur, sizeof(s->cur));
}

static void push_px(tr_fxwr_t *s) {
    memmove(&s->ph[1], &s->ph[0], 63 * sizeof(double));
    memmove(&s->hh[1], &s->hh[0], 63 * sizeof(double));
    memmove(&s->lh[1], &s->lh[0], 63 * sizeof(double));
    memmove(&s->ch[1], &s->ch[0], 63 * sizeof(double));
    memmove(&s->vh[1], &s->vh[0], 63 * sizeof(double));
    s->ph[0] = s->last_o;
    s->hh[0] = s->last_h;
    s->lh[0] = s->last_l;
    s->ch[0] = s->last_c;
    s->vh[0] = s->last_v;
    if (s->hist_n < 64) s->hist_n++;
}

void tr_fxwr_on_bar(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
    if (!s->inited) {
        tr_fxwr_load_defaults(s);
        s->inited = 1;
    }
    if (b->is_new_bar || s->bar_n == 0) {
        if (s->bar_n > 0) {
            shift_lag(s);
            push_px(s);
        }
        memcpy(s->open_cur, s->cur, sizeof(s->cur));
        s->bar_n++;
    } else {
        memcpy(s->cur, s->open_cur, sizeof(s->cur));
    }
    s->ord_n = 0;
    s->cur[YL_F_sDate] = (double)b->date;
    s->cur[YL_F_MarketPosition] = (double)b->market;
    tr_fxwr_yl_body(s, b);
    s->last_o = b->open;
    s->last_h = b->high;
    s->last_l = b->low;
    s->last_c = b->close;
    s->last_v = b->volume;
}
