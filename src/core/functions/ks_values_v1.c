#include "core/functions/ks_values_v1.h"

#include <math.h>
#include <string.h>

static double round_to_tick(double v, double ps) {
    return floor(v / ps + 0.5) * ps;
}

bool tr_ksv_init(tr_ksv_t *s, const tr_ksv_config_t *cfg) {
    if (s == 0 || cfg == 0 || cfg->market_period < 1 || cfg->price_scale <= 0.0 ||
        (cfg->tf != TR_KS_TF_1M && cfg->tf != TR_KS_TF_15S)) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->cfg = *cfg;
    if (!tr_ring_init(&s->bar_win, s->bar_win_buf, sizeof(tr_ksv_bar_t),
                      (size_t)cfg->market_period)) {
        return false;
    }
    /* 1분과 15초 모두 회귀기간 30 (KSReg 원본 34~44줄). */
    tr_compress_t compress = cfg->tf == TR_KS_TF_15S ? TR_COMPRESS_SEC : TR_COMPRESS_MIN;
    uint32_t interval = cfg->tf == TR_KS_TF_15S ? 15u : 1u;
    tr_fxreg_init(&s->reg_pred, compress, interval);
    tr_fxreg_init(&s->reg_core, compress, interval);
    tr_fxp2_init(&s->predict);
    tr_fxc_init(&s->curve);
    tr_fxsw_init(&s->swing);
    tr_ks_session_init(&s->session);
    return true;
}

void tr_ksv_eval(tr_ksv_t *s, const tr_ksv_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    const double ps = s->cfg.price_scale;
    bool is_new_bar = !s->has_bar || in->bar_open != s->last_bar_open;
    int64_t key = tr_ks_session_eval(&s->session, is_new_bar, in->current_bar, in->bdate,
                                     in->day_index);

    memset(&s->out, 0, sizeof(s->out));

    if (is_new_bar) {
        s->prev_bar = s->series;
    } else {
        s->series = s->prev_bar;
    }
    s->is_new_bar = is_new_bar;
    if (is_new_bar) {
        s->last_bar_open = in->bar_open;
        s->has_bar = true;
    }
    /* 원본 152~157줄. 같은 봉 재평가도 같은 리셋·봉수. */
    bool reset = !s->has_bar || key != s->series.key;
    if (is_new_bar) {
        s->session_reset = reset;
        s->session_bars = reset ? 1 : s->series.session_bars + 1;
    }
    reset = s->session_reset;
    int32_t bars = s->session_bars;

    if (is_new_bar) {
        tr_ksv_bar_t b = {in->high, in->low, in->close, in->volume};
        tr_ring_push(&s->bar_win, &b);
    } else {
        tr_ksv_bar_t *b0 = (tr_ksv_bar_t *)tr_ring_get_mut(&s->bar_win, 0);
        if (b0 != 0) {
            b0->h = in->high;
            b0->l = in->low;
            b0->c = in->close;
            b0->v = in->volume;
        }
    }

    tr_fxreg_input_t rin;
    memset(&rin, 0, sizeof(rin));
    rin.session_reset = reset;
    rin.session_bars = bars;
    rin.price = (in->high + in->low) / 2.0;
    rin.bar_open = in->bar_open;
    tr_fxreg_eval(&s->reg_pred, &rin);

    tr_fxp2_input_t pin;
    memset(&pin, 0, sizeof(pin));
    pin.cur_line = s->reg_pred.line;
    pin.slope = s->reg_pred.slope;
    pin.reg_valid = s->reg_pred.reg_valid;
    pin.r2 = s->reg_pred.r2;
    pin.high = in->high;
    pin.low = in->low;
    pin.close = in->close;
    pin.session_reset = reset;
    pin.session_bars = bars;
    pin.is_new_bar = is_new_bar;
    for (int k = 0; k < 5; k++) {
        pin.horizons[k] = s->cfg.predict_bars[k];
    }
    tr_fxp2_eval(&s->predict, &pin);

    s->state_reg_flat = round_to_tick(s->reg_pred.line, ps);

    if (reset) {
        s->series.persist_streak = 0.0;
        s->series.persist_valid = 0;
        s->series.persist_dir = 0;
        memset(s->series.persist_target, 0, sizeof(s->series.persist_target));
    } else {
        int dir2 = s->predict.pred_dir[1];
        if (dir2 == s->series.prev_pred_dir2 && dir2 != 0) {
            s->series.persist_streak += 1.0;
        } else {
            s->series.persist_streak = 1.0;
        }
    }
    if (s->reg_pred.reg_valid && s->reg_pred.r2 >= s->cfg.min_r2 &&
        s->predict.pred_dir[1] != 0 &&
        s->series.persist_streak == fmax(1.0, (double)s->cfg.persist_bars)) {
        s->series.persist_valid = 1;
        s->series.persist_dir = s->predict.pred_dir[1];
        for (int k = 0; k < 5; k++) {
            s->series.persist_target[k] = round_to_tick(s->predict.pred_price[k], ps);
        }
    }

    s->state_mkt_center = 0.0;
    s->mkt_valid = false;
    if (reset) {
        s->state_mkt_center = 0.0;
    }
    int32_t calc_bars = bars < s->cfg.market_period ? bars : s->cfg.market_period;
    double vol_sum = 0.0, pv_sum = 0.0;
    for (int32_t j = 0; j < calc_bars; j++) {
        tr_ksv_bar_t b;
        if (!tr_ring_at(&s->bar_win, (size_t)j, &b)) {
            break;
        }
        double tp = (b.h + b.l + b.c) / 3.0;
        if (b.v > 0.0) {
            vol_sum += b.v;
            pv_sum += tp * b.v;
        }
    }
    if (vol_sum > 0.0 && calc_bars >= 2) {
        s->state_mkt_center = pv_sum / vol_sum;
        s->mkt_valid = true;
    }

    tr_fxc_input_t cin;
    memset(&cin, 0, sizeof(cin));
    cin.session_reset = reset;
    cin.session_bars = bars;
    cin.high = in->high;
    cin.low = in->low;
    cin.ticks = s->cfg.predict_ticks;
    cin.is_new_bar = is_new_bar;
    tr_fxc_eval(&s->curve, &cin);

    double cur_future = reset ? 0.0 : s->curve.direction;

    tr_fxreg_eval(&s->reg_core, &rin);

    double core_flat = round_to_tick(s->reg_core.line, ps);
    int reg_dir = 0;
    if (s->reg_core.reg_valid && s->reg_core.r2 >= s->cfg.min_r2) {
        if (in->close > core_flat) {
            reg_dir = 1;
        }
        if (in->close < core_flat) {
            reg_dir = -1;
        }
    }

    int32_t mperiod = s->cfg.market_period > 1 ? s->cfg.market_period : 1;
    int32_t mcalc = bars < mperiod ? bars : mperiod;
    double mid_sum = 0.0;
    for (int32_t j = 0; j < mcalc; j++) {
        tr_ksv_bar_t b;
        if (!tr_ring_at(&s->bar_win, (size_t)j, &b)) {
            break;
        }
        mid_sum += (b.h + b.l) / 2.0;
    }
    double core_mkt = mid_sum / (double)mcalc;
    double mkt_slope = 0.0;
    if (bars > 1) {
        mkt_slope = core_mkt - s->series.prev_core_mkt;
    }
    int mkt_dir = 0;
    if (in->close > core_mkt && mkt_slope > 0.0) {
        mkt_dir = 1;
    }
    if (in->close < core_mkt && mkt_slope < 0.0) {
        mkt_dir = -1;
    }

    double score = (cur_future > 0.0 ? 2.0 : (cur_future < 0.0 ? -2.0 : 0.0)) +
                   (cur_future > s->series.prev_future_dir ? 1.0 : 0.0) +
                   (cur_future < s->series.prev_future_dir ? -1.0 : 0.0) +
                   (double)mkt_dir + (double)reg_dir;
    if (reset) {
        score = 0.0;
    }

    tr_fxsw_input_t win;
    memset(&win, 0, sizeof(win));
    win.session_reset = reset;
    win.session_bars = bars;
    win.future_dir = cur_future;
    win.min_hold_bars = s->cfg.min_hold_bars;
    win.high = in->high;
    win.low = in->low;
    win.is_new_bar = is_new_bar;
    tr_fxsw_eval(&s->swing, &win);

    s->out.score = score;
    if (s->reg_pred.reg_valid) {
        s->out.reg_flat = s->state_reg_flat;
    }
    if (s->mkt_valid) {
        s->out.market_center = s->state_mkt_center;
    }
    if (s->series.persist_valid) {
        for (int k = 0; k < 5; k++) {
            s->out.persist_target[k] = s->series.persist_target[k];
        }
    }
    s->out.lup_high = s->swing.out_lup.high;
    s->out.lup_low = s->swing.out_lup.low;
    s->out.lup_382 = s->swing.out_lup.lvl_382;
    s->out.lup_500 = s->swing.out_lup.lvl_500;
    s->out.lup_618 = s->swing.out_lup.lvl_618;
    s->out.ldn_high = s->swing.out_ldn.high;
    s->out.ldn_low = s->swing.out_ldn.low;
    s->out.ldn_382 = s->swing.out_ldn.lvl_382;
    s->out.ldn_500 = s->swing.out_ldn.lvl_500;
    s->out.ldn_618 = s->swing.out_ldn.lvl_618;

    s->series.key = key;
    s->series.session_bars = bars;
    s->series.prev_pred_dir2 = s->predict.pred_dir[1];
    s->series.prev_future_dir = cur_future;
    s->series.prev_core_mkt = core_mkt;
}

bool tr_ksv_relink(tr_ksv_t *s) {
    if (s == 0) {
        return false;
    }
    bool ok = tr_fxreg_relink(&s->reg_pred);
    ok = tr_fxreg_relink(&s->reg_core) && ok;
    ok = tr_fxp2_relink(&s->predict) && ok;
    ok = tr_fxc_relink(&s->curve) && ok;
    s->bar_win.storage = s->bar_win_buf;
    return ok;
}
