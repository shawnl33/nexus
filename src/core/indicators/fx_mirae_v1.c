#include "core/indicators/fx_mirae_v1.h"

#include <string.h>

#define RGB_(r, g, b) ((uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))
#define RGB_ORANGE RGB_(255, 127, 0) /* 원본 Orange 상수 대응 */
#define RGB_GREEN RGB_(0, 128, 0)    /* 원본 Green 상수 대응 */

void tr_fxmirae_default_config(tr_fxmirae_config_t *cfg, double price_scale) {
    if (cfg == 0) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->fv.predict_ticks = 10;                    /* 예측변수 */
    cfg->fv.predict_bars[0] = 5;                   /* 예측봉수1~5 */
    cfg->fv.predict_bars[1] = 10;
    cfg->fv.predict_bars[2] = 15;
    cfg->fv.predict_bars[3] = 30;
    cfg->fv.predict_bars[4] = 60;
    cfg->fv.min_r2 = 0.4;                          /* 최소신뢰도 */
    cfg->fv.persist_bars = 5;                      /* 지속봉수 */
    cfg->fv.market_period = 20;                    /* 마켓계산기간 */
    cfg->fv.min_hold_bars = 3;                     /* 최소전환유지봉수 */
    cfg->fv.price_scale = price_scale;
    cfg->synth_time_basis = 0;                     /* 합성봉시각기준 */
    cfg->reg_period_5m = 18;                       /* 회귀기간5분 */
    cfg->reg_period_15m = 10;                      /* 회귀기간15분 */
    cfg->reg_period_30m = 6;                       /* 회귀기간30분 */
}

/* 단계색상 (원본 65~71줄) */
static uint32_t stage_color(double score) {
    if (score >= 4.0) {
        return RGB_(220, 0, 0);
    }
    if (score >= 2.0) {
        return RGB_(255, 100, 70);
    }
    if (score == 1.0) {
        return RGB_(255, 185, 185);
    }
    if (score <= -4.0) {
        return RGB_(0, 0, 180);
    }
    if (score <= -2.0) {
        return RGB_(60, 130, 255);
    }
    if (score == -1.0) {
        return RGB_(180, 210, 255);
    }
    return RGB_(150, 150, 150);
}

bool tr_fxmirae_init(tr_fxmirae_t *s, const tr_fxmirae_config_t *cfg) {
    if (s == 0 || cfg == 0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    if (!tr_fxfv_init(&s->fv, &cfg->fv)) {
        return false;
    }
    tr_fxsyn_config_t sc;
    memset(&sc, 0, sizeof(sc));
    sc.synth_min = 5;
    sc.reg_period = cfg->reg_period_5m;
    sc.mkt_period = cfg->fv.market_period;
    sc.time_basis = cfg->synth_time_basis;
    sc.price_scale = cfg->fv.price_scale;
    if (!tr_fxsyn_init(&s->syn5, &sc)) {
        return false;
    }
    sc.synth_min = 15;
    sc.reg_period = cfg->reg_period_15m;
    if (!tr_fxsyn_init(&s->syn15, &sc)) {
        return false;
    }
    sc.synth_min = 30;
    sc.reg_period = cfg->reg_period_30m;
    if (!tr_fxsyn_init(&s->syn30, &sc)) {
        return false;
    }
    return true;
}

static void set_plot(tr_fxmirae_plot_t *p, bool on, double value, uint32_t rgb, int width) {
    p->on = on;
    p->value = value;
    p->rgb = rgb;
    p->width = width;
}

void tr_fxmirae_eval(tr_fxmirae_t *s, const tr_fxmirae_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    tr_fxfv_eval(&s->fv, &in->bar);

    /* 합성 3종 (원본 135~160줄) — 같은 현재/직전 봉 */
    tr_fxsyn_input_t sin;
    memset(&sin, 0, sizeof(sin));
    sin.cur_date = in->bar.date;
    sin.cur_time = in->bar.time;
    sin.is_new_bar = s->fv.is_new_bar;
    sin.has_prev = in->has_prev;
    sin.prev_date = in->prev_date;
    sin.prev_time = in->prev_time;
    sin.prev_h = in->prev_h;
    sin.prev_l = in->prev_l;
    sin.prev_c = in->prev_c;
    sin.prev_v = in->prev_v;
    tr_fxsyn_eval(&s->syn5, &sin);
    tr_fxsyn_eval(&s->syn15, &sin);
    tr_fxsyn_eval(&s->syn30, &sin);

    /* Plot 매핑 (원본 75~160줄) */
    const tr_fxfv_output_t *o = &s->fv.out;
    s->stage_rgb = stage_color(o->score);
    set_plot(&s->plots[0], true, o->score, s->stage_rgb, 2);
    set_plot(&s->plots[1], o->reg_flat != 0.0, o->reg_flat, s->stage_rgb, 3);
    set_plot(&s->plots[2], o->market_center != 0.0, o->market_center, s->stage_rgb, 3);
    static const uint32_t target_rgb[5] = {
        RGB_(110, 110, 110), RGB_(140, 100, 40), RGB_(150, 80, 150),
        RGB_(30, 140, 140), RGB_(90, 70, 180)
    };
    for (int k = 0; k < 5; k++) {
        set_plot(&s->plots[3 + k], o->persist_target[k] != 0.0, o->persist_target[k],
                 target_rgb[k], 2);
    }
    /* 지난구간 10개는 기준 구간 최고가 > 0 게이트 공통 */
    bool lup_on = o->lup_high > 0.0;
    set_plot(&s->plots[8], lup_on, o->lup_high, RGB_ORANGE, 2);
    set_plot(&s->plots[9], lup_on, o->lup_low, RGB_ORANGE, 2);
    set_plot(&s->plots[10], lup_on, o->lup_382, RGB_ORANGE, 2);
    set_plot(&s->plots[11], lup_on, o->lup_500, RGB_ORANGE, 2);
    set_plot(&s->plots[12], lup_on, o->lup_618, RGB_ORANGE, 2);
    bool ldn_on = o->ldn_high > 0.0;
    set_plot(&s->plots[13], ldn_on, o->ldn_high, RGB_GREEN, 2);
    set_plot(&s->plots[14], ldn_on, o->ldn_low, RGB_GREEN, 2);
    set_plot(&s->plots[15], ldn_on, o->ldn_382, RGB_GREEN, 2);
    set_plot(&s->plots[16], ldn_on, o->ldn_500, RGB_GREEN, 2);
    set_plot(&s->plots[17], ldn_on, o->ldn_618, RGB_GREEN, 2);
    /* 합성 기준선 6개 */
    set_plot(&s->plots[18], s->syn5.out_reg_valid == 1, s->syn5.out_reg, RGB_(240, 130, 30), 3);
    set_plot(&s->plots[19], s->syn5.out_mkt_valid == 1, s->syn5.out_mkt, RGB_(240, 130, 30), 1);
    set_plot(&s->plots[20], s->syn15.out_reg_valid == 1, s->syn15.out_reg, RGB_(140, 70, 190), 3);
    set_plot(&s->plots[21], s->syn15.out_mkt_valid == 1, s->syn15.out_mkt, RGB_(140, 70, 190), 1);
    set_plot(&s->plots[22], s->syn30.out_reg_valid == 1, s->syn30.out_reg, RGB_(20, 130, 180), 3);
    set_plot(&s->plots[23], s->syn30.out_mkt_valid == 1, s->syn30.out_mkt, RGB_(20, 130, 180), 1);
}

bool tr_fxmirae_relink(tr_fxmirae_t *s) {
    if (s == 0) {
        return false;
    }
    bool ok = tr_fxfv_relink(&s->fv);
    ok = tr_fxsyn_relink(&s->syn5) && ok;
    ok = tr_fxsyn_relink(&s->syn15) && ok;
    return tr_fxsyn_relink(&s->syn30) && ok;
}
