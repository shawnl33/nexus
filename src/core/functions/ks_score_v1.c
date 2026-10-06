#include "core/functions/ks_score_v1.h"

#include <math.h>
#include <string.h>

#define RGB_(r, g, b) ((uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))

void tr_ksscore_default_config(tr_ksscore_config_t *cfg, double price_scale) {
    if (cfg == 0) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->values.tf = TR_KS_TF_1M;
    cfg->values.predict_ticks = 10;
    cfg->values.predict_bars[0] = 5;
    cfg->values.predict_bars[1] = 10;
    cfg->values.predict_bars[2] = 15;
    cfg->values.predict_bars[3] = 30;
    cfg->values.predict_bars[4] = 60;
    cfg->values.min_r2 = 0.4;
    cfg->values.persist_bars = 5;
    cfg->values.market_period = 20;
    cfg->values.min_hold_bars = 3;
    cfg->values.price_scale = price_scale;
    cfg->reg_period_5 = 18;
    cfg->reg_period_15 = 10;
    cfg->reg_period_30 = 6;
    cfg->time_basis = 0;
    cfg->include_mkt_30 = 1;
    cfg->slack_ticks = 0.0;
}

bool tr_ksscore_init(tr_ksscore_t *s, const tr_ksscore_config_t *cfg) {
    if (s == 0 || cfg == 0 || cfg->values.tf != TR_KS_TF_1M ||
        (cfg->time_basis != 0 && cfg->time_basis != 1) ||
        (cfg->include_mkt_30 != 0 && cfg->include_mkt_30 != 1)) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->cfg = *cfg;
    if (!tr_ksv_init(&s->values, &cfg->values)) {
        return false;
    }
    tr_ks_session_init(&s->session);
    const int32_t mins[3] = {5, 15, 30};
    const int32_t periods[3] = {cfg->reg_period_5, cfg->reg_period_15, cfg->reg_period_30};
    for (int i = 0; i < 3; i++) {
        tr_kssyn_config_t sc;
        memset(&sc, 0, sizeof(sc));
        sc.synth_min = mins[i];
        sc.reg_period = periods[i];
        sc.mkt_period = cfg->values.market_period;
        sc.time_basis = cfg->time_basis;
        sc.price_scale = cfg->values.price_scale;
        sc.bar_sec = 60;
        if (!tr_kssyn_init(&s->syn[i], &sc)) {
            return false;
        }
    }
    return true;
}

bool tr_ksscore_relink(tr_ksscore_t *s) {
    if (s == 0) {
        return false;
    }
    bool ok = tr_ksv_relink(&s->values);
    for (int i = 0; i < 3; i++) {
        ok = tr_kssyn_relink(&s->syn[i]) && ok;
    }
    return ok;
}

/* 삼선 145~206, 오선 224~285, 평탄 312~346, 마켓 371~407.
 * ready와 선 개수만 다르다. 색 기록은 삼선·오선만 갱신한다. */
static void lane_eval(tr_ksscore_lane_t *cur, const tr_ksscore_lane_t *prev, int session_reset,
                      int current_bar, int ready, const double *px, int n, double low, double close,
                      int color) {
    memset(cur, 0, sizeof(*cur));
    if (session_reset) {
        cur->prev_peak = current_bar > 1 ? prev->peak : 0.0;
        cur->range_low = low;
    } else {
        cur->peak = prev->peak;
        cur->prev_peak = prev->prev_peak;
        cur->range_low = fmin(prev->range_low, low);
    }
    cur->ready = ready ? 1 : 0;
    if (!ready) {
        if (color) {
            cur->below = prev->below;
            cur->above = prev->above;
        }
        return;
    }
    double hi = px[0];
    double lo = px[0];
    for (int i = 1; i < n; i++) {
        hi = fmax(hi, px[i]);
        lo = fmin(lo, px[i]);
    }
    cur->hi = hi;
    cur->lo = lo;
    cur->gap = hi - lo;
    cur->peak = fmax(cur->peak, cur->gap);
    if (cur->peak > 0.0) {
        cur->ratio = cur->gap / cur->peak * 100.0;
    }
    if (session_reset || prev->ready == 0 || cur->gap != prev->gap) {
        cur->cnt = 1.0;
    } else {
        cur->cnt = prev->cnt + 1.0;
    }
    if (!color) {
        return;
    }
    if (session_reset || prev->ready == 0 || cur->ratio != prev->ratio) {
        cur->below = 0;
        cur->above = 0;
    } else {
        cur->below = prev->below;
        cur->above = prev->above;
    }
    if (close < cur->hi) {
        cur->below = 1;
    }
    if (close > cur->lo) {
        cur->above = 1;
    }
    cur->rgb = RGB_(128, 128, 128);
    if (cur->below == 0) {
        cur->rgb = RGB_(255, 0, 0);
    } else if (cur->above == 0) {
        cur->rgb = RGB_(0, 0, 255);
    }
}

/* 삼선 연동굵기 426~447. 25 미만이면서 봉 전체가 목표 밖이면 두께 3이 우선한다. */
static void three_paint(tr_ksscore_lane_t *three, const tr_ksscore_lane_t *reg,
                        const tr_ksscore_lane_t *mkt, double high, double low) {
    three->width = 0;
    if (!three->ready) {
        three->rgb = 0;
        return;
    }
    if (three->ratio < 30.0) {
        three->width = 2;
    } else if ((three->below == 0 || three->above == 0) && reg->ready && mkt->ready &&
               reg->ratio < 30.0 && mkt->ratio < 30.0) {
        three->width = 1;
    }
    if (three->ratio < 25.0) {
        if (low > three->hi) {
            three->rgb = RGB_(180, 0, 0);
            three->width = 3;
        } else if (high < three->lo) {
            three->rgb = RGB_(0, 0, 150);
            three->width = 3;
        }
    }
}

/* 이탈 456~555. 폭이 바뀐 봉은 새 구간에만 넣고 직전 구간은 직전 봉까지 고정한다. */
static void break_eval(tr_ksscore_brk_t *cur, double *tgt_hi, double *tgt_lo, double *cur_range,
                       double *prev_range, double *two_hi, double *two_lo, double *two_range,
                       double *price_ratio, int *changed, int *pos, const tr_ksscore_brk_t *prev,
                       int session_reset, int ready, double t0, double t1, double t2, double high,
                       double low, double close, double slack) {
    memset(cur, 0, sizeof(*cur));
    *tgt_hi = 0.0;
    *tgt_lo = 0.0;
    *changed = 0;
    *pos = 0;
    cur->ready = ready ? 1 : 0;
    if (!session_reset && ready) {
        cur->cnt = prev->cnt;
        cur->prev_cnt = prev->prev_cnt;
        cur->cur_hi = prev->cur_hi;
        cur->cur_lo = prev->cur_lo;
        cur->prev_hi = prev->prev_hi;
        cur->prev_lo = prev->prev_lo;
        cur->prev_valid = prev->prev_valid;
        cur->broke = prev->broke;
    }
    if (ready) {
        *tgt_hi = fmax(t0, fmax(t1, t2));
        *tgt_lo = fmin(t0, fmin(t1, t2));
        cur->width = *tgt_hi - *tgt_lo;
        if (session_reset || prev->ready == 0) {
            cur->cnt = 1.0;
            cur->prev_cnt = 0.0;
            cur->cur_hi = high;
            cur->cur_lo = low;
            cur->prev_hi = 0.0;
            cur->prev_lo = 0.0;
            cur->prev_valid = 0;
            cur->broke = 0;
        } else if (cur->width != prev->width) {
            *changed = 1;
            cur->prev_cnt = prev->cnt;
            cur->prev_hi = prev->cur_hi;
            cur->prev_lo = prev->cur_lo;
            cur->prev_valid = 1;
            cur->cnt = 1.0;
            cur->cur_hi = high;
            cur->cur_lo = low;
            cur->broke = 0;
        } else {
            cur->cnt = prev->cnt + 1.0;
            cur->cur_hi = fmax(prev->cur_hi, high);
            cur->cur_lo = fmin(prev->cur_lo, low);
        }
        if (cur->prev_valid) {
            if (close > cur->prev_hi + slack) {
                *pos = 1;
            } else if (close < cur->prev_lo - slack) {
                *pos = -1;
            }
            if (cur->broke == 0 && *pos != 0) {
                cur->first_break = *pos;
                cur->broke = 1;
            }
        }
    }
    *cur_range = 0.0;
    *prev_range = 0.0;
    *two_hi = 0.0;
    *two_lo = 0.0;
    *two_range = 0.0;
    if (ready) {
        *cur_range = cur->cur_hi - cur->cur_lo;
        *two_hi = cur->cur_hi;
        *two_lo = cur->cur_lo;
        if (cur->prev_valid) {
            *prev_range = cur->prev_hi - cur->prev_lo;
            *two_hi = fmax(cur->cur_hi, cur->prev_hi);
            *two_lo = fmin(cur->cur_lo, cur->prev_lo);
        }
        *two_range = *two_hi - *two_lo;
    }
    cur->two_max = session_reset ? 0.0 : prev->two_max;
    *price_ratio = 0.0;
    if (ready) {
        cur->two_max = fmax(cur->two_max, *two_range);
        if (cur->two_max > 0.0) {
            *price_ratio = *two_range / cur->two_max * 100.0;
        }
    }
}

static uint32_t score_rgb(int score_ex, int compound_show, int ratio_score) {
    uint32_t c = RGB_(220, 220, 220);
    if (score_ex == 1) {
        c = RGB_(128, 160, 255);
    } else if (score_ex == 2) {
        c = RGB_(0, 0, 255);
    } else if (score_ex == 3) {
        c = RGB_(0, 0, 150);
    } else if (score_ex == -1) {
        c = RGB_(255, 160, 160);
    } else if (score_ex == -2) {
        c = RGB_(255, 0, 0);
    } else if (score_ex == -3) {
        c = RGB_(180, 0, 0);
    } else if (compound_show == 1) {
        c = RGB_(0, 128, 0);
    }
    if (c == RGB_(220, 220, 220) && ratio_score > 0) {
        c = RGB_(160, 220, 160);
    }
    return c;
}

static void copy_lane(double *gap, double *peak, double *prev_peak, double *ratio, double *cnt,
                      double *hi, double *lo, double *range_low, const tr_ksscore_lane_t *lane) {
    *gap = lane->gap;
    *peak = lane->peak;
    *prev_peak = lane->prev_peak;
    *ratio = lane->ratio;
    *cnt = lane->cnt;
    *hi = lane->hi;
    *lo = lane->lo;
    *range_low = lane->range_low;
}

void tr_ksscore_eval(tr_ksscore_t *s, const tr_ksscore_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    bool is_new = !s->has_bar || in->bar_open != s->last_open;
    if (is_new) {
        s->snap = s->series;
        s->last_open = in->bar_open;
        s->has_bar = true;
    }
    const tr_ksscore_series_t *prev = &s->snap;

    tr_ksv_input_t kin;
    memset(&kin, 0, sizeof(kin));
    kin.bdate = in->bdate;
    kin.day_index = in->day_index;
    kin.current_bar = in->current_bar;
    kin.bar_open = in->bar_open;
    kin.high = in->high;
    kin.low = in->low;
    kin.close = in->close;
    kin.volume = in->volume;
    tr_ksv_eval(&s->values, &kin);

    int64_t key = tr_ks_session_eval(&s->session, is_new, in->current_bar, in->bdate, in->day_index);
    int session_reset = in->current_bar == 1 || key != prev->session_key;

    for (int i = 0; i < 3; i++) {
        tr_kssyn_input_t sin;
        memset(&sin, 0, sizeof(sin));
        sin.bdate = in->bdate;
        sin.day_index = in->day_index;
        sin.current_bar = in->current_bar;
        sin.cur_time = in->cur_time;
        sin.is_new_bar = is_new;
        sin.has_prev = s->has_prev_bar;
        sin.prev_time = s->prev_time;
        sin.prev_h = s->prev_h;
        sin.prev_l = s->prev_l;
        sin.prev_c = s->prev_c;
        sin.prev_v = s->prev_v;
        tr_kssyn_eval(&s->syn[i], &sin);
    }

    const double *t = s->values.out.persist_target;
    int three_on = t[0] != 0.0 && t[1] != 0.0 && t[2] != 0.0;
    int five_on = three_on && t[3] != 0.0 && t[4] != 0.0;
    tr_ksscore_lane_t three, five, reg, mkt;
    lane_eval(&three, &prev->three, session_reset, in->current_bar, three_on, t, 3, in->low,
              in->close, 1);
    lane_eval(&five, &prev->five, session_reset, in->current_bar, five_on, t, 5, in->low, in->close,
              1);

    double reg_px[4] = {s->values.out.reg_flat, s->syn[0].out_reg, s->syn[1].out_reg,
                        s->syn[2].out_reg};
    int reg_on = s->values.out.reg_flat != 0.0 && s->syn[0].out_reg_valid && s->syn[1].out_reg_valid &&
                 s->syn[2].out_reg_valid;
    lane_eval(&reg, &prev->reg, session_reset, in->current_bar, reg_on, reg_px, 4, in->low, in->close,
              0);

    double mkt_px[4] = {s->values.out.market_center, s->syn[0].out_mkt, s->syn[1].out_mkt,
                        s->syn[2].out_mkt};
    int mkt_n = 3;
    int mkt_on = s->values.out.market_center != 0.0 && s->syn[0].out_mkt_valid && s->syn[1].out_mkt_valid;
    if (s->cfg.include_mkt_30) {
        mkt_n = 4;
        mkt_on = mkt_on && s->syn[2].out_mkt_valid;
    }
    lane_eval(&mkt, &prev->mkt, session_reset, in->current_bar, mkt_on, mkt_px, mkt_n, in->low,
              in->close, 0);
    three_paint(&three, &reg, &mkt, in->high, in->low);

    double slack = fmax(0.0, s->cfg.slack_ticks) * s->cfg.values.price_scale;
    tr_ksscore_brk_t brk;
    double tgt_hi, tgt_lo, cur_range, prev_range, two_hi, two_lo, two_range, price_ratio;
    int changed = 0, pos = 0;
    break_eval(&brk, &tgt_hi, &tgt_lo, &cur_range, &prev_range, &two_hi, &two_lo, &two_range,
               &price_ratio, &changed, &pos, &prev->brk, session_reset, three_on, t[0], t[1], t[2],
               in->high, in->low, in->close, slack);

    /* 572~677. 복합·가격이탈·첫 이탈의 [1]은 직전 봉이다. 80 초과면 비율점을 0으로 두고,
     * 가격비율 40 미만의 1점은 다시 보장한다. */
    int compound = 0;
    int px_cond = 0;
    if (three_on && brk.two_max > 0.0 && price_ratio < 40.0) {
        if (three.ready && reg.ready && mkt.ready && three.ratio <= 30.0 && reg.ratio < 30.0 &&
            mkt.ratio < 30.0) {
            compound = 1;
        }
        if (brk.prev_valid) {
            px_cond = pos;
        }
    }
    brk.compound = compound;
    brk.px_cond = px_cond;

    int up = 0, dn = 0;
    if (three_on && brk.prev_valid) {
        if (pos == -1) {
            up = 1;
        } else if (pos == 1) {
            dn = 1;
        }
    }
    if (!session_reset && three_on) {
        if (prev->brk.first_break == 1) {
            dn = 1;
        }
        if (prev->brk.first_break == -1) {
            up = 1;
        }
    }
    if (!session_reset) {
        if (prev->brk.compound == 1 || prev->brk.px_cond == 1) {
            dn = 1;
        }
        if (prev->brk.px_cond == -1) {
            up = 1;
        }
    }
    if (three.ready && three.ratio >= 30.0) {
        if (three.below == 0) {
            dn = 1;
        } else if (three.above == 0) {
            up = 1;
        }
    }
    uint32_t dot = RGB_(255, 0, 0);
    if (up) {
        dot = RGB_(0, 0, 255);
    }
    if (up && dn) {
        dot = RGB_(0, 128, 0);
    }

    int score = 0;
    int show = 0;
    if (three_on && brk.prev_valid) {
        if (pos == -1) {
            score++;
        } else if (pos == 1) {
            score--;
        }
    }
    if (!session_reset && three_on) {
        if (prev->brk.first_break == 1) {
            score--;
        }
        if (prev->brk.first_break == -1) {
            score++;
        }
    }
    if (!session_reset) {
        if (prev->brk.px_cond == 1) {
            score--;
        }
        if (prev->brk.px_cond == -1) {
            score++;
        }
        if (prev->brk.compound == 1) {
            show = 1;
        }
    }
    int score_ex = score;
    if (three.ready && three.ratio >= 30.0) {
        if (three.below == 0) {
            score--;
        } else if (three.above == 0) {
            score++;
        }
    }
    int ratio_score = 0;
    int price_pts = three_on && brk.two_max > 0.0 && price_ratio < 40.0;
    if (price_pts) {
        ratio_score++;
    }
    if (reg.ready && reg.ratio < 30.0) {
        ratio_score++;
    }
    if (mkt.ready && mkt.ratio < 30.0) {
        ratio_score++;
    }
    if ((three_on && brk.two_max > 0.0 && price_ratio > 80.0) || (reg.ready && reg.ratio > 80.0) ||
        (mkt.ready && mkt.ratio > 80.0)) {
        ratio_score = 0;
    }
    if (price_pts) {
        ratio_score = ratio_score < 1 ? 1 : ratio_score;
    }

    s->series.session_key = key;
    s->series.three = three;
    s->series.five = five;
    s->series.reg = reg;
    s->series.mkt = mkt;
    s->series.brk = brk;
    s->prev_time = in->cur_time;
    s->prev_h = in->high;
    s->prev_l = in->low;
    s->prev_c = in->close;
    s->prev_v = in->volume;
    s->has_prev_bar = true;

    memset(&s->out, 0, sizeof(s->out));
    s->out.calc_ready = 1;
    s->out.session_key = key;
    s->out.session_reset = session_reset;
    s->out.stage_score = s->values.out.score;
    s->out.reg_flat = s->values.out.reg_flat;
    s->out.market_center = s->values.out.market_center;
    memcpy(s->out.target, t, sizeof(s->out.target));
    s->out.lup_high = s->values.out.lup_high;
    s->out.lup_low = s->values.out.lup_low;
    s->out.lup_382 = s->values.out.lup_382;
    s->out.lup_500 = s->values.out.lup_500;
    s->out.lup_618 = s->values.out.lup_618;
    s->out.ldn_high = s->values.out.ldn_high;
    s->out.ldn_low = s->values.out.ldn_low;
    s->out.ldn_382 = s->values.out.ldn_382;
    s->out.ldn_500 = s->values.out.ldn_500;
    s->out.ldn_618 = s->values.out.ldn_618;

    s->out.three_ready = three.ready;
    copy_lane(&s->out.three_gap, &s->out.three_peak, &s->out.three_prev_peak, &s->out.three_ratio,
              &s->out.three_cnt, &s->out.three_hi, &s->out.three_lo, &s->out.three_range_low, &three);
    s->out.three_below = three.below;
    s->out.three_above = three.above;
    s->out.three_rgb = three.rgb;
    s->out.three_width = three.width;

    s->out.five_ready = five.ready;
    copy_lane(&s->out.five_gap, &s->out.five_peak, &s->out.five_prev_peak, &s->out.five_ratio,
              &s->out.five_cnt, &s->out.five_hi, &s->out.five_lo, &s->out.five_range_low, &five);
    s->out.five_below = five.below;
    s->out.five_above = five.above;
    s->out.five_rgb = five.rgb;

    for (int i = 0; i < 3; i++) {
        s->out.reg_px[i] = s->syn[i].out_reg;
        s->out.mkt_px[i] = s->syn[i].out_mkt;
        s->out.reg_valid[i] = s->syn[i].out_reg_valid;
        s->out.mkt_valid[i] = s->syn[i].out_mkt_valid;
    }
    s->out.reg_ready = reg.ready;
    copy_lane(&s->out.reg_gap, &s->out.reg_peak, &s->out.reg_prev_peak, &s->out.reg_ratio,
              &s->out.reg_cnt, &s->out.reg_hi, &s->out.reg_lo, &s->out.reg_range_low, &reg);
    s->out.mkt_ready = mkt.ready;
    copy_lane(&s->out.mkt_gap, &s->out.mkt_peak, &s->out.mkt_prev_peak, &s->out.mkt_ratio,
              &s->out.mkt_cnt, &s->out.mkt_hi, &s->out.mkt_lo, &s->out.mkt_range_low, &mkt);

    s->out.break_ready = brk.ready;
    s->out.tgt_hi = tgt_hi;
    s->out.tgt_lo = tgt_lo;
    s->out.tgt_width = brk.width;
    s->out.seg_cnt = brk.cnt;
    s->out.prev_seg_cnt = brk.prev_cnt;
    s->out.compare_bars = brk.cnt + brk.prev_cnt;
    s->out.cur_hi = brk.cur_hi;
    s->out.cur_lo = brk.cur_lo;
    s->out.cur_range = cur_range;
    s->out.prev_hi = brk.prev_hi;
    s->out.prev_lo = brk.prev_lo;
    s->out.prev_range = prev_range;
    s->out.prev_valid = brk.prev_valid;
    s->out.two_hi = two_hi;
    s->out.two_lo = two_lo;
    s->out.two_range = two_range;
    s->out.two_max = brk.two_max;
    s->out.price_ratio = price_ratio;
    s->out.seg_changed = changed;
    s->out.broke = brk.broke;
    s->out.pos = pos;
    s->out.first_break = brk.first_break;
    s->out.slack = slack;
    s->out.compound = compound;
    s->out.px_cond = px_cond;
    s->out.up_dot = up;
    s->out.dn_dot = dn;
    s->out.dot_rgb = dot;
    s->out.score = score;
    s->out.score_ex = score_ex;
    s->out.compound_show = show;
    s->out.ratio_score = ratio_score;
    s->out.score_rgb = score_rgb(score_ex, show, ratio_score);
}
