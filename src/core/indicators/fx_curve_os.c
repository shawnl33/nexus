#include "core/indicators/fx_curve_os.h"

#include <string.h>

#define RGB_(r, g, b) ((uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))
#define RGB_ORANGE RGB_(255, 127, 0)
#define RGB_GREEN RGB_(0, 128, 0)

static void set_plot(tr_fxmirae_plot_t *p, bool on, double value, uint32_t rgb, int width) {
    p->on = on;
    p->value = value;
    p->rgb = rgb;
    p->width = width;
}

static uint32_t stage_color(double score) {
    if (score >= 4.0) return RGB_(220, 0, 0);
    if (score >= 2.0) return RGB_(255, 100, 70);
    if (score == 1.0) return RGB_(255, 185, 185);
    if (score <= -4.0) return RGB_(0, 0, 180);
    if (score <= -2.0) return RGB_(60, 130, 255);
    if (score == -1.0) return RGB_(180, 210, 255);
    return RGB_(150, 150, 150);
}

bool tr_fxcu_init(tr_fxcu_t *s, double price_scale) {
    if (s == 0 || price_scale <= 0.0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    tr_fxmirae_config_t cfg;
    tr_fxmirae_default_config(&cfg, price_scale);
    cfg.fv.swing_link = 0;
    cfg.fv.momentum_ignore_ticks = 1.0;
    cfg.synth_time_basis = 0;
    if (!tr_fxmirae_init(&s->base, &cfg)) return false;
    return tr_fxec_init(&s->entry, price_scale);
}

bool tr_fxcu_relink(tr_fxcu_t *s) {
    if (s == 0) {
        return false;
    }
    return tr_fxmirae_relink(&s->base) && tr_fxec_relink(&s->entry);
}

static void note_level(int has_prev, double cur, double prev_px, double *mem, int *flag) {
    if (cur == 0.0) {
        *mem = 0.0;
        *flag = 0;
        return;
    }
    if (has_prev && cur != prev_px) {
        if (prev_px > 0.0) {
            *mem = prev_px;
            *flag = cur > *mem ? 1 : 0;
        } else {
            *flag = 0;
        }
    }
}

void tr_fxcu_eval(tr_fxcu_t *s, const tr_fxmirae_input_t *in, const tr_fxsyn_t *syn5,
                  const tr_fxsyn_t *syn15, const tr_fxsyn_t *syn30) {
    if (s == 0 || in == 0) {
        return;
    }
    bool is_new = !s->has_bar || in->bar.bar_open != s->last_open;
    if (is_new && s->brk_has_cur) {
        s->brk_prev_buy = s->brk_buy;
        s->brk_prev_sell = s->brk_sell;
        s->brk_prev_dn_hi = s->brk_dn_hi;
        s->brk_prev_up_lo = s->brk_up_lo;
        s->brk_prev_close = s->brk_close;
        s->brk_has_prev = 1;
    }
    if (is_new) {
        s->prev = s->st;
        s->has_bar = true;
        s->last_open = in->bar.bar_open;
    } else {
        s->st = s->prev;
    }
    tr_fxmirae_eval(&s->base, in);
    const tr_fxfv_output_t *o = &s->base.fv.out;
    tr_fxcu_mem_t *m = &s->st;
    const tr_fxcu_mem_t *old = &s->prev;
    int has_prev = s->base.fv.session_bars > 1 || (!s->base.fv.session_reset && s->base.fv.has_bar);
    /* 첫 봉이 아니면 직전 봉의 기억에서 시작한다 */
    if (s->base.fv.session_bars > 1) {
        *m = *old;
    }
    note_level(s->base.fv.session_bars > 1, o->ldn_low, old->dn_lo, &m->mem_dn_lo, &m->dn_lo_up);
    note_level(s->base.fv.session_bars > 1, o->lup_low, old->up_lo, &m->mem_up_lo, &m->up_lo_up);
    note_level(s->base.fv.session_bars > 1, o->ldn_high, old->dn_hi, &m->mem_dn_hi, &m->dn_hi_up);
    note_level(s->base.fv.session_bars > 1, o->lup_high, old->up_hi, &m->mem_up_hi, &m->up_hi_up);
    m->dn_lo = o->ldn_low;
    m->up_lo = o->lup_low;
    m->dn_hi = o->ldn_high;
    m->up_hi = o->lup_high;
    (void)has_prev;

    int up_trend = m->dn_lo_up && m->up_lo_up && m->dn_hi_up && m->up_hi_up;
    int dn_trend = m->mem_dn_lo > 0.0 && o->ldn_low < m->mem_dn_lo && m->mem_up_lo > 0.0 &&
                   o->lup_low < m->mem_up_lo && m->mem_dn_hi > 0.0 && o->ldn_high < m->mem_dn_hi &&
                   m->mem_up_hi > 0.0 && o->lup_high < m->mem_up_hi;
    int blue = o->lup_high > 0.0 && o->ldn_high > 0.0 && o->lup_low > 0.0 && o->ldn_low > 0.0 &&
               o->lup_high < o->ldn_high && o->ldn_low < o->lup_low;
    int red = o->lup_high > 0.0 && o->ldn_high > 0.0 && o->lup_low > 0.0 && o->ldn_low > 0.0 &&
              o->lup_high > o->ldn_high && o->lup_low < o->ldn_low;

    uint32_t stage = stage_color(o->score);
    int above = 0, below = 0;
    for (int k = 0; k < 5; k++) {
        double t = o->persist_target[k];
        if (t != 0.0 && t > in->bar.high) above++;
        if (t != 0.0 && t < in->bar.low) below++;
    }
    uint32_t up_col = above >= 3 ? RGB_(30, 30, 150) : (above == 2 ? RGB_(80, 90, 210) : RGB_(170, 175, 235));
    uint32_t dn_col = below >= 3 ? RGB_(190, 0, 90) : (below == 2 ? RGB_(230, 80, 140) : RGB_(245, 170, 205));
    uint32_t mid_col = RGB_(150, 150, 150);

    memset(s->plots, 0, sizeof(s->plots));
    set_plot(&s->plots[0], true, o->score, stage, 2);
    set_plot(&s->plots[1], o->reg_flat != 0.0, o->reg_flat, stage, 3);
    set_plot(&s->plots[2], o->market_center != 0.0, o->market_center, stage, 3);
    for (int k = 0; k < 5; k++) {
        double t = o->persist_target[k];
        uint32_t col = mid_col;
        if (t > in->bar.high) col = up_col;
        else if (t != 0.0 && t < in->bar.low) col = dn_col;
        set_plot(&s->plots[3 + k], t != 0.0, t, col, 1);
    }
    int lup = o->lup_high > 0.0;
    uint32_t hi_col = red ? RGB_(190, 0, 0) : (up_trend ? RGB_(255, 160, 160) : (dn_trend ? RGB_(160, 190, 255) : RGB_ORANGE));
    /* 수식 굵기 0은 그리지 않음이지만, 지난구간 십자는 기본 주황·초록을 유지한다.
     * 화면 굵기 1=가늘게, 2=기본, 3=구조(수식 굵기 2). */
    int hi_w = red ? 3 : ((up_trend || dn_trend) ? 1 : 2);
    set_plot(&s->plots[8], lup, o->lup_high, hi_col, lup ? hi_w : 0);
    set_plot(&s->plots[9], lup, o->lup_low, red ? RGB_(190, 0, 0) : RGB_ORANGE, lup ? (red ? 3 : 2) : 0);
    set_plot(&s->plots[10], lup, o->lup_382, RGB_ORANGE, 2);
    set_plot(&s->plots[11], lup, o->lup_500, RGB_ORANGE, 2);
    set_plot(&s->plots[12], lup, o->lup_618, RGB_ORANGE, 2);
    int ldn = o->ldn_high > 0.0;
    uint32_t lo_col = blue ? RGB_(0, 0, 140) : (up_trend ? RGB_(255, 160, 160) : (dn_trend ? RGB_(160, 190, 255) : RGB_GREEN));
    int lo_w = blue ? 3 : ((up_trend || dn_trend) ? 1 : 2);
    set_plot(&s->plots[13], ldn, o->ldn_high, blue ? RGB_(0, 0, 140) : RGB_GREEN, ldn ? (blue ? 3 : 2) : 0);
    set_plot(&s->plots[14], ldn, o->ldn_low, lo_col, ldn ? lo_w : 0);
    set_plot(&s->plots[15], ldn, o->ldn_382, RGB_GREEN, 2);
    set_plot(&s->plots[16], ldn, o->ldn_500, RGB_GREEN, 2);
    set_plot(&s->plots[17], ldn, o->ldn_618, RGB_GREEN, 2);
    /* Plot27·28. 상승 0.382가 하락 최고 위, 하락 0.618이 상승 최저 아래. */
    int struct_up = lup && ldn && o->lup_382 > o->ldn_high;
    int struct_dn = o->lup_low > 0.0 && ldn && o->ldn_618 < o->lup_low;
    set_plot(&s->plots[26], struct_up, o->lup_382, RGB_(220, 0, 220), 3);
    set_plot(&s->plots[27], struct_dn, o->ldn_618, RGB_(0, 170, 170), 3);
    const tr_fxsyn_t *syns[3] = {syn5, syn15, syn30};
    const uint32_t syn_rgb[3] = {RGB_(240, 130, 30), RGB_(140, 70, 190), RGB_(20, 130, 180)};
    for (int i = 0; i < 3; i++) {
        int reg_on = syns[i] && syns[i]->out_reg_valid == 1;
        int mkt_on = syns[i] && syns[i]->out_mkt_valid == 1;
        set_plot(&s->plots[18 + i * 2], reg_on, reg_on ? syns[i]->out_reg : 0.0, syn_rgb[i], 3);
        set_plot(&s->plots[19 + i * 2], mkt_on, mkt_on ? syns[i]->out_mkt : 0.0, syn_rgb[i], 1);
    }
    tr_fxec_input_t ein;
    memset(&ein, 0, sizeof(ein));
    ein.date = in->bar.date;
    ein.time = in->bar.time;
    ein.bar_open = in->bar.bar_open;
    ein.is_new_bar = is_new;
    ein.high = in->bar.high;
    ein.low = in->bar.low;
    ein.close = in->bar.close;
    ein.volume = in->bar.volume;
    ein.fv = o;
    tr_fxec_eval(&s->entry, &ein);
    if (s->entry.out.dir != 0) {
        set_plot(&s->plots[24], true, s->entry.out.price, s->entry.out.rgb, s->entry.out.size);
        if (s->entry.out.emph) {
            set_plot(&s->plots[25], true, s->entry.out.price, s->entry.out.emph_rgb, 3);
        }
    }
    /* #우드스탁_해외선물하락최고돌파. 완화단계 기본 2, 표시여유틱 4. */
    {
        int valid = o->lup_high > 0.0 && o->lup_low > 0.0 && o->ldn_high > 0.0 && o->ldn_low > 0.0;
        double buy_base = o->lup_500;
        double sell_base = o->ldn_500;
        int buy = valid && buy_base < o->ldn_low && o->ldn_high >= o->lup_high;
        int sell = valid && sell_base > o->lup_high && o->lup_low <= o->ldn_low;
        double ps = s->base.fv.cfg.price_scale;
        int broke_up = s->brk_has_prev && s->brk_prev_buy &&
                       in->bar.close > s->brk_prev_dn_hi && s->brk_prev_close <= s->brk_prev_dn_hi;
        int broke_dn = s->brk_has_prev && s->brk_prev_sell &&
                       in->bar.close < s->brk_prev_up_lo && s->brk_prev_close >= s->brk_prev_up_lo;
        set_plot(&s->plots[28], broke_up, in->bar.low - 4.0 * ps, RGB_(255, 190, 0), 6);
        set_plot(&s->plots[29], broke_dn, in->bar.high + 4.0 * ps, RGB_(0, 200, 200), 6);
        set_plot(&s->plots[30], buy, o->ldn_high, RGB_(230, 180, 0), 1);
        set_plot(&s->plots[31], sell, o->lup_low, RGB_(0, 170, 170), 1);
        s->brk_buy = buy;
        s->brk_sell = sell;
        s->brk_dn_hi = o->ldn_high;
        s->brk_up_lo = o->lup_low;
        s->brk_close = in->bar.close;
        s->brk_has_cur = 1;
    }
}
