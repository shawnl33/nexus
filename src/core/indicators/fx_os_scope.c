#include "core/indicators/fx_os_scope.h"

#include "core/functions/fx_decision_v2.h"
#include "core/functions/fx_session_key_v1.h"

#include <string.h>

#define RGB_(r, g, b) ((uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))

bool tr_fxos_init(tr_fxos_t *s, double price_scale) {
    if (s == 0 || price_scale <= 0.0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->price_scale = price_scale;
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
        if (grade == 7) return RGB_(255, 120, 0);
        if (grade == 6) return RGB_(230, 70, 0);
        if (grade == 5) return RGB_(170, 0, 0);
        if (grade == 4) return RGB_(200, 0, 200);
        if (grade == 3) return RGB_(220, 0, 0);
        if (grade == 2) return RGB_(255, 170, 170);
        return RGB_(255, 215, 215);
    }
    if (grade == 7) return RGB_(0, 120, 160);
    if (grade == 6) return RGB_(0, 90, 220);
    if (grade == 5) return RGB_(0, 0, 140);
    if (grade == 4) return RGB_(0, 150, 150);
    if (grade == 3) return RGB_(0, 0, 200);
    if (grade == 2) return RGB_(150, 175, 255);
    return RGB_(205, 215, 255);
}

static int entry_size(int grade) {
    if (grade == 7 || grade == 6 || grade == 3) return 7;
    if (grade == 5) return 9;
    if (grade == 4) return 6;
    if (grade == 2) return 3;
    return 2;
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

void tr_fxos_eval(tr_fxos_t *s, const tr_fxos_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    memset(&s->out, 0, sizeof(s->out));
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
    cin.ticks = 10;
    cin.is_new_bar = in->is_new_bar;
    tr_fxc_eval(&s->curve, &cin);

    tr_fxadx_input_t ain;
    memset(&ain, 0, sizeof(ain));
    ain.session_reset = reset;
    ain.high = in->high;
    ain.low = in->low;
    ain.close = in->close;
    ain.bar_open = in->bar_open;
    tr_fxadx_eval(&s->adx, &ain);

    tr_fxprof_input_t pin;
    memset(&pin, 0, sizeof(pin));
    pin.session_reset = reset;
    pin.bar_open = in->bar_open;
    pin.high = in->high;
    pin.low = in->low;
    pin.close = in->close;
    pin.volume = in->volume;
    pin.price_scale = s->price_scale;
    pin.value_mult = 1.5;
    pin.min_bars = 5;
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
    qin.value_mult = 1.5;
    qin.min_bars = 5;
    qin.width_bars = 30;
    qin.ratio_bars = 120;
    qin.narrow_pct = 40;
    qin.stage1_bars = 15;
    qin.confirm_bars = 5;
    qin.confirm_closed = 0;
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
    din.mode = 1;
    din.adx = s->adx.adx;
    din.plus_di = s->adx.plus_di;
    din.minus_di = s->adx.minus_di;
    din.adx_valid = s->adx.valid;
    din.adx_trend = 20;
    din.adx_strong = 35;
    tr_fxdec_out_t dec = tr_fxdec_eval(&din);
    int dir = dec.unified > 0 ? 1 : (dec.unified < 0 ? -1 : 0);
    uint32_t jrgb = judge_rgb(dec.unified);
    if (dir != 0 && (fut != dir || dec.di != dir || s->session.out.state5 * dir < 0)) {
        jrgb = RGB_(150, 150, 150);
    }

    int pos = 0;
    if (s->squeeze.out.profile_valid && s->squeeze.out.band_hi > s->squeeze.out.band_lo) {
        double t = (in->close - s->squeeze.out.band_lo) /
                   (s->squeeze.out.band_hi - s->squeeze.out.band_lo);
        if (t >= 2.0 / 3.0) {
            pos = 1;
        } else if (t <= 1.0 / 3.0) {
            pos = -1;
        }
    }
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
    ein.fv = in->fv;
    tr_fxec_eval(&s->entry, &ein);
    if (s->entry.out.dir != 0) {
        s->out.ent_on = 1;
        s->out.ent_y = s->entry.out.dir > 0 ? -110 : 110;
        s->out.ent_rgb = entry_rgb(s->entry.out.dir, s->entry.out.grade);
        s->out.ent_w = entry_size(s->entry.out.grade);
    }
}
