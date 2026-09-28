#include "core/indicators/memory_lines.h"

#include <math.h>
#include <string.h>

static double quantize(double v, double price_scale) {
    if (price_scale <= 0.0) {
        return v;
    }
    return round(v / price_scale) * price_scale;
}

/* 5봉 연속 이탈 검사 공통부 */
static bool five_below(const double *h5, size_t n, double level) {
    if (h5 == 0 || n < 5) {
        return false;
    }
    for (size_t i = 0; i < 5; i++) {
        if (h5[i] >= level) {
            return false;
        }
    }
    return true;
}

static bool five_above(const double *l5, size_t n, double level) {
    if (l5 == 0 || n < 5) {
        return false;
    }
    for (size_t i = 0; i < 5; i++) {
        if (l5[i] <= level) {
            return false;
        }
    }
    return true;
}

/* ---------- 회귀기억 ---------- */

void tr_regmem_init(tr_regmem_t *s, const tr_regmem_config_t *cfg) {
    memset(s, 0, sizeof(*s));
    if (cfg != 0) {
        s->cfg = *cfg;
    }
}

void tr_regmem_on_bar(tr_regmem_t *s, const tr_regmem_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    /* 회귀기본방향 (원본 559~570) */
    int base_dir = 0;
    if (in->final_dir_valid && in->final_dir != 0) {
        base_dir = in->final_dir;
    }
    if (base_dir == 0 && in->reg_valid && in->line_sign > 0) {
        base_dir = 1;
    }
    if (base_dir == 0 && in->reg_valid && in->line_sign < 0) {
        base_dir = -1;
    }

    s->current_dir = base_dir;
    double slope_ticks = s->cfg.price_scale > 0.0 ? fabs(in->slope) / s->cfg.price_scale : 0.0;
    int needed = 1;

    /* 호가 관성 (원본 578~607) */
    if (in->compress_min && in->ob_applicable && s->mem_valid &&
        s->mem_session == in->session_no && base_dir != 0 && base_dir != s->mem_dir) {
        if (in->ob_state < 2 && in->ob_state > -2) {
            needed = 2;
        }
        if (slope_ticks >= s->cfg.steep_slope_ticks) {
            needed += s->cfg.steep_extra_bars;
        }
        if (in->ob_state * base_dir > 0) {
            s->confirm_accum++;
        } else {
            s->confirm_accum = 0;
        }
        if (s->confirm_accum < needed) {
            s->current_dir = s->mem_dir;
        }
    } else {
        s->confirm_accum = 0;
    }

    /* 세션 리셋 (원본 609~626) */
    if (in->compress_min_le30 && in->session_no != s->mem_session) {
        s->mem_dir = 0;
        s->mem_valid = false;
        s->mem_price = 0.0;
        memset(s->mem_target, 0, sizeof(s->mem_target));
        memset(s->mem_upper, 0, sizeof(s->mem_upper));
        memset(s->mem_lower, 0, sizeof(s->mem_lower));
        s->confirm_accum = 0;
        s->mem_session = in->session_no;
    }

    /* 저장 (원본 628~647) */
    s->updated = false;
    if (in->reg_valid && s->current_dir != 0 &&
        in->pred_price[0] != 0.0 && in->pred_price[1] != 0.0 && in->pred_price[2] != 0.0 &&
        (!s->mem_valid || s->current_dir != s->mem_dir)) {
        s->mem_price = in->line_flat;
        for (int k = 0; k < 3; k++) {
            s->mem_target[k] = quantize(in->pred_price[k], s->cfg.price_scale);
            s->mem_upper[k] = quantize(in->upper[k], s->cfg.price_scale);
            s->mem_lower[k] = quantize(in->lower[k], s->cfg.price_scale);
        }
        s->mem_dir = s->current_dir;
        s->mem_valid = true;
        s->updated = true;
    }

    /* 표시 (원본 649~711) */
    s->show_targets = s->mem_valid;
    bool range_ok = s->mem_valid && s->cfg.show_range && !s->updated;
    s->show_upper = range_ok && !five_below(in->h5, in->hl_count, s->mem_target[2]);
    s->show_lower = range_ok && !five_above(in->l5, in->hl_count, s->mem_target[2]);
}

/* ---------- 지속선 ---------- */

void tr_persist_init(tr_persist_t *s, const tr_persist_config_t *cfg) {
    memset(s, 0, sizeof(*s));
    if (cfg != 0) {
        s->cfg = *cfg;
    }
}

void tr_persist_on_bar(tr_persist_t *s, const tr_persist_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    if (!s->started) {
        /* CurrentBar == 1 리셋 (원본 723~732) */
        s->started = true;
        s->streak = 0;
        s->prev_dir2 = 0;
        s->saved_valid = false;
        s->saved_dir = 0;
        memset(s->target, 0, sizeof(s->target));
        memset(s->upper, 0, sizeof(s->upper));
        memset(s->lower, 0, sizeof(s->lower));
    } else {
        /* 연속 봉수 (원본 742~745) */
        if (in->pred_dir2 == s->prev_dir2 && in->pred_dir2 != 0) {
            s->streak++;
        } else {
            s->streak = 1;
        }
    }

    /* 저장: 정확히 Max(1,지속봉수) 봉째 그 봉에만 (원본 748~762) */
    int32_t need = s->cfg.persist_bars > 1 ? s->cfg.persist_bars : 1;
    if (in->reg_valid && in->r2 >= s->cfg.min_r2 && in->pred_dir2 != 0 && s->streak == need) {
        s->saved_valid = true;
        s->saved_dir = in->pred_dir2;
        for (int k = 0; k < 3; k++) {
            s->target[k] = quantize(in->pred_price[k], s->cfg.price_scale);
            s->upper[k] = quantize(in->upper[k], s->cfg.price_scale);
            s->lower[k] = quantize(in->lower[k], s->cfg.price_scale);
        }
    }
    s->prev_dir2 = in->pred_dir2;

    /* 표시 (원본 764~799) */
    if (s->saved_valid) {
        s->show_upper = !five_below(in->h5, in->hl_count, s->target[2]);
        s->show_lower = !five_above(in->l5, in->hl_count, s->target[2]);
    } else {
        s->show_upper = false;
        s->show_lower = false;
    }
}
