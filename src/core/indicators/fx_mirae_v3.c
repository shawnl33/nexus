#include "core/indicators/fx_mirae_v3.h"

#include <math.h>
#include <string.h>

#include "core/functions/fx_align_v1.h"
#include "core/functions/fx_curve_v1.h"
#include "core/functions/fx_gap_v1.h"
#include "core/functions/fx_predict_v2.h"
#include "core/functions/fx_reg_v1.h"
#include "core/functions/fx_swing_v1.h"
#include "core/functions/fx_session_key_v1.h"
#include "core/functions/fx_trend_v1.h"
#include "core/indicators/indicator.h"

#define RGB_(r, g, b) ((uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))

static double round_tick(double v, double ps) {
    if (ps <= 0.0) {
        return v;
    }
    return floor(v / ps + 0.5) * ps;
}

static uint32_t stage_rgb(double score) {
    if (score >= 4.0) {
        return RGB_(220, 0, 0);
    }
    if (score >= 2.0) {
        return RGB_(255, 100, 70);
    }
    if (score > 0.0) {
        return RGB_(255, 185, 185);
    }
    if (score <= -4.0) {
        return RGB_(0, 0, 180);
    }
    if (score <= -2.0) {
        return RGB_(60, 130, 255);
    }
    if (score < 0.0) {
        return RGB_(180, 210, 255);
    }
    return RGB_(150, 150, 150);
}

static uint32_t past_rgb(int dir, double r2, double min_r2, int which) {
    int strong = r2 >= min_r2;
    if (dir > 0) {
        if (which == 0) {
            return strong ? RGB_(235, 80, 80) : RGB_(255, 175, 175);
        }
        if (which == 1) {
            return strong ? RGB_(255, 0, 0) : RGB_(255, 145, 145);
        }
        return strong ? RGB_(205, 55, 55) : RGB_(245, 170, 170);
    }
    if (dir < 0) {
        if (which == 0) {
            return strong ? RGB_(80, 110, 235) : RGB_(170, 190, 255);
        }
        if (which == 1) {
            return strong ? RGB_(0, 0, 255) : RGB_(145, 170, 255);
        }
        return strong ? RGB_(55, 85, 205) : RGB_(165, 185, 245);
    }
    if (which == 0) {
        return RGB_(170, 170, 170);
    }
    if (which == 1) {
        return RGB_(150, 150, 150);
    }
    return RGB_(175, 175, 175);
}

static void plot_set(tr_fxv3_t *s, int n, bool on, double value, uint32_t rgb, int width) {
    if (n < 1 || n >= TR_FXV3_PLOT_N) {
        return;
    }
    s->plots[n].on = on;
    s->plots[n].value = on ? value : 0.0;
    s->plots[n].rgb = rgb;
    s->plots[n].width = width;
}

static bool hist_at(const tr_fxv3_t *s, int back, tr_fxv3_frame_t *out) {
    if (back < 1 || back > s->hist_n) {
        return false;
    }
    *out = s->hist[back - 1];
    return true;
}

static void hist_push(tr_fxv3_t *s, const tr_fxv3_frame_t *f) {
    int n = s->hist_n;
    if (n < TR_FXV3_HIST) {
        n++;
    }
    for (int i = n - 1; i > 0; i--) {
        s->hist[i] = s->hist[i - 1];
    }
    s->hist[0] = *f;
    s->hist_n = n;
}

static void hist_pop(tr_fxv3_t *s) {
    if (s->hist_n <= 0) {
        return;
    }
    for (int i = 0; i < s->hist_n - 1; i++) {
        s->hist[i] = s->hist[i + 1];
    }
    s->hist_n--;
}

static bool five_below(const tr_fxv3_t *s, double cur, int is_high, double level) {
    if (s->cur.session_bars <= 4 || s->hist_n < 4) {
        return false;
    }
    if (is_high) {
        if (!(cur < level)) {
            return false;
        }
    } else if (!(cur > level)) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        double v = is_high ? s->hist[i].high : s->hist[i].low;
        if (is_high) {
            if (!(v < level)) {
                return false;
            }
        } else if (!(v > level)) {
            return false;
        }
    }
    return true;
}

void tr_fxv3_default_config(tr_fxv3_config_t *cfg, double price_scale) {
    if (cfg == 0) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->predict_bars[0] = 5;
    cfg->predict_bars[1] = 10;
    cfg->predict_bars[2] = 15;
    cfg->predict_bars[3] = 30;
    cfg->predict_bars[4] = 60;
    cfg->min_r2 = 0.40;
    cfg->persist_bars = 5;
    cfg->price_scale = price_scale;
    cfg->past_mode = 1;
    cfg->show_range = 1;
    cfg->show_trade = 1;
    cfg->min_final_strength = 40;
    cfg->market_period = 20;
    cfg->market_band = 1.0;
    cfg->show_mkt_band = 1;
    cfg->min_hold_bars = 3;
    cfg->fade_time = 1.0;
    cfg->show_time_ratio = 1;
}

bool tr_fxv3_init(tr_fxv3_t *s, const tr_fxv3_config_t *cfg) {
    if (s == 0 || cfg == 0 || cfg->price_scale <= 0.0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->cfg = *cfg;
    if (s->cfg.persist_bars < 1) {
        s->cfg.persist_bars = 1;
    }
    s->cur.last_session_bar = -1;
    s->cur.mem_session = -1;
    s->snap = s->cur;
    return true;
}

static void apply_past(tr_fxv3_t *s, const tr_fxv3_input_t *in, double reg_flat) {
    int mode = s->cfg.past_mode;
    int valid[5] = {0};
    int dir[5] = {0};
    double r2[5] = {0};
    double price[5] = {0};
    for (int k = 0; k < 5; k++) {
        tr_fxv3_frame_t f;
        int back = s->cfg.predict_bars[k];
        if (!hist_at(s, back, &f)) {
            continue;
        }
        valid[k] = f.reg_valid;
        dir[k] = f.dir[k];
        r2[k] = f.r2;
        price[k] = f.pred[k];
        if (f.session_no != s->cur.session_no) {
            valid[k] = 0;
        }
    }
    uint32_t c2 = valid[1] ? past_rgb(dir[1], r2[1], s->cfg.min_r2, 1) : RGB_(205, 205, 205);
    plot_set(s, 20, mode == 2 && valid[0], price[0], past_rgb(dir[0], r2[0], s->cfg.min_r2, 0), 1);
    {
        bool on = mode == 1 || (mode == 2 && valid[1]);
        double v = mode == 1 ? reg_flat - s->cfg.price_scale * 4.0 : price[1];
        uint32_t rgb = mode == 1 ? c2 : past_rgb(dir[1], r2[1], s->cfg.min_r2, 1);
        plot_set(s, 21, on, v, rgb, 2);
    }
    plot_set(s, 22, mode == 2 && valid[2], price[2], past_rgb(dir[2], r2[2], s->cfg.min_r2, 2), 1);
    plot_set(s, 23, mode == 2 && valid[3], price[3], past_rgb(dir[3], r2[3], s->cfg.min_r2, 3), 1);
    plot_set(s, 24, mode == 2 && valid[4], price[4], past_rgb(dir[4], r2[4], s->cfg.min_r2, 4), 1);
    (void)in;
}

static void apply_memory(tr_fxv3_t *s, const tr_fxv3_input_t *in, double reg_flat) {
    int base = 0;
    if (in->final_valid && in->final_dir != 0) {
        base = in->final_dir;
    } else if (in->reg_valid && in->reg_sign > 0) {
        base = 1;
    } else if (in->reg_valid && in->reg_sign < 0) {
        base = -1;
    }
    if (s->cur.session_no != s->cur.mem_session) {
        s->cur.mem_valid = 0;
        s->cur.mem_dir = 0;
        s->cur.mem_price = 0;
        memset(s->cur.mem_tgt, 0, sizeof(s->cur.mem_tgt));
        memset(s->cur.mem_up, 0, sizeof(s->cur.mem_up));
        memset(s->cur.mem_dn, 0, sizeof(s->cur.mem_dn));
        s->cur.mem_session = s->cur.session_no;
    }
    int updated = 0;
    if (in->reg_valid && base != 0 && in->pred_price[0] != 0.0 && in->pred_price[1] != 0.0 &&
        in->pred_price[2] != 0.0 && (s->cur.mem_valid == 0 || base != s->cur.mem_dir)) {
        double ps = s->cfg.price_scale;
        s->cur.mem_price = reg_flat;
        for (int k = 0; k < 5; k++) {
            s->cur.mem_tgt[k] = round_tick(in->pred_price[k], ps);
        }
        double base_err = fmax(in->reg_resid, in->pred_vol * 0.25);
        for (int k = 0; k < 5; k++) {
            double span = base_err * sqrt(fmax(1.0, (double)s->cfg.predict_bars[k]));
            s->cur.mem_up[k] = round_tick(in->pred_price[k] + span, ps);
            s->cur.mem_dn[k] = round_tick(in->pred_price[k] - span, ps);
        }
        s->cur.mem_dir = base;
        s->cur.mem_valid = 1;
        updated = 1;
    }
    int up = s->cur.mem_dir > 0;
    uint32_t c_tgt[5] = {
        up ? RGB_(255, 170, 170) : RGB_(140, 170, 255),
        up ? RGB_(255, 0, 0) : RGB_(0, 0, 255),
        up ? RGB_(180, 0, 0) : RGB_(0, 0, 150),
        up ? RGB_(180, 0, 0) : RGB_(0, 0, 150),
        up ? RGB_(180, 0, 0) : RGB_(0, 0, 150),
    };
    int tgt_n[5] = {33, 34, 35, 56, 57};
    int tgt_w[5] = {3, 5, 3, 3, 3};
    plot_set(s, 32, s->cur.mem_valid, s->cur.mem_price, RGB_(0, 0, 0), 2);
    for (int k = 0; k < 5; k++) {
        plot_set(s, tgt_n[k], s->cur.mem_valid, s->cur.mem_tgt[k], c_tgt[k], tgt_w[k]);
    }
    int range_on = s->cfg.past_mode > 0 && s->cfg.show_range;
    int up_n[5] = {36, 38, 40, 58, 83};
    int dn_n[5] = {37, 39, 41, 59, 84};
    uint32_t up_c[5] = {RGB_(190, 210, 190), RGB_(165, 165, 165), RGB_(205, 195, 205),
                        RGB_(205, 195, 205), RGB_(205, 195, 205)};
    bool show = s->cur.mem_valid && range_on && !updated;
    bool hide_up = five_below(s, in->high, 1, s->cur.mem_tgt[2]);
    bool hide_dn = five_below(s, in->low, 0, s->cur.mem_tgt[2]);
    for (int k = 0; k < 5; k++) {
        plot_set(s, up_n[k], show && !hide_up, s->cur.mem_up[k], up_c[k], 0);
        plot_set(s, dn_n[k], show && !hide_dn, s->cur.mem_dn[k], up_c[k], 0);
    }
}

static uint32_t mkt_stage_rgb(int stage) {
    if (stage == 3) {
        return RGB_(220, 0, 0);
    }
    if (stage == 2) {
        return RGB_(255, 100, 70);
    }
    if (stage == 1) {
        return RGB_(255, 185, 185);
    }
    if (stage == -1) {
        return RGB_(180, 210, 255);
    }
    if (stage == -2) {
        return RGB_(60, 130, 255);
    }
    if (stage == -3) {
        return RGB_(0, 0, 180);
    }
    return RGB_(120, 120, 120);
}

static void apply_market(tr_fxv3_t *s, const tr_fxv3_input_t *in) {
    if (!in->mkt_valid) {
        return;
    }
    uint32_t col = in->mkt_stage == 0 ? stage_rgb(s->stage) : mkt_stage_rgb(in->mkt_stage);
    plot_set(s, 51, true, in->mkt_center, col, 3);
    if (!s->cfg.show_mkt_band) {
        return;
    }
    plot_set(s, 52, true, in->mkt_up1, RGB_(255, 120, 120), 0);
    plot_set(s, 53, true, in->mkt_up2, RGB_(255, 0, 0), 0);
    plot_set(s, 54, true, in->mkt_dn1, RGB_(120, 150, 255), 0);
    plot_set(s, 55, true, in->mkt_dn2, RGB_(0, 0, 255), 0);
}

static void apply_swing(tr_fxv3_t *s, const tr_fxv3_input_t *in) {
    int dir = in->sw_leg_dir;
    int w = in->sw_time_ratio < s->cfg.fade_time ? 2 : 0;
    uint32_t green = RGB_(0, 128, 0);
    uint32_t orange = RGB_(255, 127, 0);
    bool dn_on = in->sw_dn.high > 0.0 && dir == 1;
    plot_set(s, 60, dn_on, in->sw_dn.high, green, w);
    plot_set(s, 61, dn_on, in->sw_dn.low, green, w);
    plot_set(s, 62, dn_on, in->sw_dn.lvl_382, green, w);
    plot_set(s, 63, dn_on, in->sw_dn.lvl_500, green, w);
    plot_set(s, 64, dn_on, in->sw_dn.lvl_618, green, w);
    bool up_on = in->sw_up.high > 0.0 && dir == -1;
    plot_set(s, 65, up_on, in->sw_up.high, orange, w);
    plot_set(s, 66, up_on, in->sw_up.low, orange, w);
    plot_set(s, 67, up_on, in->sw_up.lvl_382, orange, w);
    plot_set(s, 68, up_on, in->sw_up.lvl_500, orange, w);
    plot_set(s, 69, up_on, in->sw_up.lvl_618, orange, w);
    plot_set(s, 70, s->cfg.show_time_ratio && in->sw_time_ratio > 0.0, in->sw_time_ratio * 100.0,
             RGB_(100, 100, 100), 2);
    if (in->sw_up_grade == 2 && dir == 1) {
        plot_set(s, 71, true, in->sw_up.ext_1618, RGB_(200, 0, 0), 3);
        plot_set(s, 72, true, in->sw_up.ext_2382, RGB_(150, 0, 0), 3);
    }
    if (in->sw_up_grade == 1 && dir == 1) {
        plot_set(s, 75, true, in->sw_up.ext_1382, RGB_(255, 140, 0), 2);
        plot_set(s, 76, true, in->sw_up.ext_200, RGB_(220, 60, 0), 2);
    }
    if (in->sw_up_grade == -2 && dir == 1) {
        plot_set(s, 77, true, in->sw_up.ext_100, RGB_(255, 140, 0), 1);
    } else if (in->sw_up_grade == 0 && in->sw_up.high > 0.0 && dir == 1) {
        plot_set(s, 77, true, in->sw_up.ext_100, RGB_(255, 200, 0), 1);
        plot_set(s, 81, true, in->sw_up.ext_1618, RGB_(255, 200, 0), 1);
    }
    if (in->sw_dn_grade == 2 && dir == -1) {
        plot_set(s, 73, true, in->sw_dn.ext_1618, RGB_(0, 0, 200), 3);
        plot_set(s, 74, true, in->sw_dn.ext_2382, RGB_(0, 0, 150), 3);
    }
    if (in->sw_dn_grade == 1 && dir == -1) {
        plot_set(s, 78, true, in->sw_dn.ext_1382, RGB_(0, 140, 255), 2);
        plot_set(s, 79, true, in->sw_dn.ext_200, RGB_(0, 60, 220), 2);
    }
    if (in->sw_dn_grade == -2 && dir == -1) {
        plot_set(s, 80, true, in->sw_dn.ext_100, RGB_(0, 60, 220), 1);
    } else if (in->sw_dn_grade == 0 && in->sw_dn.high > 0.0 && dir == -1) {
        plot_set(s, 80, true, in->sw_dn.ext_100, RGB_(0, 200, 255), 1);
        plot_set(s, 82, true, in->sw_dn.ext_1618, RGB_(0, 200, 255), 1);
    }
}

static void apply_persist(tr_fxv3_t *s, const tr_fxv3_input_t *in) {
    int dir2 = in->pred_dir[1];
    if (in->session_reset) {
        s->cur.streak = 0;
        s->cur.pst_valid = 0;
        s->cur.pst_dir = 0;
        memset(s->cur.pst_tgt, 0, sizeof(s->cur.pst_tgt));
        memset(s->cur.pst_up, 0, sizeof(s->cur.pst_up));
        memset(s->cur.pst_dn, 0, sizeof(s->cur.pst_dn));
    } else {
        if (dir2 == s->cur.prev_dir2 && dir2 != 0) {
            s->cur.streak = s->cur.streak + 1;
        } else {
            s->cur.streak = 1;
        }
    }
    if (in->reg_valid && in->reg_r2 >= s->cfg.min_r2 && dir2 != 0 &&
        s->cur.streak == s->cfg.persist_bars) {
        double ps = s->cfg.price_scale;
        double base_err = fmax(in->reg_resid, in->pred_vol * 0.25);
        s->cur.pst_valid = 1;
        s->cur.pst_dir = dir2;
        for (int k = 0; k < 5; k++) {
            double span = base_err * sqrt(fmax(1.0, (double)s->cfg.predict_bars[k]));
            s->cur.pst_tgt[k] = round_tick(in->pred_price[k], ps);
            s->cur.pst_up[k] = round_tick(in->pred_price[k] + span, ps);
            s->cur.pst_dn[k] = round_tick(in->pred_price[k] - span, ps);
        }
    }
    int up = s->cur.pst_dir > 0;
    int tgt_n[5] = {42, 43, 44, 85, 86};
    int tgt_w[5] = {1, 2, 1, 1, 1};
    uint32_t tgt_c[5] = {
        up ? RGB_(255, 190, 190) : RGB_(170, 190, 255),
        up ? RGB_(255, 100, 100) : RGB_(100, 130, 255),
        up ? RGB_(220, 100, 100) : RGB_(100, 120, 220),
        up ? RGB_(220, 100, 100) : RGB_(100, 120, 220),
        up ? RGB_(220, 100, 100) : RGB_(100, 120, 220),
    };
    for (int k = 0; k < 5; k++) {
        plot_set(s, tgt_n[k], s->cur.pst_valid, s->cur.pst_tgt[k], tgt_c[k], tgt_w[k]);
    }
    int up_n[5] = {45, 47, 49, 87, 89};
    int dn_n[5] = {46, 48, 50, 88, 96};
    uint32_t band_c[5] = {RGB_(210, 225, 210), RGB_(190, 190, 190), RGB_(220, 210, 220),
                          RGB_(220, 210, 220), RGB_(220, 210, 220)};
    bool hide_up = five_below(s, in->high, 1, s->cur.pst_tgt[2]);
    bool hide_dn = five_below(s, in->low, 0, s->cur.pst_tgt[2]);
    for (int k = 0; k < 5; k++) {
        plot_set(s, up_n[k], s->cur.pst_valid && !hide_up, s->cur.pst_up[k], band_c[k], 0);
        plot_set(s, dn_n[k], s->cur.pst_valid && !hide_dn, s->cur.pst_dn[k], band_c[k], 0);
    }
}

void tr_fxv3_eval(tr_fxv3_t *s, const tr_fxv3_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    bool is_new = !s->has_bar || in->bar_open != s->last_open;
    if (is_new) {
        s->snap = s->cur;
    } else {
        hist_pop(s);
        s->cur = s->snap;
    }
    if (in->session_reset && in->bar_index != s->cur.last_session_bar) {
        s->cur.session_no++;
        s->cur.last_session_bar = in->bar_index;
        s->cur.session_bars = 1;
    } else if (is_new && !s->has_bar) {
        s->cur.session_bars = 1;
    } else {
        /* 새 봉이든 같은 봉 재평가든, 리셋 경계가 아니면 이 봉의 번호는 직전+1 */
        s->cur.session_bars++;
    }
    memset(s->plots, 0, sizeof(s->plots));
    double ps = s->cfg.price_scale;
    double reg_flat = round_tick(in->reg_line, ps);
    s->reg_flat = reg_flat;

    double score = 0.0;
    if (in->future_dir > 0.0) {
        score += 2.0;
    } else if (in->future_dir < 0.0) {
        score -= 2.0;
    }
    if (in->future_dir > s->cur.prev_future) {
        score += 1.0;
    }
    if (in->future_dir < s->cur.prev_future) {
        score -= 1.0;
    }
    score += (double)in->market_dir + (double)in->reg_dir;
    if (in->session_reset) {
        score = 0.0;
    }
    s->stage = score;
    uint32_t scol = stage_rgb(score);
    int sw = 2 + (int)fabs(score);
    plot_set(s, 1, true, score, scol, sw);

    int rw = 2;
    if (in->reg_r2 >= 0.70) {
        rw = 6;
    } else if (in->reg_r2 >= s->cfg.min_r2) {
        rw = 4;
    }
    plot_set(s, 7, in->reg_valid, reg_flat, scol, rw);

    apply_past(s, in, reg_flat);

    bool buy = s->cfg.show_trade && in->final_valid && in->final_state >= 1;
    bool sell = s->cfg.show_trade && in->final_valid && in->final_state <= -1;
    plot_set(s, 30, buy, reg_flat,
             in->final_state >= 2 ? RGB_(255, 0, 0) : RGB_(255, 128, 0),
             in->final_state >= 2 ? 5 : 2);
    plot_set(s, 31, sell, reg_flat,
             in->final_state <= -2 ? RGB_(0, 0, 255) : RGB_(0, 160, 200),
             in->final_state <= -2 ? 5 : 2);

    apply_memory(s, in, reg_flat);
    apply_persist(s, in);
    apply_market(s, in);
    apply_swing(s, in);

    s->cur.prev_future = in->future_dir;
    s->cur.prev_dir2 = in->pred_dir[1];

    tr_fxv3_frame_t f;
    memset(&f, 0, sizeof(f));
    f.session_no = s->cur.session_no;
    f.high = in->high;
    f.low = in->low;
    f.reg_valid = in->reg_valid;
    f.r2 = in->reg_r2;
    for (int k = 0; k < 5; k++) {
        f.pred[k] = in->pred_price[k];
        f.dir[k] = in->pred_dir[k];
    }
    hist_push(s, &f);
    s->last_open = in->bar_open;
    s->has_bar = true;
}

bool tr_fxv3_run_init(tr_fxv3_run_t *s, double price_scale) {
    if (s == 0 || price_scale <= 0.0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->price_scale = price_scale;
    tr_fxv3_config_t cfg;
    tr_fxv3_default_config(&cfg, price_scale);
    if (!tr_fxv3_init(&s->view, &cfg)) {
        return false;
    }
    if (!tr_fxreg_init(&s->reg, TR_COMPRESS_MIN, 1) || !tr_fxp2_init(&s->pred) || !tr_fxc_init(&s->curve) ||
        !tr_fxsw_init(&s->swing)) {
        return false;
    }
    tr_fxtrend_config_t tc = {10};
    tr_fxtrend_init(&s->trend, &tc);
    tr_fxgap_config_t gc = {10, 0.35, 0.75};
    tr_fxgap_init(&s->gap, &gc);
    return true;
}

bool tr_fxv3_run_relink(tr_fxv3_run_t *s) {
    if (s == 0) {
        return false;
    }
    bool ok = tr_fxreg_relink(&s->reg);
    ok = tr_fxp2_relink(&s->pred) && ok;
    return tr_fxc_relink(&s->curve) && ok;
}

const tr_fxv3_t *tr_fxv3_run_view(const tr_fxv3_run_t *s) {
    return s != 0 ? &s->view : 0;
}

void tr_fxv3_run_eval(tr_fxv3_run_t *s, int64_t date, int64_t time_hhmmss,
                      double open, double high, double low, double close, double volume,
                      tr_time_us_t bar_open) {
    if (s == 0) {
        return;
    }
    bool is_new = !s->has_bar || bar_open != s->last_open;
    int64_t key = tr_fx_session_key_v1(date, time_hhmmss);
    bool reset = !s->has_key || key != s->prev_key;
    if (reset) {
        s->session_bars = 1;
        if (is_new) {
            s->mid_n = 0;
        }
    } else if (is_new) {
        s->session_bars++;
    }
    if (is_new) {
        s->bar_i++;
    }
    double mid = (high + low) / 2.0;
    tr_fxreg_input_t rin = {
        .session_reset = reset, .session_bars = s->session_bars, .price = mid, .bar_open = bar_open,
    };
    tr_fxreg_eval(&s->reg, &rin);
    tr_fxp2_input_t pin = {
        .cur_line = s->reg.line,
        .slope = s->reg.slope,
        .reg_valid = s->reg.reg_valid,
        .r2 = s->reg.r2,
        .high = high,
        .low = low,
        .close = close,
        .session_reset = reset,
        .session_bars = s->session_bars,
        .is_new_bar = is_new,
    };
    for (int k = 0; k < 5; k++) {
        pin.horizons[k] = s->view.cfg.predict_bars[k];
    }
    tr_fxp2_eval(&s->pred, &pin);
    tr_fxc_input_t cin = {
        .session_reset = reset,
        .session_bars = s->session_bars,
        .high = high,
        .low = low,
        .ticks = 10,
        .is_new_bar = is_new,
    };
    tr_fxc_eval(&s->curve, &cin);
    tr_fxsw_input_t sin = {
        .session_reset = reset,
        .session_bars = s->session_bars,
        .future_dir = reset ? 0.0 : s->curve.direction,
        .min_hold_bars = s->view.cfg.min_hold_bars,
        .high = high,
        .low = low,
        .is_new_bar = is_new,
    };
    tr_fxsw_eval(&s->swing, &sin);
    tr_fxtrend_input_t tin = {
        .session_reset = reset,
        .bar_index = s->bar_i,
        .time_hhmmss = time_hhmmss,
        .high = high,
        .low = low,
        .close = close,
        .price_scale = s->price_scale,
        .min_r2 = 0.40,
        .bar_open = bar_open,
    };
    tr_fxtrend_eval(&s->trend, &tin);
    tr_fxgap_input_t gin = {
        .session_reset = reset,
        .bar_index = s->bar_i,
        .time_hhmmss = time_hhmmss,
        .open = open,
        .high = high,
        .low = low,
        .close = close,
        .bar_open = bar_open,
    };
    tr_fxgap_eval(&s->gap, &gin);

    if (is_new) {
        if (s->mid_n < 20) {
            s->mid_n++;
        }
        for (int j = s->mid_n - 1; j > 0; j--) {
            s->mids[j] = s->mids[j - 1];
        }
    }
    s->mids[0] = mid;
    if (reset && is_new) {
        s->vwap_n = 0;
    }
    if (is_new) {
        if (s->vwap_n < 20) {
            s->vwap_n++;
        }
        for (int j = s->vwap_n - 1; j > 0; j--) {
            s->tp[j] = s->tp[j - 1];
            s->volw[j] = s->volw[j - 1];
        }
    }
    s->tp[0] = (high + low + close) / 3.0;
    s->volw[0] = volume;
    int vuse = s->vwap_n;
    if (vuse > s->view.cfg.market_period) {
        vuse = s->view.cfg.market_period;
    }
    if (vuse > s->session_bars) {
        vuse = s->session_bars;
    }
    double sum_v = 0.0, sum_pv = 0.0;
    for (int j = 0; j < vuse; j++) {
        if (s->volw[j] > 0.0) {
            sum_v += s->volw[j];
            sum_pv += s->tp[j] * s->volw[j];
        }
    }
    int mkt_valid = 0;
    double mkt_c = 0, mkt_u1 = 0, mkt_d1 = 0, mkt_u2 = 0, mkt_d2 = 0;
    int mkt_stage = 0;
    if (sum_v > 0.0 && vuse >= 2) {
        mkt_c = sum_pv / sum_v;
        double var_sum = 0.0;
        for (int j = 0; j < vuse; j++) {
            if (s->volw[j] > 0.0) {
                double d = s->tp[j] - mkt_c;
                var_sum += d * d * s->volw[j];
            }
        }
        double sd = sqrt(var_sum / sum_v) * s->view.cfg.market_band;
        mkt_u1 = mkt_c + sd;
        mkt_d1 = mkt_c - sd;
        mkt_u2 = mkt_c + (mkt_u1 - mkt_c) * 2.0;
        mkt_d2 = mkt_c - (mkt_c - mkt_d1) * 2.0;
        mkt_valid = 1;
        if (is_new) {
            s->vwap_before = s->last_vwap;
        }
        s->last_vwap = mkt_c;
    }
    int use = s->mid_n < 20 ? s->mid_n : 20;
    if (use > s->session_bars) {
        use = s->session_bars;
    }
    double sum = 0.0;
    for (int j = 0; j < use; j++) {
        sum += s->mids[j];
    }
    double center = use > 0 ? sum / (double)use : mid;
    if (is_new) {
        s->center_before = s->last_center;
    }
    double slope = s->session_bars > 1 ? center - s->center_before : 0.0;
    s->last_center = center;
    int market_dir = 0;
    if (close > center && slope > 0.0) {
        market_dir = 1;
    } else if (close < center && slope < 0.0) {
        market_dir = -1;
    }
    double flat = round_tick(s->reg.line, s->price_scale);
    if (mkt_valid) {
        double mslope = s->session_bars > 1 ? mkt_c - s->vwap_before : 0.0;
        double dist = fmax(s->price_scale, fabs(mkt_u1 - mkt_c));
        if (dist > 0.0) {
            double pos = (close - mkt_c) / dist * 100.0;
            if (pos > 100.0) {
                pos = 100.0;
            }
            if (pos < -100.0) {
                pos = -100.0;
            }
            if (mslope > 0.0 && close > mkt_c && close > flat) {
                mkt_stage = pos >= 66.0 ? 3 : pos >= 33.0 ? 2 : 1;
            } else if (mslope < 0.0 && close < mkt_c && close < flat) {
                mkt_stage = pos <= -66.0 ? -3 : pos <= -33.0 ? -2 : -1;
            }
            if (high >= mkt_c && low <= mkt_c) {
                mkt_stage = 0;
            }
        }
    }
    int reg_dir = 0;
    if (s->reg.reg_valid && s->reg.r2 >= 0.40) {
        if (close > flat) {
            reg_dir = 1;
        } else if (close < flat) {
            reg_dir = -1;
        }
    }
    tr_fxalign_output_t align;
    memset(&align, 0, sizeof(align));
    if (s->gap.valid) {
        tr_fxalign_input_t ain;
        memset(&ain, 0, sizeof(ain));
        for (int k = 0; k < 3; k++) {
            ain.pred_dir[k] = s->pred.pred_dir[k];
        }
        ain.reg_valid = s->reg.reg_valid;
        ain.r2 = s->reg.r2;
        ain.trend_dir = s->trend.dir;
        ain.trend_state = s->trend.state;
        ain.trend_strength = s->trend.strength;
        ain.trend_valid = s->trend.valid;
        ain.gap_grade = s->gap.gap_grade;
        ain.daily_weight_in = s->gap.daily_weight;
        ain.elapsed_min = s->gap.elapsed_min;
        ain.big_gap_reeval_min = 30;
        ain.min_r2 = 0.40;
        ain.min_final_strength = 40;
        tr_fxalign_eval(&ain, &align);
    }
    tr_fxv3_input_t vin;
    memset(&vin, 0, sizeof(vin));
    vin.session_reset = reset;
    vin.bar_index = s->bar_i;
    vin.bar_open = bar_open;
    vin.high = high;
    vin.low = low;
    vin.close = close;
    vin.reg_valid = s->reg.reg_valid;
    vin.reg_line = s->reg.line;
    vin.reg_r2 = s->reg.r2;
    vin.reg_resid = s->reg.residual;
    vin.reg_sign = s->reg.line_sign;
    for (int k = 0; k < 5; k++) {
        vin.pred_price[k] = s->pred.pred_price[k];
        vin.pred_dir[k] = s->pred.pred_dir[k];
    }
    vin.pred_vol = s->pred.volatility;
    vin.future_dir = reset ? 0.0 : s->curve.direction;
    vin.market_dir = market_dir;
    vin.reg_dir = reg_dir;
    vin.final_valid = align.final_valid;
    vin.final_dir = align.final_dir;
    vin.final_state = align.final_state;
    vin.mkt_valid = mkt_valid;
    vin.mkt_center = mkt_c;
    vin.mkt_up1 = mkt_u1;
    vin.mkt_dn1 = mkt_d1;
    vin.mkt_up2 = mkt_u2;
    vin.mkt_dn2 = mkt_d2;
    vin.mkt_stage = mkt_stage;
    vin.sw_leg_dir = s->swing.out_leg_dir;
    vin.sw_time_ratio = s->swing.out_time_ratio;
    vin.sw_up_grade = s->swing.out_lup_grade;
    vin.sw_dn_grade = s->swing.out_ldn_grade;
    vin.sw_up = s->swing.out_lup;
    vin.sw_dn = s->swing.out_ldn;
    tr_fxv3_eval(&s->view, &vin);
    s->prev_key = key;
    s->has_key = true;
    s->last_open = bar_open;
    s->has_bar = true;
    (void)open;
}
