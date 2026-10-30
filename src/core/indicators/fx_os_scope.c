#include "core/indicators/fx_os_scope.h"

#include "core/paintbar/fx_judge_paint95.h"
#include "core/signals/fx_wr_yl.h"
#include "core/backtest/yl_fill.h"
#include "core/functions/fx_decision_v2.h"
#include "core/functions/fx_flat_count_v2.h"
#include "core/functions/fx_session_key_v1.h"
#include "core/functions/fx_wave_adj_v6.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RGB_(r, g, b) ((uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))

void tr_fxsig_cfg_default(tr_fxsig_cfg_t *cfg) {
    if (cfg == 0) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->trade_start = 210000;
    cfg->trade_end = 20000;
    cfg->rth_start = 223000;
    cfg->adj_min = 23.6;
    cfg->adj_unified = 1;
    cfg->buy_382 = 1;
    cfg->state_bars = 60;
    cfg->state_pct = 50;
    cfg->state_pink = 1;
    cfg->sell_u100 = 1;
    cfg->sell_low = 38.2;
    cfg->lead_u100 = 1;
    cfg->slope_min = 2;
    cfg->slope_limit = 30;
    cfg->grade_limit = 2;
    cfg->adj_once = 1;
    cfg->strong_px = 38.2;
    cfg->strong_time = 23.6;
    cfg->mid_px = 61.8;
    cfg->start_b_only = 1;
    cfg->struct_boost = 1;
    cfg->struct_px = 78.6;
    cfg->signal_limit = 120;
    cfg->flip_opp_pct = 61.8;
    cfg->min_leg = 3;
    cfg->min_trend = 15;
    cfg->min_opp = 3;
    cfg->confirm_back = 2;
    cfg->predict_bars[0] = 5;
    cfg->predict_bars[1] = 10;
    cfg->predict_bars[2] = 15;
    cfg->predict_bars[3] = 30;
    cfg->predict_bars[4] = 60;
    cfg->min_r2 = 0.4;
    cfg->persist_bars = 5;
    cfg->market_period = 20;
    cfg->min_hold_bars = 3;
    cfg->swing_link = 0;
    cfg->momentum_ignore_ticks = 1;
    cfg->predict_ticks = 10;
    cfg->confirm_closed = 0;
    cfg->relax = 1;
    cfg->brk_check = 1;
    cfg->brk_once = 1;
    cfg->order_qty = 1;
}

static int sig_fv_open(tr_fxos_t *s) {
    tr_fxfv_config_t fc;
    int i;
    int period;
    if (s == 0 || s->price_scale <= 0.0) {
        return 0;
    }
    memset(&fc, 0, sizeof(fc));
    fc.predict_ticks = s->cfg.predict_ticks > 0 ? s->cfg.predict_ticks : 10;
    for (i = 0; i < 5; i++) {
        fc.predict_bars[i] = s->cfg.predict_bars[i] > 0 ? s->cfg.predict_bars[i] : 0;
    }
    if (fc.predict_bars[0] <= 0) {
        fc.predict_bars[0] = 5;
        fc.predict_bars[1] = 10;
        fc.predict_bars[2] = 15;
        fc.predict_bars[3] = 30;
        fc.predict_bars[4] = 60;
    }
    fc.min_r2 = s->cfg.min_r2 > 0.0 ? s->cfg.min_r2 : 0.4;
    fc.persist_bars = s->cfg.persist_bars > 0 ? s->cfg.persist_bars : 5;
    period = s->cfg.market_period > 0 ? s->cfg.market_period : 20;
    if (period > 100) {
        period = 100;
    }
    fc.market_period = period;
    fc.min_hold_bars = s->cfg.min_hold_bars > 0 ? s->cfg.min_hold_bars : 3;
    fc.price_scale = s->price_scale;
    fc.swing_link = s->cfg.swing_link;
    fc.momentum_ignore_ticks = s->cfg.momentum_ignore_ticks;
    return tr_fxfv_init(&s->sig_fv, &fc) ? 1 : 0;
}

void tr_fxos_set_cfg(tr_fxos_t *s, const tr_fxsig_cfg_t *cfg) {
    if (s == 0 || cfg == 0) {
        return;
    }
    s->cfg = *cfg;
    if (s->cfg.state_bars < 0) {
        s->cfg.state_bars = 0;
    }
    if (s->cfg.state_bars > 60) {
        s->cfg.state_bars = 60;
    }
    if (s->cfg.slope_min < 0) {
        s->cfg.slope_min = 0;
    }
    sig_fv_open(s);
}

bool tr_fxos_init(tr_fxos_t *s, double price_scale) {
    if (s == 0 || price_scale <= 0.0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->price_scale = price_scale;
    tr_fxsig_cfg_default(&s->cfg);
    tr_fxos_filter_cfg_default(&s->filt);
    if (!sig_fv_open(s)) {
        return false;
    }
    if (!tr_fxc_init(&s->curve)) {
        return false;
    }
    if (!tr_fxadx_init(&s->adx, 14)) {
        return false;
    }
    tr_fxprof_init(&s->session);
    tr_fxsq_init(&s->squeeze);
    if (!tr_fxec_init(&s->entry, price_scale)) {
        return false;
    }
    return true;
}

bool tr_fxos_relink(tr_fxos_t *s) {
    if (s == 0) {
        return false;
    }
    return tr_fxc_relink(&s->curve) && tr_fxec_relink(&s->entry);
}

static uint32_t judge_rgb(int state) {
    if (state == 100) {
        return RGB_(255, 0, 0);
    }
    if (state == 50) {
        return RGB_(255, 140, 140);
    }
    if (state == -100) {
        return RGB_(0, 0, 255);
    }
    if (state == -50) {
        return RGB_(140, 165, 255);
    }
    return RGB_(108, 108, 108);
}

static int stage_of(int hold) {
    if (hold >= 60) {
        return 3;
    }
    if (hold >= 30) {
        return 2;
    }
    if (hold >= 15) {
        return 1;
    }
    return 0;
}

static uint32_t hold_rgb(int pos, int stage) {
    if (pos > 0) {
        if (stage == 3) return RGB_(200, 0, 0);
        if (stage == 2) return RGB_(240, 100, 100);
        if (stage == 1) return RGB_(255, 175, 175);
        return RGB_(255, 220, 220);
    }
    if (pos < 0) {
        if (stage == 3) return RGB_(0, 40, 200);
        if (stage == 2) return RGB_(100, 130, 240);
        if (stage == 1) return RGB_(175, 195, 255);
        return RGB_(220, 228, 255);
    }
    if (stage == 3) return RGB_(90, 90, 90);
    if (stage == 2) return RGB_(140, 140, 140);
    if (stage == 1) return RGB_(185, 185, 185);
    return RGB_(220, 220, 220);
}

static uint32_t entry_rgb(int dir, int grade) {
    if (dir > 0) {
        if (grade == 8) return RGB_(255, 60, 140);
        if (grade == 7) return RGB_(255, 120, 0);
        if (grade == 6) return RGB_(230, 70, 0);
        if (grade == 5) return RGB_(170, 0, 0);
        if (grade == 4) return RGB_(200, 0, 200);
        if (grade == 3) return RGB_(220, 0, 0);
        if (grade == 2) return RGB_(255, 170, 170);
        return RGB_(255, 215, 215);
    }
    if (grade == 8) return RGB_(0, 150, 255);
    if (grade == 7) return RGB_(0, 120, 160);
    if (grade == 6) return RGB_(0, 90, 220);
    if (grade == 5) return RGB_(0, 0, 140);
    if (grade == 4) return RGB_(0, 150, 150);
    if (grade == 3) return RGB_(0, 0, 200);
    if (grade == 2) return RGB_(150, 175, 255);
    return RGB_(205, 215, 255);
}

static int entry_size(int grade) {
    if (grade == 8 || grade == 7 || grade == 6 || grade == 3) return 7;
    if (grade == 5) return 9;
    if (grade == 4) return 6;
    if (grade == 2) return 3;
    return 2;
}

static uint32_t wave_sig_rgb(int dir, int grade) {
    if (dir > 0) {
        if (grade == 3) return RGB_(200, 0, 0);
        if (grade == 2) return RGB_(255, 90, 90);
        return RGB_(255, 170, 170);
    }
    if (grade == 3) return RGB_(0, 0, 200);
    if (grade == 2) return RGB_(90, 120, 255);
    return RGB_(170, 190, 255);
}

static uint32_t resume_rgb(int dir, int grade) {
    if (dir > 0) {
        if (grade == 3) return RGB_(230, 100, 0);
        if (grade == 2) return RGB_(255, 150, 50);
        return RGB_(255, 200, 140);
    }
    if (grade == 3) return RGB_(110, 0, 170);
    if (grade == 2) return RGB_(160, 80, 220);
    return RGB_(205, 170, 240);
}

static void fill_wave(tr_fxwave_view_t *v, const tr_fxadj_out_t *a) {
    int show = a->valid && (a->in_adj || a->sig_dir != 0);
    memset(v, 0, sizeof(*v));
    v->time_r = show ? a->time_ratio : 0;
    v->price_r = show ? a->price_ratio : 0;
    v->opp_r = show ? a->opp_ratio : 0;
    if (!a->valid) {
        return;
    }
    if (a->flipped) {
        v->state = a->flipped;
        v->state_rgb = RGB_(90, 90, 90);
    } else if (a->trend > 0) {
        v->state = a->trend;
        v->state_rgb = a->in_adj ? RGB_(255, 180, 180) : RGB_(220, 0, 0);
    } else if (a->trend < 0) {
        v->state = a->trend;
        v->state_rgb = a->in_adj ? RGB_(180, 190, 255) : RGB_(0, 0, 220);
    }
    if (a->sig_dir != 0) {
        v->sig = a->sig_dir;
        v->sig_w = a->sig_grade == 3 ? 7 : (a->sig_grade == 2 ? 5 : 3);
        v->sig_rgb = wave_sig_rgb(a->sig_dir, a->sig_grade);
    }
}

static void copy_wave_view(const tr_fxwave_view_t *v, double *time_r, double *price_r, double *opp_r,
                           int *state, uint32_t *state_rgb, int *sig, uint32_t *sig_rgb, int *sig_w) {
    *time_r = v->time_r;
    *price_r = v->price_r;
    *opp_r = v->opp_r;
    *state = v->state;
    *state_rgb = v->state_rgb;
    *sig = v->sig;
    *sig_rgb = v->sig_rgb;
    *sig_w = v->sig_w;
}

static int dot_w(int len) {
    if (len >= 60) {
        return 5;
    }
    if (len >= 30) {
        return 4;
    }
    return 3;
}

void tr_fxos_set_synth(tr_fxos_input_t *in, const tr_fxsyn_t *s5, const tr_fxsyn_t *s15,
                       const tr_fxsyn_t *s30) {
    if (in == 0) {
        return;
    }
    in->reg5_ok = s5 != 0 && s5->out_reg_valid ? 1 : 0;
    in->reg15_ok = s15 != 0 && s15->out_reg_valid ? 1 : 0;
    in->reg30_ok = s30 != 0 && s30->out_reg_valid ? 1 : 0;
    in->reg5 = s5 != 0 ? s5->out_reg : 0;
    in->reg15 = s15 != 0 ? s15->out_reg : 0;
    in->reg30 = s30 != 0 ? s30->out_reg : 0;
    in->mkt5_ok = s5 != 0 && s5->out_mkt_valid ? 1 : 0;
    in->mkt15_ok = s15 != 0 && s15->out_mkt_valid ? 1 : 0;
    in->mkt30_ok = s30 != 0 && s30->out_mkt_valid ? 1 : 0;
    in->mkt5 = s5 != 0 ? s5->out_mkt : 0;
    in->mkt15 = s15 != 0 ? s15->out_mkt : 0;
    in->mkt30 = s30 != 0 ? s30->out_mkt : 0;
}

void tr_fxos_eval(tr_fxos_t *s, const tr_fxos_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    /* 원본은 완성 봉마다 한 번이다. 같은 봉의 재평가(진행 중 체결)는 봉 수를 올리지 않는다. */
    if (in->is_new_bar || !s->has_bar_open) {
        s->hist_at_open = s->hist;
        s->pair_at_open = s->pair_st;
        s->zd_at_open = s->zd_st;
        s->flat_prev_open = s->flat_prev;
        s->prev_flat_open = s->prev_flat;
        s->prev_close_open = s->prev_close;
        s->prev_pos_open = s->prev_pos;
        s->prev_slope_open = s->prev_slope;
        s->has_flat_open = s->has_flat;
        s->prev_up_hi_open = s->prev_up_hi;
        s->prev_up_lo_open = s->prev_up_lo;
        s->prev_up_382_open = s->prev_up_382;
        s->prev_up_500_open = s->prev_up_500;
        s->prev_up_618_open = s->prev_up_618;
        s->prev_dn_hi_open = s->prev_dn_hi;
        s->prev_dn_lo_open = s->prev_dn_lo;
        s->prev_dn_382_open = s->prev_dn_382;
        s->prev_dn_500_open = s->prev_dn_500;
        s->prev_dn_618_open = s->prev_dn_618;
        s->has_bar_open = 1;
    } else {
        s->hist = s->hist_at_open;
        s->pair_st = s->pair_at_open;
        s->zd_st = s->zd_at_open;
        s->flat_prev = s->flat_prev_open;
        s->prev_flat = s->prev_flat_open;
        s->prev_close = s->prev_close_open;
        s->prev_pos = s->prev_pos_open;
        s->prev_slope = s->prev_slope_open;
        s->has_flat = s->has_flat_open;
        s->prev_up_hi = s->prev_up_hi_open;
        s->prev_up_lo = s->prev_up_lo_open;
        s->prev_up_382 = s->prev_up_382_open;
        s->prev_up_500 = s->prev_up_500_open;
        s->prev_up_618 = s->prev_up_618_open;
        s->prev_dn_hi = s->prev_dn_hi_open;
        s->prev_dn_lo = s->prev_dn_lo_open;
        s->prev_dn_382 = s->prev_dn_382_open;
        s->prev_dn_500 = s->prev_dn_500_open;
        s->prev_dn_618 = s->prev_dn_618_open;
    }
    memset(&s->out, 0, sizeof(s->out));
    s->out.rth_open = tr_fxos_rth_open((int)in->date, &s->filt);
    int reset = s->reset_bar;
    if (in->is_new_bar || s->session_bars == 0) {
        int64_t key = tr_fx_session_key_v1(in->date, in->time);
        reset = !s->has_key || key != s->key;
        s->has_key = true;
        s->key = key;
        s->reset_bar = reset;
        s->session_bars = reset ? 1 : s->session_bars + 1;
    }

    tr_fxc_input_t cin;
    memset(&cin, 0, sizeof(cin));
    cin.session_reset = reset;
    cin.session_bars = s->session_bars;
    cin.high = in->high;
    cin.low = in->low;
    cin.ticks = s->entry.cfg.predict_ticks > 0 ? s->entry.cfg.predict_ticks : s->cfg.predict_ticks;
    cin.is_new_bar = in->is_new_bar;
    tr_fxc_eval(&s->curve, &cin);

    tr_fxadx_input_t adxin;
    memset(&adxin, 0, sizeof(adxin));
    adxin.session_reset = reset;
    adxin.high = in->high;
    adxin.low = in->low;
    adxin.close = in->close;
    adxin.bar_open = in->bar_open;
    tr_fxadx_eval(&s->adx, &adxin);

    tr_fxprof_input_t pin;
    memset(&pin, 0, sizeof(pin));
    pin.session_reset = reset;
    pin.bar_open = in->bar_open;
    pin.high = in->high;
    pin.low = in->low;
    pin.close = in->close;
    pin.volume = in->volume;
    pin.price_scale = s->price_scale;
    pin.value_mult = s->entry.cfg.value_mult;
    pin.min_bars = s->entry.cfg.min_bars;
    pin.period = 0;
    tr_fxprof_eval(&s->session, &pin);

    tr_fxsq_input_t qin;
    memset(&qin, 0, sizeof(qin));
    qin.session_reset = reset;
    qin.is_new_bar = in->is_new_bar;
    qin.bar_open = in->bar_open;
    qin.high = in->high;
    qin.low = in->low;
    qin.close = in->close;
    qin.volume = in->volume;
    qin.price_scale = s->price_scale;
    qin.value_mult = s->entry.cfg.value_mult;
    qin.min_bars = s->entry.cfg.min_bars;
    qin.width_bars = s->entry.cfg.width_bars;
    qin.ratio_bars = s->entry.cfg.ratio_bars;
    qin.narrow_pct = s->entry.cfg.narrow_pct;
    qin.stage1_bars = s->entry.cfg.stage1_bars;
    qin.confirm_bars = s->entry.cfg.confirm_sq_bars;
    qin.confirm_closed = s->cfg.confirm_closed;
    tr_fxsq_eval(&s->squeeze, &qin);

    int fut = 0;
    if (!reset) {
        fut = s->curve.direction > 0.0 ? 1 : (s->curve.direction < 0.0 ? -1 : 0);
    }
    tr_fxdec_input_t din;
    memset(&din, 0, sizeof(din));
    din.future_dir = fut;
    din.state5 = s->session.out.state5;
    din.profile_valid = s->session.out.valid;
    din.center = s->session.out.center;
    din.close = in->close;
    din.mode = s->entry.cfg.profile_mode > 0 ? s->entry.cfg.profile_mode : 1;
    din.adx = s->adx.adx;
    din.plus_di = s->adx.plus_di;
    din.minus_di = s->adx.minus_di;
    din.adx_valid = s->adx.valid;
    din.adx_trend = s->entry.cfg.adx_trend;
    din.adx_strong = s->entry.cfg.adx_strong;
    tr_fxdec_out_t dec = tr_fxdec_eval(&din);
    int dir = dec.unified > 0 ? 1 : (dec.unified < 0 ? -1 : 0);
    uint32_t jrgb = judge_rgb(dec.unified);
    if (dir != 0 && (fut != dir || dec.di != dir || s->session.out.state5 * dir < 0)) {
        jrgb = RGB_(150, 150, 150);
    }

    int pos = tr_fxos_box_color(in->close, s->squeeze.out.band_hi, s->squeeze.out.band_lo,
                                s->squeeze.out.profile_valid);
    int stage = stage_of(s->squeeze.out.hold);
    int rel_w = dot_w(s->squeeze.out.release_len);
    int cf_w = dot_w(s->squeeze.out.confirm_len);
    uint32_t rel_rgb = RGB_(120, 120, 120);
    if (s->squeeze.out.release_dir > 0) {
        rel_rgb = RGB_(220, 0, 0);
    } else if (s->squeeze.out.release_dir < 0) {
        rel_rgb = RGB_(0, 0, 200);
    }

    s->out.judge = dec.unified;
    s->out.judge_rgb = jrgb;
    s->out.fut = fut;
    s->out.prof = s->session.out.state5;
    s->out.di = dec.di;
    s->out.adx = dec.adx;
    s->out.sq_on = s->squeeze.out.release;
    s->out.sq_len = s->squeeze.out.release_len;
    s->out.sq_rgb = rel_rgb;
    s->out.sq_w = s->squeeze.out.release_len >= 60 ? 8 : (s->squeeze.out.release_len >= 30 ? 7 : 6);
    s->out.hold = s->squeeze.out.hold;
    s->out.hold_rgb = hold_rgb(pos, stage);
    s->out.ratio_on = s->squeeze.out.profile_valid && s->squeeze.out.ratio > 0.0;
    s->out.ratio = s->squeeze.out.ratio;
    s->out.ratio_rgb = jrgb;
    s->out.rel_on = s->squeeze.out.release;
    s->out.rel_len = s->squeeze.out.release_len;
    s->out.rel_rgb = rel_rgb;
    s->out.rel_w = rel_w;
    s->out.cf_on = s->squeeze.out.confirm_dir != 0;
    s->out.cf_len = s->squeeze.out.confirm_len;
    s->out.cf_rgb = s->squeeze.out.confirm_dir > 0 ? RGB_(220, 0, 0) : RGB_(0, 0, 200);
    s->out.cf_w = cf_w;

    tr_fxec_input_t ein;
    memset(&ein, 0, sizeof(ein));
    ein.date = in->date;
    ein.time = in->time;
    ein.bar_open = in->bar_open;
    ein.is_new_bar = in->is_new_bar;
    ein.high = in->high;
    ein.low = in->low;
    ein.close = in->close;
    ein.volume = in->volume;
    {
        tr_fxfv_input_t fin;
        memset(&fin, 0, sizeof(fin));
        fin.date = in->date;
        fin.time = in->time;
        fin.bar_open = in->bar_open;
        fin.high = in->high;
        fin.low = in->low;
        fin.close = in->close;
        fin.volume = in->volume;
        if (s->sig_fv.cfg.price_scale > 0.0) {
            tr_fxfv_eval(&s->sig_fv, &fin);
        }
    }
    ein.fv = s->sig_fv.cfg.price_scale > 0.0 ? &s->sig_fv.out : in->fv;
    tr_fxec_eval(&s->entry, &ein);
    int ent_dir = s->entry.out.dir;
    int ent_grade = s->entry.out.grade;
    const tr_fxfv_output_t *fv = ein.fv;
    double flat = fv != 0 ? fv->reg_flat : 0.0;
    tr_fxflat_out_t flat_out;
    tr_fxflat_step(&s->flat_prev, reset, flat, s->prev_flat, in->high, in->low,
                   in->close, s->prev_close, s->price_scale, 3, &flat_out);
    tr_fxadj_in_t win;
    tr_fxadj_out_t aout;
    memset(&win, 0, sizeof(win));
    win.flat = flat;
    win.flat_prev = s->prev_flat;
    win.high = in->high;
    win.low = in->low;
    win.close = in->close;
    win.close_prev = s->prev_close;
    win.price_scale = s->price_scale;
    if (fv != 0) {
        win.up_hi = fv->lup_high;
        win.up_lo = fv->lup_low;
        win.up_382 = fv->lup_382;
        win.dn_hi = fv->ldn_high;
        win.dn_lo = fv->ldn_low;
        win.dn_618 = fv->ldn_618;
    }
    win.session_reset = reset;
    win.new_bar = in->is_new_bar;
    win.min_leg = s->cfg.min_leg;
    win.min_trend = s->cfg.min_trend;
    win.min_opp = s->cfg.min_opp;
    win.confirm_back = s->cfg.confirm_back;
    win.strong_px = s->cfg.strong_px;
    win.strong_time = s->cfg.strong_time;
    win.mid_px = s->cfg.mid_px;
    win.start_b_only = s->cfg.start_b_only;
    win.struct_boost = s->cfg.struct_boost;
    win.struct_px = s->cfg.struct_px;
    win.signal_limit = s->cfg.signal_limit;
    win.flip_opp_pct = s->cfg.flip_opp_pct;
    win.rev = 6;
    tr_fxadj_eval(&s->adj, &win, &aout);
    {
        tr_fxadj_in_t vin;
        tr_fxadj_out_t vout;
        vin = win;
        vin.up_hi = vin.up_lo = vin.up_382 = 0;
        vin.dn_hi = vin.dn_lo = vin.dn_618 = 0;
        vin.start_b_only = 0;
        vin.struct_boost = 0;
        vin.signal_limit = 0;
        vin.flip_opp_pct = 0;
        vin.rev = 1;
        tr_fxadj_eval(&s->adj1, &vin, &vout);
        fill_wave(&s->out.wv[0], &vout);
        vin = win;
        vin.signal_limit = 0;
        vin.flip_opp_pct = 0;
        vin.rev = 2;
        tr_fxadj_eval(&s->adj2, &vin, &vout);
        fill_wave(&s->out.wv[1], &vout);
        vin = win;
        vin.signal_limit = 90;
        vin.flip_opp_pct = 100;
        vin.rev = 4;
        tr_fxadj_eval(&s->adj4, &vin, &vout);
        fill_wave(&s->out.wv[2], &vout);
        vin = win;
        vin.signal_limit = 120;
        vin.flip_opp_pct = 100;
        vin.rev = 5;
        tr_fxadj_eval(&s->adj5, &vin, &vout);
        fill_wave(&s->out.wv[3], &vout);
    }

    if (s->cfg.slope_limit > 0 && ent_dir != 0 && ent_grade <= s->cfg.grade_limit) {
        if (ent_dir > 0 && flat_out.prev_up_slope >= s->cfg.slope_limit) {
            ent_dir = 0;
        }
        if (ent_dir < 0 && flat_out.prev_dn_slope >= s->cfg.slope_limit) {
            ent_dir = 0;
        }
    }
    if (ent_dir != 0) {
        s->out.ent_on = 1;
        s->out.ent_y = ent_dir > 0 ? -110 : 110;
        s->out.ent_rgb = entry_rgb(ent_dir, ent_grade);
        s->out.ent_w = entry_size(ent_grade);
    }

    s->out.flat_pos = flat_out.pos;
    s->out.flat_slope = flat_out.slope;
    s->out.flat_on = flat_out.valid ? 1 : 0;
    s->out.flat_pos_rgb = flat_out.pos > 0 ? RGB_(255, 150, 150)
                          : (flat_out.pos < 0 ? RGB_(150, 170, 255) : RGB_(180, 180, 180));
    s->out.flat_slope_rgb = flat_out.slope > 0 ? RGB_(200, 0, 0)
                            : (flat_out.slope < 0 ? RGB_(0, 0, 200) : RGB_(120, 120, 120));
    if (reset) {
        s->flat_max_range = 0;
    }
    if (flat_out.valid && flat_out.range_ticks > s->flat_max_range) {
        s->flat_max_range = flat_out.range_ticks;
    }
    s->out.flat_amp = (flat_out.valid && s->flat_max_range > 0.0)
                      ? flat_out.range_ticks / s->flat_max_range * 100.0 : 0;
    s->out.flat_up_pos = flat_out.prev_up_pos;
    s->out.flat_dn_pos = -flat_out.prev_dn_pos;
    s->out.flat_up_slope = flat_out.prev_up_slope;
    s->out.flat_dn_slope = -flat_out.prev_dn_slope;

    int adj_show = aout.valid && aout.sig_dir != 0 &&
                   (s->cfg.adj_unified != 1 || dec.unified * aout.sig_dir >= 50) &&
                   (s->cfg.adj_min <= 0 || aout.price_ratio >= s->cfg.adj_min || aout.time_ratio >= s->cfg.adj_min);
    if (s->cfg.buy_382 && adj_show && aout.sig_dir > 0 && fv != 0 && fv->lup_382 > 0 && in->high < fv->lup_382) {
        adj_show = 0;
    }
    /* 조정연빨강포함 기본 1: 상승 추세의 연빨강 조정도 빨강으로 센다. */
    int up_bar = aout.valid && aout.trend == 1 && (aout.in_adj == 0 || s->cfg.state_pink);
    if (reset) {
        s->up_i = 0;
        memset(s->up_state, 0, sizeof(s->up_state));
        s->adj_done = 0;
    }
    if (s->prev_adj_trend != aout.trend) {
        s->adj_done = 0;
    }
    if (s->cfg.state_bars > 0 && adj_show && aout.sig_dir > 0 && in->time >= 70000 && in->time < s->out.rth_open &&
        s->up_i >= s->cfg.state_bars) {
        int sum = 0;
        int n = s->cfg.state_bars;
        for (int i = 0; i < n; i++) {
            int idx = (s->up_i - 1 - i) % 60;
            if (idx < 0) {
                idx += 60;
            }
            sum += s->up_state[idx];
        }
        if (sum >= n * s->cfg.state_pct / 100) {
            adj_show = 0;
        }
    }
    if (adj_show && aout.sig_dir < 0) {
        if (s->cfg.sell_u100 && (dec.unified > -100 || fut != -1 || dec.di != -1 || s->session.out.state5 > 0)) {
            adj_show = 0;
        }
        if (s->cfg.sell_low > 0 && fv != 0 && fv->ldn_high > fv->ldn_low && fv->ldn_low > 0) {
            double pct = (in->close - fv->ldn_low) / (fv->ldn_high - fv->ldn_low) * 100.0;
            if (pct >= 0.0 && pct <= s->cfg.sell_low) {
                adj_show = 0;
            }
        }
    }
    if (s->cfg.adj_once && s->adj_done && adj_show) {
        adj_show = 0;
    }
    if (adj_show) {
        s->adj_done = 1;
        int g = aout.sig_grade;
        s->out.adj_on = 1;
        s->out.adj_y = aout.sig_dir > 0 ? -130 : 130;
        s->out.adj_w = g == 3 ? 7 : (g == 2 ? 5 : 3);
        if (aout.sig_dir > 0) {
            s->out.adj_rgb = g == 3 ? RGB_(230, 100, 0) : (g == 2 ? RGB_(255, 150, 50) : RGB_(255, 200, 140));
        } else {
            s->out.adj_rgb = g == 3 ? RGB_(110, 0, 170) : (g == 2 ? RGB_(160, 80, 220) : RGB_(205, 170, 240));
        }
    }
    s->prev_adj_trend = aout.trend;
    s->up_state[s->up_i % 60] = (unsigned char)up_bar;
    s->up_i += 1;

    int brk = 0;
    if (fv != 0 && s->has_flat && !reset) {
        int relax = s->cfg.relax;
        double buy_base = relax >= 3 ? s->prev_up_382 : (relax == 2 ? s->prev_up_500 : s->prev_up_618);
        double sell_base = relax >= 3 ? s->prev_dn_618 : (relax == 2 ? s->prev_dn_500 : s->prev_dn_382);
        int buy_line = buy_base < s->prev_dn_lo && s->prev_dn_hi >= s->prev_up_hi;
        int sell_line = sell_base > s->prev_up_hi && s->prev_up_lo <= s->prev_dn_lo;
        int uni_buy = s->cfg.brk_check != 1 || dec.unified >= 50;
        int uni_sell = s->cfg.brk_check != 1 || dec.unified <= -50;
        if (buy_line && uni_buy && in->close > s->prev_dn_hi && s->prev_close <= s->prev_dn_hi &&
            (s->cfg.brk_once != 1 || s->prev_dn_hi != s->buy_used_px)) {
            brk = 1;
            s->buy_used_px = s->prev_dn_hi;
        } else if (sell_line && uni_sell && in->close < s->prev_up_lo && s->prev_close >= s->prev_up_lo &&
                   (s->cfg.brk_once != 1 || s->prev_up_lo != s->sell_used_px)) {
            brk = -1;
            s->sell_used_px = s->prev_up_lo;
        }
    }
    if (reset) {
        s->buy_used_px = 0;
        s->sell_used_px = 0;
        s->lead_dn_cross = 1;
        s->lead_up_cross = 1;
    }
    if (brk != 0) {
        s->out.brk_on = 1;
        s->out.brk_y = brk > 0 ? -150 : 150;
        s->out.brk_rgb = brk > 0 ? RGB_(255, 190, 0) : RGB_(0, 200, 200);
    }

    int lead = 0;
    int cross = 0;
    if (flat_out.valid && !reset) {
        if (flat_out.slope < 0 && s->prev_slope >= 0) {
            s->lead_dn_cross = 0;
        }
        if (flat_out.slope > 0 && s->prev_slope <= 0) {
            s->lead_up_cross = 0;
        }
        if (flat_out.slope < -s->cfg.slope_min && !s->lead_dn_cross && flat_out.pos < 0 && s->prev_pos >= 0) {
            cross = -1;
        }
        if (flat_out.slope > s->cfg.slope_min && !s->lead_up_cross && flat_out.pos > 0 && s->prev_pos <= 0) {
            cross = 1;
        }
        if (flat_out.slope < 0 && flat_out.pos < 0) {
            s->lead_dn_cross = 1;
        }
        if (flat_out.slope > 0 && flat_out.pos > 0) {
            s->lead_up_cross = 1;
        }
    }
    if (cross != 0) {
        s->out.flat_mark = cross;
    }
    lead = cross;
    int gray = jrgb == RGB_(150, 150, 150);
    if (s->cfg.lead_u100 && lead < 0 &&
        (dec.unified > -100 || fut != -1 || dec.di != -1 || s->session.out.state5 > 0 || gray)) {
        lead = 0;
    }
    if (s->cfg.lead_u100 && lead > 0 &&
        (dec.unified < 100 || fut != 1 || dec.di != 1 || s->session.out.state5 < 0 || gray)) {
        lead = 0;
    }
    if (lead != 0) {
        s->out.lead_on = 1;
        s->out.lead_y = lead > 0 ? -170 : 170;
        s->out.lead_rgb = lead > 0 ? RGB_(0, 180, 0) : RGB_(0, 0, 0);
    }
    {
        tr_fxos_hist_in_t hin;
        memset(&hin, 0, sizeof(hin));
        hin.session_first = reset;
        hin.t1 = s->sig_fv.out.persist_target[0];
        hin.t2 = s->sig_fv.out.persist_target[1];
        hin.t3 = s->sig_fv.out.persist_target[2];
        hin.open = in->open;
        hin.high = in->high;
        hin.low = in->low;
        hin.close = in->close;
        hin.two_window = 240;
        hin.two_min_bars = 3;
        hin.flat_window = 240;
        hin.fv_ok = 1;
        hin.rise_hi = s->sig_fv.out.lup_high;
        hin.rise_lo = s->sig_fv.out.lup_low;
        hin.fall_hi = s->sig_fv.out.ldn_high;
        hin.fall_lo = s->sig_fv.out.ldn_low;
        hin.prev_dn_len = flat_out.prev_dn_slope;
        hin.prev_up_len = flat_out.prev_up_slope;
        hin.slope = flat_out.slope;
        hin.sq_ratio = s->squeeze.out.ratio;
        hin.sq_hold = s->squeeze.out.hold;
        hin.sq_color = pos;
        hin.flat_1 = s->sig_fv.out.reg_flat;
        hin.flat_5 = in->reg5_ok ? in->reg5 : 0;
        hin.flat_15 = in->reg15_ok ? in->reg15 : 0;
        hin.flat_30 = in->reg30_ok ? in->reg30 : 0;
        hin.flat_ready = s->sig_fv.out.reg_flat != 0.0 && in->reg5_ok && in->reg15_ok && in->reg30_ok;
        hin.mkt_1 = s->sig_fv.out.market_center;
        hin.mkt5_ok = in->mkt5_ok;
        hin.mkt15_ok = in->mkt15_ok;
        hin.mkt30_ok = in->mkt30_ok;
        hin.mkt_5 = in->mkt5;
        hin.mkt_15 = in->mkt15;
        hin.mkt_30 = in->mkt30;
        hin.price_scale = s->price_scale;
        hin.slack_ticks = 1;
        hin.prev_up_slope = flat_out.prev_up_slope;
        hin.prev_dn_slope = flat_out.prev_dn_slope;
        hin.unified = dec.unified;
        tr_fxos_hist_bar(&s->hist, &hin);
    }
    {
        tr_fxos_filter_in_t fin;
        tr_fxos_filter_out_t fout;
        memset(&fin, 0, sizeof(fin));
        fin.unified = dec.unified;
        fin.cand_dir = ent_dir;
        fin.cand_grade = ent_grade;
        fin.lead_dir = lead;
        fin.brk_dir = brk;
        fin.adj_dir = adj_show ? aout.sig_dir : 0;
        fin.adj_kind = aout.sig_kind;
        fin.adj_grade = aout.sig_grade;
        fin.date = (int)in->date;
        fin.time = (int)in->time;
        fin.lead_valid = flat_out.valid ? 1 : 0;
        fin.session_first = reset ? 1 : 0;
        fin.slope = flat_out.slope;
        fin.slope_prev = s->prev_slope;
        fin.pos = flat_out.pos;
        fin.low = in->low;
        fin.high = in->high;
        fin.close = in->close;
        fin.price_unit = in->price_unit;
        fin.three_ratio = s->hist.three_ratio;
        fin.two_ratio = s->hist.two_ratio;
        fin.flat_ratio = s->hist.flat_ratio;
        fin.swing = s->hist.swing;
        fin.prior_low = s->hist.prior_low;
        fin.prior_high = s->hist.prior_high;
        fin.width_seen = s->hist.width_seen;
        fin.tall_buy = s->hist.tall_buy;
        fin.tall_sell = s->hist.tall_sell;
        fin.hot_up = s->hist.hot_up;
        fin.hot_dn = s->hist.hot_dn;
        fin.red_n = s->hist.red_n;
        fin.bars_since_buy = s->hist.bars_since_buy;
        fin.had_m100 = s->hist.had_m100;
        fin.had_p100 = s->hist.had_p100;
        fin.target1 = s->sig_fv.out.persist_target[0];
        fin.target5 = s->sig_fv.out.persist_target[4];
        fin.rise_hi = s->sig_fv.out.lup_high;
        fin.rise_lo = s->sig_fv.out.lup_low;
        fin.fall_hi = s->sig_fv.out.ldn_high;
        fin.fall_lo = s->sig_fv.out.ldn_low;
        fin.sq_bars = s->squeeze.out.hold;
        fin.width_ratio = s->squeeze.out.ratio;
        fin.amp = s->out.flat_amp;
        fin.gray = s->hist.gray;
        fin.prev_up = flat_out.prev_up_slope;
        fin.prev_dn = flat_out.prev_dn_slope;
        s->applied = fin;
        tr_fxos_filter(&s->filt, &fin, &fout);
        if (fout.turn_dir > 0) {
            s->hist.bars_since_buy = 0;
        }
        ent_dir = fout.cand_dir;
        brk = fout.brk_dir;
        lead = fout.lead_dir;
        if (fout.adj_dir == 0) {
            adj_show = 0;
            s->out.adj_on = 0;
        }
        if (ent_dir == 0) {
            s->out.ent_on = 0;
        }
        if (brk == 0) {
            s->out.brk_on = 0;
        }
        if (lead == 0) {
            s->out.lead_on = 0;
        }
        s->out.turn_on = fout.turn_dir != 0 ? 1 : 0;
        s->out.turn_y = fout.turn_y;
        s->out.turn_rgb = fout.turn_rgb;
        s->out.turn_w = fout.turn_w;
        s->out.sig_grade = fout.cand_grade;
        s->out.sig_adj_kind = fout.adj_kind;
        s->out.sig_adj_grade = fout.adj_grade;
    }

    {
        tr_fxwave_view_t v6;
        fill_wave(&v6, &aout);
        copy_wave_view(&v6, &s->out.wave_time, &s->out.wave_price, &s->out.wave_opp,
                       &s->out.wave_state, &s->out.wave_state_rgb,
                       &s->out.wave_sig, &s->out.wave_sig_rgb, &s->out.wave_sig_w);
    }
    {
        int raw_dir = s->entry.out.dir;
        int raw_grade = s->entry.out.grade;
        int line_dir = s->ec_closed_ok ? s->ec_closed_dir : 0;
        double line_px = s->ec_closed_ok ? s->ec_closed_px : 0;
        if (in->is_new_bar && s->ec_has) {
            s->ec_closed_dir = s->ec_dir;
            s->ec_closed_px = s->ec_px_st;
            s->ec_closed_ok = 1;
            line_dir = s->ec_closed_dir;
            line_px = s->ec_closed_px;
        }
        if (reset) {
            line_dir = 0;
            line_px = 0;
        }
        if (raw_dir != 0) {
            line_dir = raw_dir;
            line_px = in->close;
            s->out.ec_on = 1;
            s->out.ec_px = s->entry.out.price;
            s->out.ec_rgb = entry_rgb(raw_dir, raw_grade);
            s->out.ec_w = entry_size(raw_grade);
        }
        s->ec_dir = line_dir;
        s->ec_px_st = line_px;
        s->ec_has = 1;
        if (line_dir != 0 && line_px > 0) {
            s->out.ec_line_on = 1;
            s->out.ec_line = line_px;
            s->out.ec_line_rgb = line_dir > 0 ? RGB_(255, 120, 120) : RGB_(120, 150, 255);
        }
        s->out.swing_on = s->entry.st.swing_ok;
        s->out.swing_pct = s->entry.st.wpct;
        if (aout.valid && aout.sig_dir != 0) {
            int g = aout.sig_grade;
            s->out.rs_on = 1;
            s->out.rs_w = g == 3 ? 7 : (g == 2 ? 5 : 3);
            s->out.rs_rgb = resume_rgb(aout.sig_dir, g);
            s->out.rs_px = aout.sig_dir > 0 ? in->low - 10.0 * s->price_scale
                                            : in->high + 10.0 * s->price_scale;
        }
    }
    if (fv != 0) {
        int valid = fv->lup_high > 0 && fv->lup_low > 0 && fv->ldn_high > 0 && fv->ldn_low > 0;
        int buy = valid && fv->lup_500 < fv->ldn_low && fv->ldn_high >= fv->lup_high;
        int sell = valid && fv->ldn_500 > fv->lup_high && fv->lup_low <= fv->ldn_low;
        int broke_up = 0, broke_dn = 0;
        if (in->is_new_bar && s->bk_has) {
            s->bk_prev_buy = s->bk_buy;
            s->bk_prev_sell = s->bk_sell;
            s->bk_prev_dn_hi = s->bk_dn_hi;
            s->bk_prev_up_lo = s->bk_up_lo;
            s->bk_prev_close = s->bk_close;
            s->bk_has_prev = 1;
        }
        broke_up = s->bk_has_prev && s->bk_prev_buy && in->close > s->bk_prev_dn_hi &&
                   s->bk_prev_close <= s->bk_prev_dn_hi;
        broke_dn = s->bk_has_prev && s->bk_prev_sell && in->close < s->bk_prev_up_lo &&
                   s->bk_prev_close >= s->bk_prev_up_lo;
        if (broke_up) {
            s->out.bk_on = 1;
            s->out.bk_px = in->low - 16.0 * s->price_scale;
            s->out.bk_rgb = RGB_(255, 190, 0);
        } else if (broke_dn) {
            s->out.bk_on = 1;
            s->out.bk_px = in->high + 16.0 * s->price_scale;
            s->out.bk_rgb = RGB_(0, 200, 200);
        }
        s->bk_buy = buy;
        s->bk_sell = sell;
        s->bk_dn_hi = fv->ldn_high;
        s->bk_up_lo = fv->lup_low;
        s->bk_close = in->close;
        s->bk_has = 1;
    }
    {
        double mid = (in->high + in->low) / 2.0;
        double sess_fut, fut_part, sum;
        int reg_dir = 0, mkt_dir = 0, ob_dir = 0, stage, i, n;
        double flat_line = in->reg_line;
        if (in->is_new_bar || !s->paint_has) {
            if (s->paint_has) {
                s->paint_fut_1 = s->paint_fut;
                s->mkt_avg_1 = s->mkt_avg;
                s->mkt_avg_1_ok = 1;
            }
            s->paint_first = !s->paint_has || in->session_first;
            if (s->mkt_n < 20) {
                s->mkt_i = s->mkt_n++;
            } else {
                s->mkt_i = (s->mkt_i + 1) % 20;
            }
        }
        s->mkt_buf[s->mkt_i] = mid;
        sum = 0;
        n = s->mkt_n;
        for (i = 0; i < n; i++) {
            sum += s->mkt_buf[i];
        }
        s->mkt_avg = n > 0 ? sum / (double)n : mid;
        sess_fut = s->paint_first ? 0 : in->htf_dir;
        if (s->price_scale > 0) {
            flat_line = round(in->reg_line / s->price_scale) * s->price_scale;
        }
        if (in->reg_valid && in->reg_r2 >= 0.40) {
            if (in->close > flat_line) {
                reg_dir = 1;
            }
            if (in->close < flat_line) {
                reg_dir = -1;
            }
        }
        if (s->mkt_avg_1_ok) {
            double slope = s->mkt_avg - s->mkt_avg_1;
            if (in->close > s->mkt_avg && slope > 0) {
                mkt_dir = 1;
            }
            if (in->close < s->mkt_avg && slope < 0) {
                mkt_dir = -1;
            }
        }
        if (in->ob_valid) {
            if (in->ob_score > 0) {
                ob_dir = 1;
            } else if (in->ob_score < 0) {
                ob_dir = -1;
            }
        }
        fut_part = (sess_fut > 0 ? 2 : -2);
        if (s->paint_has && sess_fut > s->paint_fut_1) {
            fut_part += 1;
        }
        if (s->paint_has && sess_fut < s->paint_fut_1) {
            fut_part -= 1;
        }
        stage = (int)fut_part + mkt_dir + reg_dir + ob_dir;
        if (stage > 4) {
            s->out.os_paint = RGB_(220, 0, 0);
        } else if (stage > 2) {
            s->out.os_paint = RGB_(255, 100, 70);
        } else if (stage > 0) {
            s->out.os_paint = RGB_(255, 185, 185);
        } else if (stage < -4) {
            s->out.os_paint = RGB_(0, 0, 180);
        } else if (stage < -2) {
            s->out.os_paint = RGB_(60, 130, 255);
        } else if (stage < 0) {
            s->out.os_paint = RGB_(180, 210, 255);
        } else {
            s->out.os_paint = RGB_(150, 150, 150);
        }
        s->paint_fut = sess_fut;
        s->paint_has = 1;
    }

    s->out.paint_rgb = jrgb;
    if (adj_show) {
        s->out.paint_rgb = aout.sig_dir > 0 ? RGB_(255, 140, 0) : RGB_(150, 0, 220);
    }
    if (s->out.turn_on) {
        s->out.paint_rgb = s->out.turn_y < 0 ? RGB_(0, 100, 0) : RGB_(150, 0, 80);
    }
    if (lead != 0) {
        s->out.paint_rgb = lead > 0 ? RGB_(150, 75, 0) : RGB_(0, 0, 0);
    }
    if (brk != 0) {
        s->out.paint_rgb = brk > 0 ? RGB_(0, 200, 0) : RGB_(255, 0, 255);
    }
    if (ent_dir != 0) {
        s->out.paint_rgb = ent_dir > 0 ? RGB_(0, 255, 0) : RGB_(0, 255, 255);
    }
    {
        tr_fxp95_in_t p95;
        memset(&p95, 0, sizeof(p95));
        p95.date = in->date;
        p95.time = in->time;
        p95.is_new_bar = in->is_new_bar;
        p95.session_reset = reset;
        p95.open = in->open;
        p95.high = in->high;
        p95.low = in->low;
        p95.close = in->close;
        p95.volume = in->volume;
        p95.judge = dec.unified;
        p95.fut = fut;
        p95.di = dec.di;
        p95.state5 = s->session.out.state5;
        p95.pos = flat_out.pos;
        p95.slope = flat_out.slope;
        p95.pos_prev = s->flat_prev.pos;
        if (fv != 0) {
            p95.target5 = fv->persist_target[4];
            p95.up_lo = fv->lup_low;
            p95.up_hi = fv->lup_high;
        }
        if (s->entry.st.swing_ok) p95.swing_ratio = s->entry.st.wpct;
        tr_fxp95_bar(&s->p95, &p95);
        if (s->p95.group != 0) {
            s->out.paint_rgb = s->p95.rgb;
        }
    }

    /* 진입신호매매V1: 색방향, 신호 우선순위, 매매시간, 청산, 진입. */
    int color_dir = dec.unified > 0 ? 1 : (dec.unified < 0 ? -1 : 0);
    if (color_dir != 0 && (fut != color_dir || dec.di != color_dir || s->session.out.state5 * color_dir < 0)) {
        color_dir = 0;
    }
    int hms = (int)in->time;
    int trade_time = s->cfg.trade_start > s->cfg.trade_end
                         ? (hms >= s->cfg.trade_start || hms < s->cfg.trade_end)
                         : (hms >= s->cfg.trade_start && hms < s->cfg.trade_end);
    int sig = 0;
    int kind = 0;
    if (adj_show && aout.sig_dir != 0) {
        sig = aout.sig_dir;
        kind = 4;
    }
    if (s->out.turn_on) {
        sig = s->out.turn_y < 0 ? 1 : -1;
        kind = 5;
    }
    if (lead != 0) {
        sig = lead;
        kind = 3;
    }
    if (brk != 0) {
        sig = brk;
        kind = 2;
    }
    if (ent_dir != 0) {
        sig = ent_dir;
        kind = 1;
    }
    /* 수량 없는 청산은 잔량을 모두 닫는다. 진입 수량은 설정창 수량. 종가 체결. */
    int qty = s->cfg.order_qty > 0 ? s->cfg.order_qty : 1;
    if (s->mkt > 0 && color_dir == -1 && s->prev_color_dir != -1) {
        s->out.sig_exit = 1;
        s->out.exit_qty = s->contracts;
        s->mkt = 0;
        s->contracts = 0;
        s->avg_entry = 0;
    }
    if (s->mkt < 0 && color_dir == 1 && s->prev_color_dir != 1) {
        s->out.sig_exit = -1;
        s->out.exit_qty = s->contracts;
        s->mkt = 0;
        s->contracts = 0;
        s->avg_entry = 0;
    }
    if (!trade_time && s->mkt > 0) {
        s->out.sig_exit = 2;
        s->out.exit_qty = s->contracts;
        s->mkt = 0;
        s->contracts = 0;
        s->avg_entry = 0;
    }
    if (!trade_time && s->mkt < 0) {
        s->out.sig_exit = -2;
        s->out.exit_qty = s->contracts;
        s->mkt = 0;
        s->contracts = 0;
        s->avg_entry = 0;
    }
    s->out.raw_dir = sig;
    s->out.raw_kind = kind;
    s->out.color_dir = color_dir;
    s->out.session_first = reset;
    if (trade_time && sig == 1 && s->mkt != 1) {
        s->mkt = 1;
        s->contracts = qty;
        s->avg_entry = in->close;
        s->out.sig_dir = 1;
        s->out.sig_kind = kind;
    }
    if (trade_time && sig == -1 && s->mkt != -1) {
        s->mkt = -1;
        s->contracts = qty;
        s->avg_entry = in->close;
        s->out.sig_dir = -1;
        s->out.sig_kind = kind;
    }
    /* 승률 화살은 tr_fxos_wr_bar 가 원본 주문 이름으로 채운다. 진입신호 이름으로 대체하지 않는다. */
    s->out.wr_dir = 0;
    s->out.wr_order = 0;
    s->out.wr_exit_dir = 0;
    s->out.wr_name[0] = '\0';
    s->out.wr_exit_name[0] = '\0';
    s->prev_color_dir = color_dir;

    /* 수익관리는 이 시스템트레이딩의 I_MarketPosition, I_AvgEntryPrice, I_CurrentContracts. */
    {
        tr_fxpnl_in_t pin;
        tr_fxpnl_out_t pout;
        memset(&pin, 0, sizeof(pin));
        pin.date = in->date;
        pin.high = in->high;
        pin.low = in->low;
        pin.close = in->close;
        pin.price_scale = s->price_scale;
        pin.market = s->mkt;
        pin.contracts = s->contracts;
        pin.avg_entry = s->avg_entry;
        pin.qty_full = s->cfg.qty_full;
        pin.qty_part = s->cfg.qty_part;
        pin.new_bar = in->is_new_bar;
        tr_fxpnl_eval(&s->pnl, &pin, &pout);
        s->out.pnl_open = pout.open_pts;
        s->out.pnl_mfe = pout.mfe_pts;
        s->out.pnl_mae = pout.mae_pts;
        s->out.pnl_closed = pout.closed_pts;
        s->out.pnl_keep = pout.keep_pts;
        s->out.pnl_entries = pout.entries;
        s->out.pnl_wins = pout.wins;
        s->out.pnl_open_rgb = pout.open_rgb;
        s->out.pnl_mfe_rgb = pout.mfe_rgb;
        s->out.pnl_mae_rgb = pout.mae_rgb;
        s->out.pnl_keep_rgb = pout.keep_rgb;
        s->out.pnl_open_w = pout.open_w;
        s->out.pnl_mfe_w = pout.mfe_w;
        s->out.pnl_mae_w = pout.mae_w;
        s->out.pnl_keep_w = pout.keep_on ? 1 : 0;
        s->out.pnl_open_on = pout.open_on;
        s->out.pnl_mfe_on = pout.mfe_on;
        s->out.pnl_mae_on = pout.mae_on;
        s->out.pnl_keep_on = pout.keep_on;
        s->out.pnl_p4 = pout.p4;
        s->out.pnl_p5 = pout.p5;
        s->out.pnl_p30 = pout.p30;
        s->out.pnl_keep2 = pout.keep2_pts;
        s->out.pnl_p4_on = pout.p4_on;
        s->out.pnl_p5_on = pout.p5_on;
        s->out.pnl_p30_on = pout.p30_on;
        s->out.pnl_keep2_on = pout.keep2_on;
        s->out.pnl_long = pout.closed_long_pts;
        s->out.pnl_long_on = pout.closed_long_on;
        s->out.pnl_long_rgb = pout.closed_long_rgb;
        s->out.pnl_side = pout.side;
        s->out.pnl_short = pout.closed_short_pts;
        s->out.pnl_short_on = pout.closed_short_on;
        s->out.pnl_exit = pout.exit_pts;
        s->out.pnl_exit_on = pout.exit_on;
        {
            tr_fxpos_in_t xin;
            memset(&xin, 0, sizeof(xin));
            xin.date = in->date;
            xin.time = in->time;
            xin.high = in->high;
            xin.low = in->low;
            xin.close = in->close;
            xin.price_scale = s->price_scale;
            xin.tick_mult = 1;
            xin.partial_ticks = 10;
            xin.market = s->mkt;
            xin.contracts = s->contracts;
            xin.avg = s->avg_entry;
            tr_fxpos_eval(&s->pos1, &xin, &s->out.pos_v1);
            xin.v2 = 1;
            tr_fxpos_eval(&s->pos2, &xin, &s->out.pos_v2);
        }
        {
            tr_fxpair_in_t pin;
            tr_fxpair_out_t pout;
            memset(&pin, 0, sizeof(pin));
            pin.dir = s->out.raw_dir;
            pin.kind = s->out.raw_kind;
            pin.grade = s->out.sig_grade;
            pin.adj_kind = s->out.sig_adj_kind;
            pin.adj_grade = s->out.sig_adj_grade;
            pin.unified = s->out.judge;
            pin.slope = s->out.flat_slope;
            pin.pos = s->out.flat_pos;
            pin.lead_valid = s->out.flat_on;
            pin.time_hms = (int)in->time;
            pin.rth_hms = s->out.rth_open;
            pin.session_first = s->out.session_first;
            pin.color_dir = s->out.color_dir;
            pin.high = in->high;
            pin.low = in->low;
            pin.close = in->close;
            pin.price_scale = s->price_scale > 0 ? s->price_scale : 1;
            pin.price_unit = in->price_unit;
            pin.di = s->out.di;
            pin.prev_dn = flat_out.prev_dn_slope;
            pin.three_ratio = s->applied.three_ratio;
            pin.sq_hold = s->applied.sq_bars;
            pin.width_ratio = s->applied.width_ratio;
            pin.swing = s->applied.swing;
            pin.rise_hi = s->applied.rise_hi;
            pin.rise_lo = s->applied.rise_lo;
            pin.fall_hi = s->applied.fall_hi;
            pin.fall_lo = s->applied.fall_lo;
            s->pair_st.v68 = 0;
            tr_fxpair_on_bar(&s->pair_st, &pin, &pout);
            s->out.pair_dir = pout.entry_dir;
            s->out.pair_kind = pin.kind;
            s->out.pair_grade = pin.grade;
            s->out.pair_ak = pin.adj_kind;
            s->out.pair_ag = pin.adj_grade;
            s->out.pair_qty = pout.entry_qty;
            s->out.pair_suf = pout.suf;
            s->out.pair_exit = pout.exit_code;
            s->out.pair_loss = pout.exit_loss;
            s->out.pair_ticks = s->pair_st.first_ticks;
            s->out.pair_limit = pout.limit_hit;
            s->out.pair_exit_qty = pout.exit_qty;
            s->out.pair_limit_qty = pout.limit_hit_qty;
            s->zd_st.v68 = 1;
            tr_fxpair_on_bar(&s->zd_st, &pin, &pout);
            s->out.zd_dir = pout.entry_dir;
            s->out.zd_kind = pin.kind;
            s->out.zd_grade = pin.grade;
            s->out.zd_ak = pin.adj_kind;
            s->out.zd_ag = pin.adj_grade;
            s->out.zd_qty = pout.entry_qty;
            s->out.zd_suf = pout.suf;
            s->out.zd_exit = pout.exit_code;
            s->out.zd_loss = pout.exit_loss;
            s->out.zd_ticks = s->zd_st.first_ticks;
            s->out.zd_limit = pout.limit_hit;
            s->out.zd_exit_qty = pout.exit_qty;
            s->out.zd_limit_qty = pout.limit_hit_qty;
        }
        s->out.pnl_flips = pout.flips;
        s->out.pnl_danger = pout.danger;
        s->out.pnl_p26_on = pout.p26_on;
        s->out.pnl_p27_on = pout.p27_on;
    }

    s->flat_prev = flat_out;
        s->prev_flat = flat;
        s->prev_close = in->close;
        s->prev_pos = flat_out.pos;
        s->prev_slope = flat_out.slope;
        if (fv != 0) {
            s->prev_up_hi = fv->lup_high;
            s->prev_up_lo = fv->lup_low;
            s->prev_up_382 = fv->lup_382;
            s->prev_up_500 = fv->lup_500;
            s->prev_up_618 = fv->lup_618;
            s->prev_dn_hi = fv->ldn_high;
            s->prev_dn_lo = fv->ldn_low;
            s->prev_dn_382 = fv->ldn_382;
            s->prev_dn_500 = fv->ldn_500;
            s->prev_dn_618 = fv->ldn_618;
        }
        s->has_flat = 1;
}

int tr_fxos_format(char *buf, size_t cap, const tr_fxos_out_t *o) {
    int m, i;
    if (buf == 0 || cap == 0 || o == 0) {
        return -1;
    }
    m = snprintf(buf, cap,
                 "%d,%u,%d,%d,%d,%d,%d,%d,%u,%d,%d,%u,%d,%.10g,%u,%d,%d,%u,%d,%d,%d,%u,%d,%d,%d,%u,%d,"
                 "%d,%d,%u,%d,%d,%d,%u,%d,%d,%u,%d,%d,%u,%u,%d,%.10g,%.10g,%.10g,%d,%u,%d,%u,%d,%u,%d,%d,%.10g,%.10g,%.10g,%.10g,%d,%d,%.10g,%u,%d,%u,%d,%u,%d,%u,%d,%d,%d,%d,%d,%d,%.10g,%d,%.10g,%d,%d,%d,%d,%d,%.10g,%d,%.10g",
                 o->judge, o->judge_rgb, o->fut, o->prof, o->di, o->adx,
                 o->sq_on, o->sq_len, o->sq_rgb, o->sq_w,
                 o->hold, o->hold_rgb, o->ratio_on, o->ratio, o->ratio_rgb,
                 o->rel_on, o->rel_len, o->rel_rgb, o->rel_w,
                 o->cf_on, o->cf_len, o->cf_rgb, o->cf_w,
                 o->ent_on, o->ent_y, o->ent_rgb, o->ent_w,
                 o->adj_on, o->adj_y, o->adj_rgb, o->adj_w,
                 o->brk_on, o->brk_y, o->brk_rgb,
                 o->lead_on, o->lead_y, o->lead_rgb,
                 o->flat_pos, o->flat_slope, o->flat_pos_rgb, o->flat_slope_rgb, o->flat_mark,
                 o->wave_time, o->wave_price, o->wave_opp, o->wave_state, o->wave_state_rgb,
                 o->wave_sig, o->wave_sig_rgb, o->wave_sig_w,
                 o->paint_rgb, o->sig_dir, o->sig_exit,
                 o->pnl_open, o->pnl_mfe, o->pnl_mae, o->pnl_closed,
                 o->pnl_entries, o->pnl_wins, o->pnl_keep,
                 o->pnl_open_rgb, o->pnl_open_w, o->pnl_mfe_rgb, o->pnl_mfe_w,
                 o->pnl_mae_rgb, o->pnl_mae_w, o->pnl_keep_rgb, o->pnl_keep_w,
                 o->pnl_open_on, o->pnl_mfe_on, o->pnl_mae_on, o->pnl_keep_on,
                 o->pnl_p4_on, o->pnl_p4, o->pnl_p5_on, o->pnl_p5,
                 o->pnl_flips, o->pnl_danger, o->pnl_p26_on, o->pnl_p27_on,
                 o->pnl_p30_on, o->pnl_p30, o->pnl_keep2_on, o->pnl_keep2);
    if (m < 0 || (size_t)m >= cap) {
        return m;
    }
    for (i = 0; i < 4; i++) {
        const tr_fxwave_view_t *w = &o->wv[i];
        int n = snprintf(buf + m, cap - (size_t)m, ",%.10g,%.10g,%.10g,%d,%u,%d,%u,%d",
                         w->time_r, w->price_r, w->opp_r, w->state, w->state_rgb,
                         w->sig, w->sig_rgb, w->sig_w);
        if (n < 0 || (size_t)m + (size_t)n >= cap) {
            return -1;
        }
        m += n;
    }
    {
        int n = snprintf(buf + m, cap - (size_t)m, ",%d,%.10g,%u,%d,%d,%.10g,%u,%d,%.10g,%u,%d,%d,%.10g,%u,%u,%d,%.10g,%u,%d,%d,%d,%d,%d,%.10g,%d,%.10g,%d,%d,%d,%d,%d,%.10g,%d,%d,%d,%u,%d,%d,%d,%d,%d,%.10g,%.10g,%.10g,%d,%.10g,%.10g,%.10g,%d,%d,%d,%.10g,%.10g,%.10g,%d,%.10g,%.10g,%.10g,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%u,%u,%u,%d,%u,%u,%u,%u,%d,%d,%d,%d,%.10g,%d,%.10g,%d,%d,%d,%.10g,%d,%d,%d,%.10g,%d,%.10g,%d,%d,%d,%.10g,%d,%d",
                         o->ec_on, o->ec_px, o->ec_rgb, o->ec_w, o->ec_line_on, o->ec_line, o->ec_line_rgb,
                         o->rs_on, o->rs_px, o->rs_rgb, o->rs_w,
                         o->bk_on, o->bk_px, o->bk_rgb, o->os_paint,
                         o->pnl_long_on, o->pnl_long, o->pnl_long_rgb, o->sig_kind,
                         o->sig_qty, o->exit_qty,
                         o->pnl_side, o->pnl_short_on, o->pnl_short, o->pnl_exit_on, o->pnl_exit,
                         o->flat_on, o->flat_up_pos, o->flat_dn_pos, o->flat_up_slope, o->flat_dn_slope, o->flat_amp,
                         o->rth_open, o->turn_on, o->turn_y, o->turn_rgb, o->turn_w,
                         o->sig_grade, o->sig_adj_kind, o->sig_adj_grade,
                         o->pos_v1.side, o->pos_v1.open_ticks, o->pos_v1.mfe_ticks, o->pos_v1.mae_ticks,
                         o->pos_v1.exit_on, o->pos_v1.exit_ticks, o->pos_v1.closed_long, o->pos_v1.closed_short,
                         o->pos_v1.wins, o->pos_v1.losses,
                         o->pos_v2.side, o->pos_v2.open_ticks, o->pos_v2.mfe_ticks, o->pos_v2.mae_ticks,
                         o->pos_v2.exit_on, o->pos_v2.exit_ticks, o->pos_v2.closed_long, o->pos_v2.closed_short,
                         o->pos_v2.wins, o->pos_v2.losses,
                         o->pair_dir, o->pair_kind, o->pair_grade, o->pair_ak, o->pair_ag,
                         o->pair_qty, o->pair_suf, o->pair_exit, o->pair_loss, o->pair_ticks,
                         o->zd_dir, o->zd_kind, o->zd_grade, o->zd_ak, o->zd_ag,
                         o->zd_qty, o->zd_suf, o->zd_exit, o->zd_loss, o->zd_ticks,
                         o->pos_v1.open_rgb, o->pos_v1.mfe_rgb, o->pos_v1.mae_rgb, o->pos_v1.long_rgb, o->pos_v1.open_w,
                         o->pos_v2.open_rgb, o->pos_v2.mfe_rgb, o->pos_v2.mae_rgb, o->pos_v2.long_rgb, o->pos_v2.open_w,
                         o->pos_v1.mfe_w, o->pos_v1.mae_w, o->pos_v1.keep_mfe_on, o->pos_v1.keep_mfe,
                         o->pos_v1.keep_mae_on, o->pos_v1.keep_mae, o->pos_v1.flips, o->pos_v1.danger,
                         o->pos_v1.total_on, o->pos_v1.total_ticks,
                         o->pos_v2.mfe_w, o->pos_v2.mae_w, o->pos_v2.keep_mfe_on, o->pos_v2.keep_mfe,
                         o->pos_v2.keep_mae_on, o->pos_v2.keep_mae, o->pos_v2.flips, o->pos_v2.danger,
                         o->pos_v2.total_on, o->pos_v2.total_ticks,
                         o->pair_limit, o->zd_limit);
        if (n < 0 || (size_t)m + (size_t)n >= cap) {
            return -1;
        }
        m += n;
    }
    /* 기존 칸을 유지하고 청산 수량을 뒤에 추가한다 (228~231). */
    int n = snprintf(buf + m, cap - (size_t)m, ",%d,%d,%d,%d,%d,%.10g,%d,%d,%d,\"%s\",\"%s\"",
                     o->pair_exit_qty, o->pair_limit_qty, o->zd_exit_qty, o->zd_limit_qty,
                     o->swing_on, o->swing_pct, o->wr_dir, o->wr_order, o->wr_exit_dir,
                     o->wr_name, o->wr_exit_name);
    if (n < 0 || (size_t)m + (size_t)n >= cap) return -1;
    return m + n;
}

/* 매수 청산 지정가·스톱이 이 봉의 고저에 닿으면 그 이름을 돌려준다.
 * 1차청산은 진입 봉이 아니라 가격이 닿은 봉에 그린다. */
static int long_exit_touch(const tr_yl_book_t *book, double open, double high, double low,
                           char *name, size_t cap) {
    int i;
    if (book == 0) {
        return 0;
    }
    for (i = 0; i < book->queue_n; i++) {
        const tr_yl_pending_t *p = &book->queue[i];
        int hit = 0;
        if (p->action != TR_YL_EXIT_LONG) {
            continue;
        }
        if (p->price_type == TR_YL_LIMIT) {
            hit = open >= p->price || high >= p->price;
        } else if (p->price_type == TR_YL_STOP) {
            hit = open <= p->price || low <= p->price;
        }
        if (hit) {
            snprintf(name, cap, "%s", p->name);
            return 1;
        }
    }
    return 0;
}

/* 매도 청산 지정가·스톱. 닿으면 저가 쪽 화살표다. */
static int short_exit_touch(const tr_yl_book_t *book, double open, double high, double low,
                            char *name, size_t cap) {
    int i;
    if (book == 0) {
        return 0;
    }
    for (i = 0; i < book->queue_n; i++) {
        const tr_yl_pending_t *p = &book->queue[i];
        int hit = 0;
        if (p->action != TR_YL_EXIT_SHORT) {
            continue;
        }
        if (p->price_type == TR_YL_LIMIT) {
            hit = open <= p->price || low <= p->price;
        } else if (p->price_type == TR_YL_STOP) {
            hit = open >= p->price || high >= p->price;
        }
        if (hit) {
            snprintf(name, cap, "%s", p->name);
            return 1;
        }
    }
    return 0;
}

static void wr_show(tr_fxos_t *s) {
    s->out.wr_dir = s->wr_hold_dir;
    s->out.wr_order = s->wr_hold_order;
    s->out.wr_exit_dir = s->wr_hold_exit;
    snprintf(s->out.wr_name, sizeof(s->out.wr_name), "%s", s->wr_hold_name);
    snprintf(s->out.wr_exit_name, sizeof(s->out.wr_exit_name), "%s", s->wr_hold_exit_name);
}

/* 같은 이름의 스톱·지정가는 새로 쌓지 않고 가격만 바꾼다. 예스트레이더도 같은 주문을 갈아 끼운다. */
static void submit_order(tr_yl_book_t *book, const tr_yl_order_t *order) {
    int i;
    if (book == 0 || order == 0) {
        return;
    }
    if (order->price_type != TR_YL_ONCLOSE && order->name != 0) {
        for (i = 0; i < book->queue_n; i++) {
            tr_yl_pending_t *p = &book->queue[i];
            if (strcmp(p->name, order->name) == 0) {
                p->action = order->action;
                p->price_type = order->price_type;
                p->price = order->price;
                p->qty = order->qty;
                return;
            }
        }
    }
    tr_yl_book_submit(book, order);
}

static int is_first_target(const char *name) {
    return name != 0 && (strcmp(name, "1차청산매수") == 0 || strcmp(name, "1차청산매도") == 0);
}

static int is_entry_fill(const char *name) {
    return name != 0 && name[0] != '\0' && strstr(name, "청산") == 0 && strstr(name, "스톱") == 0 &&
           strstr(name, "손절") == 0;
}

static void note_fills(tr_fxos_t *s, int from) {
    int k;
    int saw_entry = 0;
    int saw_first = 0;
    if (s->wr_book == 0) {
        return;
    }
    for (k = from; k < s->wr_book->trade_n; k++) {
        const tr_yl_trade_t *t = tr_yl_book_trade(s->wr_book, k);
        if (t == 0) {
            continue;
        }
        if (is_entry_fill(t->name)) {
            saw_entry = 1;
        }
        if (is_first_target(t->name)) {
            saw_first = 1;
        }
    }
    if (s->wr_book->pos.side == 0 || saw_entry) {
        s->wr_first_filled = 0;
    }
    if (saw_first && s->wr_book->pos.side != 0) {
        s->wr_first_filled = 1;
    }
}

static void wr_commit(tr_fxos_t *s) {
    tr_yl_bar_t bar;
    int i;
    int n0;
    if (s->wr_book == 0) {
        return;
    }
    memset(&bar, 0, sizeof(bar));
    bar.time = s->wr_bar_open / 1000000;
    bar.open = s->wr_save_o;
    bar.high = s->wr_save_h;
    bar.low = s->wr_save_l;
    bar.close = s->wr_save_c;
    n0 = s->wr_book->trade_n;
    tr_yl_book_open(s->wr_book, &bar);
    note_fills(s, n0);
    for (i = 0; i < s->wr_saved_n; i++) {
        tr_yl_order_t o;
        const tr_fxwr_ord_t *src = &s->wr_saved[i];
        /* 1차는 진입당 한 번이다. 남은 계약은 수익보호 스톱·색청산이 가져간다. */
        if (s->wr_first_filled && is_first_target(src->name)) {
            continue;
        }
        memset(&o, 0, sizeof(o));
        o.action = src->action;
        o.price_type = src->price_type;
        o.price = src->price;
        o.qty = src->qty;
        o.name = src->name;
        submit_order(s->wr_book, &o);
    }
    n0 = s->wr_book->trade_n;
    tr_yl_book_close(s->wr_book);
    note_fills(s, n0);
    if (s->wr_book->pos.side == 0) {
        s->wr_first_filled = 0;
    }
    s->wr_saved_n = 0;
}

void tr_fxos_wr_reset(tr_fxos_t *s) {
    if (s == 0) {
        return;
    }
    if (s->wr != 0) {
        free(s->wr->eng);
        free(s->wr);
        s->wr = 0;
    }
    free(s->wr_book);
    s->wr_book = 0;
    s->wr_seen = 0;
    s->wr_saved_n = 0;
    s->wr_hold_dir = 0;
    s->wr_hold_order = 0;
    s->wr_hold_exit = 0;
    s->wr_hold_name[0] = '\0';
    s->wr_hold_exit_name[0] = '\0';
    s->wr_bar_fill[0] = '\0';
    s->wr_first_filled = 0;
}

/* 예스랭귀지 가격은 포인트다. 엔진 봉은 100배 raw 이므로 승률 계산에만 되돌린다.
 * 비상손절pt(25)처럼 PriceScale 을 안 곱하는 값은 포인트다. raw 로 빼면 0.25포인트가 된다. */
static void wr_as_points(const tr_fxos_input_t *in, tr_fxos_input_t *out) {
    *out = *in;
    out->open /= 100.0;
    out->high /= 100.0;
    out->low /= 100.0;
    out->close /= 100.0;
}

void tr_fxos_wr_bar(tr_fxos_t *s, const tr_fxos_input_t *raw) {
    tr_fxwr_bar_t wb;
    tr_fxos_input_t pin;
    const tr_fxos_input_t *in;
    int i;
    int same;
    if (s == 0 || raw == 0) {
        return;
    }
    wr_as_points(raw, &pin);
    in = &pin;
    same = s->wr_seen && in->bar_open == s->wr_bar_open && in->open == s->wr_save_o &&
           in->high == s->wr_save_h && in->low == s->wr_save_l && in->close == s->wr_save_c &&
           in->volume == s->last_wr_v;
    if (same) {
        wr_show(s);
        return;
    }
    if (s->wr == 0) {
        s->wr = (tr_fxwr_t *)calloc(1, sizeof(*s->wr));
    }
    if (s->wr_book == 0) {
        s->wr_book = (tr_yl_book_t *)calloc(1, sizeof(*s->wr_book));
        if (s->wr_book != 0) {
            tr_yl_book_init(s->wr_book, 1, s->price_scale / 100.0);
        }
    }
    if (s->wr == 0 || s->wr_book == 0) {
        return;
    }
    if (s->wr_seen && in->bar_open != s->wr_bar_open) {
        s->wr_bar_fill[0] = '\0';
        wr_commit(s);
    }
    /* 예스트레이더는 스크립트보다 먼저, 걸려 있던 스톱·지정가를 이 봉의 고저로 체결한다.
     * 수익보호 스톱이 먼저 체결되면 포지션이 없어져 색청산은 나가지 않는다. */
    {
        tr_yl_bar_t now;
        int n0 = s->wr_book->trade_n;
        int k;
        memset(&now, 0, sizeof(now));
        now.time = in->bar_open / 1000000;
        now.open = in->open;
        now.high = in->high;
        now.low = in->low;
        now.close = in->close;
        tr_yl_book_open(s->wr_book, &now);
        for (k = n0; k < s->wr_book->trade_n; k++) {
            const tr_yl_trade_t *t = tr_yl_book_trade(s->wr_book, k);
            if (t == 0 || t->name[0] == '\0') {
                continue;
            }
            /* 진입 체결은 화살표가 따로 그린다. 청산·스톱·손절만 청산 이름으로 남긴다. */
            if (strstr(t->name, "청산") == 0 && strstr(t->name, "스톱") == 0 &&
                strstr(t->name, "손절") == 0 && strstr(t->name, "본전") == 0) {
                continue;
            }
            snprintf(s->wr_bar_fill, sizeof(s->wr_bar_fill), "%s", t->name);
        }
        note_fills(s, n0);
    }
    memset(&wb, 0, sizeof(wb));
    wb.date = in->date;
    wb.time = in->time;
    wb.bar_open = in->bar_open;
    wb.open = in->open;
    wb.high = in->high;
    wb.low = in->low;
    wb.close = in->close;
    wb.volume = in->volume;
    wb.price_scale = s->price_scale / 100.0;
    wb.market = s->wr_book->pos.side;
    wb.is_new_bar = !s->wr_seen || in->bar_open != s->wr_bar_open;
    tr_fxwr_on_bar(s->wr, &wb);
    s->wr_hold_dir = 0;
    s->wr_hold_order = (int)s->wr->cur[YL_F_주문번호];
    s->wr_hold_exit = 0;
    s->wr_hold_name[0] = '\0';
    s->wr_hold_exit_name[0] = '\0';
    s->wr_saved_n = s->wr->ord_n;
    if (s->wr_saved_n > 24) {
        s->wr_saved_n = 24;
    }
    for (i = 0; i < s->wr_saved_n; i++) {
        const tr_fxwr_ord_t *src = &s->wr->ords[i];
        s->wr_saved[i] = *src;
        if ((src->action == TR_YL_BUY || src->action == TR_YL_SELL) && s->wr_hold_name[0] == '\0') {
            s->wr_hold_dir = src->action;
            snprintf(s->wr_hold_name, sizeof(s->wr_hold_name), "%s", src->name);
        }
        if (s->wr_bar_fill[0] == '\0' && src->price_type == TR_YL_ONCLOSE &&
            s->wr_hold_exit_name[0] == '\0' &&
            (src->action == TR_YL_EXIT_LONG || src->action == TR_YL_EXIT_SHORT)) {
            s->wr_hold_exit = src->action == TR_YL_EXIT_LONG ? 1 : -1;
            snprintf(s->wr_hold_exit_name, sizeof(s->wr_hold_exit_name), "%s", src->name);
        }
    }
    if (s->wr_bar_fill[0] != '\0') {
        s->wr_hold_exit = strstr(s->wr_bar_fill, "매도") != 0 ? -1 : 1;
        snprintf(s->wr_hold_exit_name, sizeof(s->wr_hold_exit_name), "%s", s->wr_bar_fill);
    } else if (long_exit_touch(s->wr_book, in->open, in->high, in->low, s->wr_hold_exit_name,
                               sizeof(s->wr_hold_exit_name))) {
        s->wr_hold_exit = 1;
    } else if (short_exit_touch(s->wr_book, in->open, in->high, in->low, s->wr_hold_exit_name,
                                sizeof(s->wr_hold_exit_name))) {
        s->wr_hold_exit = -1;
    }
    if (s->wr_hold_name[0] == '\0') {
        s->wr_hold_order = 0;
    }
    s->wr_seen = 1;
    s->wr_bar_open = in->bar_open;
    s->wr_save_o = in->open;
    s->wr_save_h = in->high;
    s->wr_save_l = in->low;
    s->wr_save_c = in->close;
    s->last_wr_v = in->volume;
    wr_show(s);
}
