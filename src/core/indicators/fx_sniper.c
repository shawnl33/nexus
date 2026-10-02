#include "core/indicators/fx_sniper.h"

#include <math.h>
#include <string.h>

#define RGB_(r, g, b) ((uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))

static int clamp_i(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

void tr_fxsniper_default_config(tr_fxsniper_config_t *cfg, double price_scale) {
    if (cfg == 0) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->price_scale = price_scale;
    cfg->n_targets = 3;
    cfg->target_limit = 25;
    cfg->price_limit = 30;
    cfg->range_period = 10;
    cfg->atr_period = 20;
    cfg->target_atr_cap = 2;
    cfg->price_atr_cap = 3;
    cfg->hold_bars = 3;
    cfg->wait_bars = 10;
    cfg->slack_ticks = 1;
}

void tr_fxsniper_init(tr_fxsniper_t *s, const tr_fxsniper_config_t *cfg) {
    if (s == 0) {
        return;
    }
    memset(s, 0, sizeof(*s));
    if (cfg != 0) {
        s->cfg = *cfg;
    }
    s->cfg.n_targets = s->cfg.n_targets == 5 ? 5 : 3;
    s->cfg.range_period = clamp_i(s->cfg.range_period, 2, TR_FXSNIPER_HIST);
    s->cfg.atr_period = clamp_i(s->cfg.atr_period, 2, TR_FXSNIPER_HIST);
    if (s->cfg.hold_bars < 1) {
        s->cfg.hold_bars = 1;
    }
    if (s->cfg.wait_bars < 1) {
        s->cfg.wait_bars = 1;
    }
}

static uint32_t score_rgb(int score_ex, int compound, int ratio_score) {
    uint32_t rgb = RGB_(220, 220, 220);
    if (score_ex == 1) {
        rgb = RGB_(128, 160, 255);
    } else if (score_ex == 2) {
        rgb = RGB_(0, 0, 255);
    } else if (score_ex == 3) {
        rgb = RGB_(0, 0, 150);
    } else if (score_ex == -1) {
        rgb = RGB_(255, 160, 160);
    } else if (score_ex == -2) {
        rgb = RGB_(255, 0, 0);
    } else if (score_ex == -3) {
        rgb = RGB_(180, 0, 0);
    } else if (compound) {
        rgb = RGB_(0, 128, 0);
    }
    if (rgb == RGB_(220, 220, 220) && ratio_score > 0) {
        rgb = RGB_(160, 220, 160);
    }
    return rgb;
}

void tr_fxsniper_eval(tr_fxsniper_t *s, const tr_fxsniper_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    bool is_new = !s->has_bar || in->bar_open != s->last_open;
    if (is_new) {
        s->s_session_bars = s->session_bars;
        s->s_streak = s->streak;
        s->s_waiting = s->waiting;
        s->s_waited = s->waited;
        s->s_first = s->first_break;
        s->s_prev_break = s->prev_break;
        s->s_prev_compound = s->prev_compound;
        s->s_prev_cond = s->prev_cond;
        s->s_prev_ym = s->prev_ym_first;
        s->s_px_exit = s->px_exit;
        s->s_n = s->n;
        s->s_target_max = s->target_max;
        s->s_price_max = s->price_max;
        s->s_prev_width = s->prev_target_width;
        s->s_save_hi = s->save_hi;
        s->s_save_lo = s->save_lo;
        s->s_has = s->has_bar;
    } else {
        s->session_bars = s->s_session_bars;
        s->streak = s->s_streak;
        s->waiting = s->s_waiting;
        s->waited = s->s_waited;
        s->first_break = s->s_first;
        s->prev_break = s->s_prev_break;
        s->prev_compound = s->s_prev_compound;
        s->prev_cond = s->s_prev_cond;
        s->prev_ym_first = s->s_prev_ym;
        s->px_exit = s->s_px_exit;
        s->n = s->s_n;
        s->target_max = s->s_target_max;
        s->price_max = s->s_price_max;
        s->prev_target_width = s->s_prev_width;
        s->save_hi = s->s_save_hi;
        s->save_lo = s->s_save_lo;
        s->has_bar = s->s_has;
        if (s->n > 0) {
            s->n--;
        }
    }
    int prev_break = s->first_break;
    int prev_compound = s->prev_cond;
    int prev_ym = s->prev_ym_first;
    if (in->session_reset) {
        s->session_bars = 1;
        s->target_max = 0;
        s->price_max = 0;
        s->streak = 0;
        s->waiting = 0;
        s->waited = 0;
        s->save_hi = 0;
        s->save_lo = 0;
    } else {
        s->session_bars = s->s_session_bars + 1;
        s->target_max = s->s_target_max;
        s->price_max = s->s_price_max;
        s->streak = s->s_streak;
        s->waiting = s->s_waiting;
        s->waited = s->s_waited;
        s->save_hi = s->s_save_hi;
        s->save_lo = s->s_save_lo;
    }
    if (s->n < TR_FXSNIPER_HIST) {
        s->n++;
    }
    for (int i = s->n - 1; i > 0; i--) {
        s->hi[i] = s->hi[i - 1];
        s->lo[i] = s->lo[i - 1];
        s->cl[i] = s->cl[i - 1];
    }
    s->hi[0] = in->high;
    s->lo[0] = in->low;
    s->cl[0] = in->close;

    int n_tgt = s->cfg.n_targets;
    int tgt_ready = 1;
    for (int i = 0; i < n_tgt; i++) {
        if (in->target[i] == 0.0) {
            tgt_ready = 0;
        }
    }
    double tgt_hi = 0, tgt_lo = 0, tgt_w = 0, tgt_ratio = 0, tgt_dir = 0;
    if (tgt_ready) {
        tgt_hi = in->target[0];
        tgt_lo = in->target[0];
        for (int i = 1; i < n_tgt; i++) {
            tgt_hi = fmax(tgt_hi, in->target[i]);
            tgt_lo = fmin(tgt_lo, in->target[i]);
        }
        tgt_w = tgt_hi - tgt_lo;
        s->target_max = fmax(s->target_max, tgt_w);
        if (s->target_max > 0.0) {
            tgt_ratio = tgt_w / s->target_max * 100.0;
        }
        if (!in->session_reset && s->s_has && s->prev_target_width > 0.0) {
            if (tgt_w > s->s_prev_width) {
                tgt_dir = 1;
            } else if (tgt_w < s->s_prev_width) {
                tgt_dir = -1;
            }
        }
    }
    s->prev_target_width = tgt_w;

    int need = s->cfg.range_period > s->cfg.atr_period ? s->cfg.range_period : s->cfg.atr_period;
    int calc = s->session_bars >= need && s->n >= need;
    double px_hi = 0, px_lo = 0, px_w = 0, px_ratio = 0, atr = 0, tgt_atr = 0, px_atr = 0;
    int compressed = 0;
    if (calc) {
        px_hi = s->hi[0];
        px_lo = s->lo[0];
        for (int i = 1; i < s->cfg.range_period; i++) {
            px_hi = fmax(px_hi, s->hi[i]);
            px_lo = fmin(px_lo, s->lo[i]);
        }
        px_w = px_hi - px_lo;
        s->price_max = fmax(s->price_max, px_w);
        if (s->price_max > 0.0) {
            px_ratio = px_w / s->price_max * 100.0;
        }
        double sum = 0;
        for (int i = 0; i < s->cfg.atr_period; i++) {
            double tr = s->hi[i] - s->lo[i];
            if (s->session_bars - i > 1 && i + 1 < s->n) {
                tr = fmax(tr, fmax(fabs(s->hi[i] - s->cl[i + 1]), fabs(s->lo[i] - s->cl[i + 1])));
            }
            sum += tr;
        }
        atr = sum / (double)s->cfg.atr_period;
        if (tgt_ready && atr > 0.0) {
            tgt_atr = tgt_w / atr;
            px_atr = px_w / atr;
            if (tgt_ratio < s->cfg.target_limit && px_ratio < s->cfg.price_limit &&
                tgt_atr <= s->cfg.target_atr_cap && px_atr <= s->cfg.price_atr_cap) {
                compressed = 1;
            }
        }
    }
    int first = 0;
    int stage = 0;
    int compound = in->break_ready && in->three_ready && in->reg_ready && in->mkt_ready &&
                   in->price_ratio < 40.0 && in->three_ratio <= 30.0 && in->reg_ratio < 30.0 &&
                   in->mkt_ratio < 30.0;
    if (calc && tgt_ready && atr > 0.0) {
        s->streak = compressed ? s->streak + 1 : 0;
        double slack = fmax(0.0, s->cfg.slack_ticks) * s->cfg.price_scale;
        if (s->waiting) {
            s->waited += 1;
            if (s->waited > s->cfg.wait_bars) {
                s->waiting = 0;
            } else if (in->close > s->save_hi + slack) {
                first = 1;
                s->waiting = 0;
            } else if (in->close < s->save_lo - slack) {
                first = -1;
                s->waiting = 0;
            }
        }
        if (s->streak == s->cfg.hold_bars && s->waiting == 0 && first == 0) {
            s->waiting = 1;
            s->waited = 0;
            s->save_hi = fmax(px_hi, tgt_hi);
            s->save_lo = fmin(px_lo, tgt_lo);
        }
        if (compressed) {
            stage = 1;
        }
        if (s->waiting) {
            stage = 2;
        }
        if (first != 0) {
            stage = first * 3;
        }
    } else {
        s->streak = 0;
        s->waiting = 0;
        s->waited = 0;
    }
    s->first_break = first;
    s->prev_cond = compound;

    int score = 0;
    if (in->break_ready && in->prev_range_valid) {
        if (in->pos == -1) {
            score++;
        } else if (in->pos == 1) {
            score--;
        }
    }
    if (!in->session_reset && in->break_ready) {
        if (prev_ym == 1) {
            score--;
        } else if (prev_ym == -1) {
            score++;
        }
    }
    if (!in->session_reset) {
        if (prev_break == 1) {
            score--;
        } else if (prev_break == -1) {
            score++;
        }
    }
    int score_ex = score;
    if (in->three_ready && in->three_ratio >= 30.0) {
        if (in->below == 0) {
            score--;
        } else if (in->above == 0) {
            score++;
        }
    }
    int ratio_score = 0;
    int price_pts = in->break_ready && in->price_ratio < 40.0;
    if (price_pts) {
        ratio_score++;
    }
    if (in->reg_ready && in->reg_ratio < 30.0) {
        ratio_score++;
    }
    if (in->mkt_ready && in->mkt_ratio < 30.0) {
        ratio_score++;
    }
    if ((in->break_ready && in->price_ratio > 80.0) || (in->reg_ready && in->reg_ratio > 80.0) ||
        (in->mkt_ready && in->mkt_ratio > 80.0)) {
        ratio_score = 0;
    }
    if (price_pts) {
        ratio_score = ratio_score < 1 ? 1 : ratio_score;
    }
    s->prev_break = first;
    s->prev_compound = compound;
    s->prev_ym_first = in->first_break;

    memset(&s->out, 0, sizeof(s->out));
    s->out.calc_ready = calc && tgt_ready && atr > 0.0;
    s->out.compressed = compressed;
    s->out.stage = stage;
    s->out.first_break = prev_break;
    s->out.target_ratio = tgt_ratio;
    s->out.price_ratio = px_ratio;
    s->out.score = score;
    s->out.score_ex = score_ex;
    s->out.ratio_score = ratio_score;
    s->out.compound = prev_compound;
    s->out.rgb = score_rgb(score_ex, prev_compound, ratio_score);
    /* 가격압축이탈은 이번 봉 위치를 다음 봉에 보여 준다. 세션 첫 봉은 숨긴다. */
    s->out.px_exit = in->session_reset ? 0 : s->px_exit;
    s->px_exit = (in->break_ready && in->price_ratio < 40.0 && in->prev_range_valid) ? in->pos : 0;
    s->out.below = in->below ? 1 : 0;
    s->out.above = in->above ? 1 : 0;
    s->out.session_reset = in->session_reset ? 1 : 0;
    s->last_open = in->bar_open;
    s->has_bar = true;
    (void)tgt_dir;
}
