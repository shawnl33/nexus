#include "core/indicators/fx_entry_cand.h"

#include "core/functions/fx_decision_v2.h"
#include "core/functions/fx_session_key_v1.h"

#include <math.h>
#include <string.h>

#define RGB_(r, g, b) ((uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))

void tr_fxec_default_cfg(tr_fxec_cfg_t *cfg) {
    if (cfg == 0) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->mode = 4;
    cfg->c_grade_in = -1;
    cfg->break_ticks_in = -1;
    cfg->confirm_bars = 1;
    cfg->di_required = 1;
    cfg->expand_bars = 5;
    cfg->release_valid_bars = 10;
    cfg->release_a = 1;
    cfg->rearm_bars = 3;
    cfg->b_extra = 1;
    cfg->b_on = 1;
    cfg->entry_start = 200000;
    cfg->entry_end = 20000;
    cfg->struct_on = 1;
    cfg->struct_width = 50;
    cfg->struct_valid_bars = 10;
    cfg->struct_buy = 1;
    cfg->struct_include_release = 1;
    cfg->s_on = 1;
    cfg->s_width = 50;
    cfg->s_squeeze_bars = 30;
    cfg->s_ratio = 50;
    cfg->s_active_bars = 60;
    cfg->s_wait_bars = 2;
    cfg->block_start = 230000;
    cfg->block_bars = 60;
    cfg->r_on = 1;
    cfg->r_three = 40;
    cfg->r_swing = 50;
    cfg->r_and = 1;
    cfg->r_ratio_max = 100;
    cfg->r_rev_bars = 5;
    cfg->r_pos_on = 1;
    cfg->r_leg_min = 3;
    cfg->r_slack_ticks = 1;
    cfg->weak_window = 120;
    cfg->weak_wait = 10;
    cfg->opp_window = 90;
    cfg->opp_min = 20;
    cfg->opp_struct = 1;
    cfg->t_on = 1;
    cfg->t_start = 230000;
    cfg->t_end = 20000;
    cfg->t_quiet = 120;
    cfg->t_three = 60;
    cfg->t_ratio = 70;
    cfg->t_width = 70;
    cfg->t_pos_on = 1;
    cfg->brk_on = 1;
    cfg->brk_unified = 50;
    cfg->brk_pos_on = 1;
    cfg->brk_heat = 120;
    cfg->mark_ticks = 4;
    cfg->value_mult = 1.5;
    cfg->min_bars = 5;
    cfg->adx_period = 14;
    cfg->adx_trend = 20;
    cfg->adx_strong = 35;
    cfg->profile_mode = 1;
    cfg->width_bars = 30;
    cfg->ratio_bars = 120;
    cfg->stage1_bars = 15;
    cfg->confirm_sq_bars = 5;
    cfg->narrow_pct = 40;
    cfg->predict_ticks = 10;
}

static int in_span(int t, int a, int b) {
    if (a == b) return 1;
    if (a < b) return t >= a && t < b;
    return t >= a || t < b;
}

static int grade_size(int g) {
    if (g == 7 || g == 6 || g == 3) return 7;
    if (g == 5) return 9;
    if (g == 4) return 6;
    if (g == 2) return 3;
    return 2;
}

static uint32_t buy_rgb(int g) {
    if (g == 7) return RGB_(255, 120, 0);
    if (g == 6) return RGB_(230, 70, 0);
    if (g == 5) return RGB_(170, 0, 0);
    if (g == 4) return RGB_(200, 0, 200);
    if (g == 3) return RGB_(220, 0, 0);
    if (g == 2) return RGB_(255, 170, 170);
    return RGB_(255, 215, 215);
}

static uint32_t sell_rgb(int g) {
    if (g == 7) return RGB_(0, 120, 160);
    if (g == 6) return RGB_(0, 90, 220);
    if (g == 5) return RGB_(0, 0, 140);
    if (g == 4) return RGB_(0, 150, 150);
    if (g == 3) return RGB_(0, 0, 200);
    if (g == 2) return RGB_(150, 175, 255);
    return RGB_(205, 215, 255);
}

void tr_fxec_decide(tr_fxec_mem_t *m, const tr_fxec_mem_t *old, const tr_fxec_cfg_t *cfg,
                    const tr_fxec_bar_t *b, tr_fxec_out_t *o) {
    memset(o, 0, sizeof(*o));
    if (m == 0 || old == 0 || cfg == 0 || b == 0) return;
    *m = *old;
    int mode = cfg->mode;
    if (mode < 1) mode = 1;
    if (mode > 4) mode = 4;
    int use_c = cfg->c_grade_in;
    if (cfg->c_grade_in < 0) use_c = mode == 1 ? 1 : 0;
    double brk_ticks = cfg->break_ticks_in;
    if (cfg->break_ticks_in < 0) brk_ticks = mode == 2 ? 1 : 2;
    int conf_n = 0, di_req = 0;
    if (mode >= 3) {
        conf_n = cfg->confirm_bars;
        di_req = cfg->di_required;
    }
    int unified = b->unified;
    int dir = 0, grade = 0;
    int rel_dir = m->rel_dir, rel_kind = m->rel_kind, rel_age = m->rel_age;
    int block = m->block_dir;
    int pend_dir = m->pend_dir, pend_n = m->pend_n;
    double pend_hi = m->pend_hi, pend_lo = m->pend_lo;
    int exp_dir = m->exp_dir, exp_age = m->exp_age;
    double exp_hi = m->exp_hi, exp_lo = m->exp_lo;
    int entries = m->entries;
    if (b->cur_bar <= 1) {
        rel_age = 999;
    }
    if (b->reset) {
        rel_dir = 0; rel_kind = 0; rel_age = 999; block = 0;
        pend_dir = 0; pend_n = 0; exp_dir = 0; entries = 0;
        m->session_max_w = 0;
        m->struct_dir = 0; m->struct_age = 0;
        m->last_dir = 0; m->last_age = 0;
        m->s_wait_dir = 0; m->s_wait_age = 0; m->s_wait_px = 0;
        m->three_peak = 0;
        m->p_n = 0; m->p_prev_ok = 0;
        m->recent_release_len = 0;
        m->buy_age = 9999; m->sell_age = 9999;
        m->strong_buy_age = 9999; m->strong_sell_age = 9999;
        m->strong_buy_age2 = 9999; m->strong_sell_age2 = 9999;
        m->used_up = 0; m->used_dn = 0;
        m->weak_dir = 0; m->weak_age = 0; m->weak_grade = 0;
    } else if (b->cur_bar > 1) {
        m->last_age = old->last_age + 1;
        m->buy_age = old->buy_age + 1;
        m->sell_age = old->sell_age + 1;
        m->strong_buy_age = old->strong_buy_age + 1;
        m->strong_sell_age = old->strong_sell_age + 1;
        m->strong_buy_age2 = old->strong_buy_age2 + 1;
        m->strong_sell_age2 = old->strong_sell_age2 + 1;
        m->used_up = old->used_up;
        m->used_dn = old->used_dn;
    }
    if (!b->reset && (b->sq_release || b->sq_confirm_dir != 0) && b->sq_release_len > 0) {
        m->recent_release_len = b->sq_release_len;
    }
    int all_ok = b->fv_ok && b->up_hi > 0 && b->dn_hi > 0 && b->up_lo > 0 && b->dn_lo > 0;
    double all_hi = 0, all_lo = 0, width = 0, wpct = 0;
    if (all_ok) {
        all_hi = fmax(b->up_hi, b->dn_hi);
        all_lo = fmin(b->up_lo, b->dn_lo);
        width = all_hi - all_lo;
        if (width > m->session_max_w) m->session_max_w = width;
        if (m->session_max_w > 0) wpct = width / m->session_max_w * 100.0;
    }
    if (m->struct_dir != 0) {
        m->struct_age += 1;
        if (m->struct_age > cfg->struct_valid_bars) {
            m->struct_dir = 0;
            m->struct_age = 0;
        }
    }
    int start_dir = b->sq_confirm_dir;
    if (cfg->struct_include_release && b->sq_release_dir != 0) start_dir = b->sq_release_dir;
    if (start_dir != 0 && !b->reset) {
        m->struct_dir = start_dir;
        m->struct_age = 0;
    }
    /* V3 A/B/C. 원본은 이 결과를 먼저 두고, 없을 때만 구조를 얹는다. */
    rel_age += 1;
    if (mode >= 2 && b->box_dir != 0) {
        rel_dir = b->box_dir; rel_kind = 1; rel_age = 0;
    } else if (b->sq_release_dir != 0) {
        rel_dir = b->sq_release_dir; rel_kind = 1; rel_age = 0;
    } else if (b->sq_confirm_dir != 0) {
        rel_dir = b->sq_confirm_dir; rel_kind = 2; rel_age = 0;
    }
    if (block == 1 && unified <= 0) block = 0;
    if (block == -1 && unified >= 0) block = 0;
    if (!b->reset) {
        int confirmed = 0, a_dir = 0;
        if (mode >= 2) {
            if (pend_dir != 0) {
                if ((pend_dir == 1 && b->close > pend_hi) || (pend_dir == -1 && b->close < pend_lo)) {
                    pend_n += 1;
                    if (pend_n >= conf_n) { confirmed = pend_dir; pend_dir = 0; }
                } else { pend_dir = 0; pend_n = 0; }
            }
            if (b->box_dir != 0) {
                pend_hi = b->box_hi; pend_lo = b->box_lo;
                if (conf_n <= 0) confirmed = b->box_dir;
                else { pend_dir = b->box_dir; pend_n = 0; }
            }
            if (mode == 4) {
                if (exp_dir != 0) {
                    exp_age += 1;
                    if ((exp_dir == 1 && b->close <= exp_hi) || (exp_dir == -1 && b->close >= exp_lo)) exp_dir = 0;
                    else if (b->sq_hold == 0) { a_dir = exp_dir; exp_dir = 0; }
                    else if (exp_age >= cfg->expand_bars) exp_dir = 0;
                }
                if (confirmed != 0) {
                    if (b->sq_hold == 0) a_dir = confirmed;
                    else { exp_dir = confirmed; exp_age = 0; exp_hi = pend_hi; exp_lo = pend_lo; }
                }
            } else a_dir = confirmed;
            if (cfg->release_a && a_dir == 0 && b->sq_release_dir != 0) a_dir = b->sq_release_dir;
            int di_ok = 1;
            if (di_req) {
                di_ok = 0;
                if (b->adx_valid && a_dir == 1 && b->plus_di > b->minus_di) di_ok = 1;
                if (b->adx_valid && a_dir == -1 && b->minus_di > b->plus_di) di_ok = 1;
            }
            double need = 50;
            if ((a_dir == 1 && b->future_dir == 1 && unified >= need && di_ok && block != 1) ||
                (a_dir == -1 && b->future_dir == -1 && unified <= -need && di_ok && block != -1)) {
                dir = a_dir; grade = 3;
            }
        }
        if (dir == 0 && cfg->b_on) {
            int trig = 0;
            if (unified == 100 && old->uni[0] != 100) trig = 1;
            else if (unified == -100 && old->uni[0] != -100) trig = -1;
            else if ((mode == 1 || cfg->b_extra) && unified == 100 && rel_age == 0 && rel_dir == 1) trig = 1;
            else if ((mode == 1 || cfg->b_extra) && unified == -100 && rel_age == 0 && rel_dir == -1) trig = -1;
            if (trig != 0 && block != trig) {
                if (rel_dir != 0 && rel_age <= cfg->release_valid_bars) {
                    if (rel_dir == trig) {
                        dir = trig; grade = (mode == 1 && rel_kind == 1) ? 3 : 2;
                    }
                } else if (use_c) { dir = trig; grade = 1; }
            }
        }
        if (dir != 0 && !in_span(b->time_hms, cfg->entry_start, cfg->entry_end)) {
            dir = 0; grade = 0;
        }
        if (dir != 0) { block = dir; entries += 1; }
    }
    int struct_dir = 0;
    if (dir == 0 && cfg->struct_on && b->cur_bar > 2 && !b->reset && m->struct_dir != 0 &&
        old->sok == 1 && old->sok1 == 1 && old->wpct < cfg->struct_width) {
        if (m->struct_dir == -1 && b->close < old->alo && old->close1 >= old->alo) struct_dir = -1;
        if (m->struct_dir == 1 && cfg->struct_buy && b->close > old->ahi && old->close1 <= old->ahi) struct_dir = 1;
    }
    if (dir == 0 && struct_dir != 0 && struct_dir != m->last_dir) {
        dir = struct_dir;
        grade = 4;
    }

    int three_ok = 0;
    double three_gap = 0, three_ratio = 0;
    if (b->v1_ok && b->tgt[0] != 0 && b->tgt[1] != 0 && b->tgt[2] != 0) {
        three_ok = 1;
        double hi = fmax(b->tgt[0], fmax(b->tgt[1], b->tgt[2]));
        double lo = fmin(b->tgt[0], fmin(b->tgt[1], b->tgt[2]));
        three_gap = hi - lo;
        m->three_peak = fmax(m->three_peak, three_gap);
        if (m->three_peak > 0) three_ratio = three_gap / m->three_peak * 100.0;
    }
    int p_pos = 0;
    int p_n = m->p_n, p_ok = m->p_prev_ok;
    double p_hi = m->p_hi, p_lo = m->p_lo, p_phi = m->p_prev_hi, p_plo = m->p_prev_lo;
    if (b->reset || !three_ok) {
        p_n = 0; p_ok = 0; p_hi = 0; p_lo = 0; p_phi = 0; p_plo = 0;
    } else if (three_ok) {
        if (!old->three_ok) {
            p_n = 1; p_hi = b->high; p_lo = b->low; p_ok = 0; p_phi = 0; p_plo = 0;
        } else if (three_gap != old->three_gap && old->p_n >= (cfg->r_leg_min > 1 ? cfg->r_leg_min : 1)) {
            p_phi = old->p_hi; p_plo = old->p_lo; p_ok = 1;
            p_n = 1; p_hi = b->high; p_lo = b->low;
        } else {
            p_n = old->p_n + 1;
            p_hi = fmax(old->p_hi, b->high);
            p_lo = fmin(old->p_lo, b->low);
        }
        if (p_ok) {
            double slack = fmax(0.0, cfg->r_slack_ticks) * (b->price_scale > 0 ? b->price_scale : 1);
            if (b->close > p_phi + slack) p_pos = 1;
            else if (b->close < p_plo - slack) p_pos = -1;
        }
    }
    m->p_n = p_n; m->p_prev_ok = p_ok; m->p_hi = p_hi; m->p_lo = p_lo;
    m->p_prev_hi = p_phi; m->p_prev_lo = p_plo;

    int blue = 0, red = 0;
    if (b->up_hi > 0 && b->dn_hi > 0 && b->up_lo > 0 && b->dn_lo > 0) {
        if (b->up_hi < b->dn_hi && b->dn_lo < b->up_lo) blue = 1;
        if (b->up_hi > b->dn_hi && b->up_lo < b->dn_lo) red = 1;
    }
    int t_dir = 0;
    if (cfg->t_on && b->cur_bar > 2 && !b->reset && in_span(b->time_hms, cfg->t_start, cfg->t_end) &&
        m->buy_age >= cfg->t_quiet && m->sell_age >= cfg->t_quiet &&
        old->three_ok == 1 && old->three_ratio < cfg->t_three &&
        old->sq_ratio1 < cfg->t_ratio && old->sok == 1 && old->wpct < cfg->t_width) {
        if (old->blue == 1 && b->close < old->dn_lo && old->close1 >= old->dn_lo &&
            unified == -100 && (cfg->t_pos_on != 1 || p_pos == -1)) t_dir = -1;
        if (old->red == 1 && b->close > old->up_hi && old->close1 <= old->up_hi &&
            unified == 100 && (cfg->t_pos_on != 1 || p_pos == 1)) t_dir = 1;
    }
    if (t_dir != 0) { dir = t_dir; grade = 7; }

    /* 등급 8. S·R 이 뒤에서 덮어쓴다. 경과2 초기값은 9999 라 첫 강신호 전에는 과열 제한이 열리지 않는다. */
    int g8_dir = 0;
    double g8_px = 0;
    if (cfg->brk_on && b->cur_bar > 2 && !b->reset) {
        int up_struct = old->up_hi > 0 && old->dn_hi > 0 && old->up_lo > 0 && old->dn_lo > 0 &&
                        old->up_382 > old->dn_hi;
        int dn_struct = old->up_hi > 0 && old->dn_hi > 0 && old->up_lo > 0 && old->dn_lo > 0 &&
                        old->dn_618 < old->up_lo;
        if (up_struct && b->close > old->up_hi && old->close1 <= old->up_hi &&
            unified >= cfg->brk_unified && (cfg->brk_pos_on != 1 || p_pos == 1) &&
            old->up_hi != m->used_up && (cfg->brk_heat <= 0 || m->strong_buy_age2 >= cfg->brk_heat)) {
            g8_dir = 1;
            g8_px = old->up_hi;
        }
        if (dn_struct && b->close < old->dn_lo && old->close1 >= old->dn_lo &&
            unified <= -cfg->brk_unified && (cfg->brk_pos_on != 1 || p_pos == -1) &&
            old->dn_lo != m->used_dn && (cfg->brk_heat <= 0 || m->strong_sell_age2 >= cfg->brk_heat)) {
            g8_dir = -1;
            g8_px = old->dn_lo;
        }
    }
    if (g8_dir != 0) { dir = g8_dir; grade = 8; }

    int diag = -1, r_dir = 0, r_eval = 0;
    if (!b->reset && b->cur_bar > 2) {
        if (unified == -100 && old->uni[0] != -100) r_eval = -1;
        if (unified == 100 && old->uni[0] != 100) r_eval = 1;
    }
    if (cfg->r_on && r_eval != 0) {
        int opp = 0, found = 0;
        int win = cfg->r_rev_bars;
        if (win > b->bars - 1) win = b->bars - 1;
        if (win > TR_FXEC_HIST) win = TR_FXEC_HIST;
        for (int k = 0; k < win && !found; k++) {
            int u = old->uni[k];
            if (u == 100 || u == -100) {
                found = 1;
                if (u == -100 * r_eval) opp = 1;
            }
        }
        int three_pass = old->three_ok == 1 && old->three_ratio < cfg->r_three;
        int swing_pass = old->sok == 1 && old->wpct < cfg->r_swing;
        if (!opp) diag = 1;
        else if (cfg->r_pos_on && p_pos != r_eval) diag = 8;
        else if (!b->profile_valid || (r_eval == -1 && b->close >= b->center) || (r_eval == 1 && b->close <= b->center)) diag = 2;
        else if (!b->adx_valid || b->future_dir != r_eval ||
                 (r_eval == -1 && b->minus_di <= b->plus_di) || (r_eval == 1 && b->plus_di <= b->minus_di)) diag = 3;
        else if (!three_pass && (cfg->r_and == 1 || !swing_pass)) diag = 4;
        else if (cfg->r_and == 1 && !swing_pass) diag = 5;
        else if (b->sq_ratio >= cfg->r_ratio_max) diag = 6;
        else { diag = 0; r_dir = r_eval; }
    }
    if (r_dir != 0) { dir = r_dir; grade = 6; }

    int s_dir = 0;
    if (m->s_wait_dir != 0) {
        m->s_wait_age += 1;
        if ((m->s_wait_dir == 1 && b->close <= m->s_wait_px) || (m->s_wait_dir == -1 && b->close >= m->s_wait_px)) {
            m->s_wait_dir = 0; m->s_wait_age = 0; m->s_wait_px = 0;
        } else if (b->sq_hold == 0) {
            s_dir = m->s_wait_dir;
            m->s_wait_dir = 0; m->s_wait_age = 0; m->s_wait_px = 0;
        } else if (m->s_wait_age >= cfg->s_wait_bars) {
            m->s_wait_dir = 0; m->s_wait_age = 0; m->s_wait_px = 0;
        }
    }
    int s_break = 0;
    if (cfg->s_on && b->cur_bar > 2 && !b->reset && old->sok == 1 && old->sok1 == 1 &&
        old->wpct < cfg->s_width && old->sq_hold1 >= cfg->s_squeeze_bars && b->sq_ratio < cfg->s_ratio) {
        if (b->close > old->ahi && old->close1 <= old->ahi) s_break = 1;
        if (b->close < old->alo && old->close1 >= old->alo) s_break = -1;
    }
    if (s_break != 0) {
        if (b->sq_hold == 0 || b->sq_hold >= cfg->s_active_bars) s_dir = s_break;
        else if (cfg->s_wait_bars > 0) {
            m->s_wait_dir = s_break; m->s_wait_age = 0;
            m->s_wait_px = s_break == 1 ? old->ahi : old->alo;
        }
    }
    if (s_dir != 0) { dir = s_dir; grade = 5; }

    double basis_len = b->sq_hold > 0 ? b->sq_hold : m->recent_release_len;
    if (cfg->opp_min > 0 && dir != 0 && (grade == 3 || (grade == 4 && cfg->opp_struct)) &&
        ((dir == -1 && m->buy_age < cfg->opp_window) || (dir == 1 && m->sell_age < cfg->opp_window)) &&
        basis_len < cfg->opp_min) {
        dir = 0; grade = 0;
    }

    int weak_ok = 0;
    if (m->weak_dir != 0) {
        m->weak_age += 1;
        if (unified == -100 * m->weak_dir) {
            m->weak_dir = 0; m->weak_age = 0; m->weak_grade = 0;
        } else if (unified == 100 * m->weak_dir) weak_ok = 1;
        else if (m->weak_age >= cfg->weak_wait) {
            m->weak_dir = 0; m->weak_age = 0; m->weak_grade = 0;
        }
    }
    if (m->weak_dir != 0 && dir != 0) {
        m->weak_dir = 0; m->weak_age = 0; m->weak_grade = 0; weak_ok = 0;
    } else if (weak_ok) {
        dir = m->weak_dir; grade = m->weak_grade;
        m->weak_dir = 0; m->weak_age = 0; m->weak_grade = 0;
    }
    if (cfg->weak_window > 0 && !weak_ok && dir != 0 && (grade == 1 || grade == 2) &&
        ((dir == -1 && m->strong_buy_age < cfg->weak_window) || (dir == 1 && m->strong_sell_age < cfg->weak_window))) {
        if (cfg->weak_wait > 0) {
            m->weak_dir = dir; m->weak_age = 0; m->weak_grade = grade;
        }
        dir = 0; grade = 0;
    }

    (void)brk_ticks;
    if (!in_span(b->time_hms, cfg->entry_start, cfg->entry_end) && dir != 0) {
        dir = 0; grade = 0;
    }
    int block_time = 0;
    if (cfg->block_start >= 70000) block_time = b->time_hms >= cfg->block_start || b->time_hms < 70000;
    else block_time = b->time_hms >= cfg->block_start && b->time_hms < 70000;
    if (dir != 0 && cfg->block_bars > 0 && block_time && m->last_dir == -dir && m->last_age < cfg->block_bars) {
        dir = 0; grade = 0;
    }
    if (dir != 0) {
        m->last_dir = dir; m->last_age = 0;
        if (dir == 1) m->buy_age = 0;
        if (dir == -1) m->sell_age = 0;
        if (grade >= 3 && dir == 1) {
            m->strong_buy_age2 = m->strong_buy_age;
            m->strong_buy_age = 0;
        }
        if (grade >= 3 && dir == -1) {
            m->strong_sell_age2 = m->strong_sell_age;
            m->strong_sell_age = 0;
        }
        if (grade == 8 && dir == 1) m->used_up = g8_px;
        if (grade == 8 && dir == -1) m->used_dn = g8_px;
        if (grade == 4) { m->struct_dir = 0; m->struct_age = 0; }
    }
    if (diag == 0 && grade != 6) diag = 7;

    for (int i = TR_FXEC_HIST - 1; i > 0; i--) m->uni[i] = old->uni[i - 1];
    m->uni[0] = unified;
    m->sok1 = old->sok; m->sok = all_ok;
    m->wpct1 = old->wpct; m->wpct = wpct;
    m->ahi1 = old->ahi; m->ahi = all_hi;
    m->alo1 = old->alo; m->alo = all_lo;
    m->dn_lo1 = old->dn_lo; m->dn_lo = b->dn_lo;
    m->dn_hi1 = old->dn_hi; m->dn_hi = b->dn_hi;
    m->up_lo1 = old->up_lo; m->up_lo = b->up_lo;
    m->up_3821 = old->up_382; m->up_382 = b->up_382;
    m->dn_6181 = old->dn_618; m->dn_618 = b->dn_618;
    m->up_hi1 = old->up_hi; m->up_hi = b->up_hi;
    m->blue1 = old->blue; m->blue = blue;
    m->red1 = old->red; m->red = red;
    m->three_ok1 = old->three_ok; m->three_ok = three_ok;
    m->three_ratio1 = old->three_ratio; m->three_ratio = three_ratio;
    m->three_gap1 = old->three_gap; m->three_gap = three_gap;
    m->sq_ratio1 = b->sq_ratio;
    m->sq_hold1 = b->sq_hold;
    m->close1 = b->close;
    m->rel_dir = rel_dir; m->rel_kind = rel_kind; m->rel_age = rel_age;
    m->block_dir = block; m->entries = entries;
    m->pend_dir = pend_dir; m->pend_n = pend_n; m->pend_hi = pend_hi; m->pend_lo = pend_lo;
    m->exp_dir = exp_dir; m->exp_age = exp_age; m->exp_hi = exp_hi; m->exp_lo = exp_lo;

    o->dir = dir;
    o->grade = grade;
    o->unified = unified;
    o->diag = diag;
    if (dir == 0) return;
    double ps = b->price_scale > 0 ? b->price_scale : 1;
    o->price = dir == 1 ? b->low - cfg->mark_ticks * ps : b->high + cfg->mark_ticks * ps;
    o->size = grade_size(grade);
    o->rgb = dir == 1 ? buy_rgb(grade) : sell_rgb(grade);
    if (o->size >= 5) {
        o->emph = 1;
        o->emph_rgb = dir == 1 ? RGB_(255, 255, 0) : RGB_(0, 255, 255);
    }
}

bool tr_fxec_init(tr_fxec_t *s, double price_scale) {
    if (s == 0 || price_scale <= 0.0) return false;
    memset(s, 0, sizeof(*s));
    s->price_scale = price_scale;
    tr_fxec_default_cfg(&s->cfg);
    s->st.rel_age = 999;
    s->st.buy_age = s->st.sell_age = s->st.strong_buy_age = s->st.strong_sell_age = 9999;
    s->st.strong_buy_age2 = s->st.strong_sell_age2 = 9999;
    s->prev = s->st;
    if (!tr_fxc_init(&s->curve)) return false;
    if (!tr_fxadx_init(&s->adx, s->cfg.adx_period)) return false;
    tr_fxprof_init(&s->profile);
    tr_fxsq_init(&s->squeeze_v1);
    tr_fxsq_init(&s->squeeze_v4);
    return true;
}

bool tr_fxec_relink(tr_fxec_t *s) {
    return s != 0 && tr_fxc_relink(&s->curve);
}

void tr_fxec_use_cfg(tr_fxec_t *s, const tr_fxec_cfg_t *cfg) {
    if (s == 0 || cfg == 0) {
        return;
    }
    s->cfg = *cfg;
    if (s->cfg.adx_period < 2) {
        s->cfg.adx_period = 2;
    }
    tr_fxadx_init(&s->adx, s->cfg.adx_period);
}

static void sq_in(tr_fxsq_input_t *q, const tr_fxec_t *s, const tr_fxec_input_t *in, int reset, double ticks, int rearm) {
    memset(q, 0, sizeof(*q));
    q->session_reset = reset;
    q->is_new_bar = in->is_new_bar;
    q->bar_open = in->bar_open;
    q->high = in->high;
    q->low = in->low;
    q->close = in->close;
    q->volume = in->volume;
    q->price_scale = s->price_scale;
    q->value_mult = s->cfg.value_mult;
    q->min_bars = s->cfg.min_bars;
    q->width_bars = s->cfg.width_bars;
    q->ratio_bars = s->cfg.ratio_bars;
    q->narrow_pct = s->cfg.narrow_pct;
    q->stage1_bars = s->cfg.stage1_bars;
    q->confirm_bars = s->cfg.confirm_sq_bars;
    q->confirm_closed = s->cfg.confirm_closed;
    q->break_ticks = ticks;
    q->rearm_bars = rearm;
}

void tr_fxec_eval(tr_fxec_t *s, const tr_fxec_input_t *in) {
    if (s == 0 || in == 0) return;
    memset(&s->out, 0, sizeof(s->out));
    bool is_new = !s->has_bar || in->bar_open != s->last_open;
    if (is_new) {
        s->prev = s->st;
        s->has_bar = true;
        s->last_open = in->bar_open;
        s->evals += 1;
        int64_t key = tr_fx_session_key_v1(in->date, in->time);
        s->reset_bar = !s->has_key || key != s->key;
        s->has_key = true;
        s->key = key;
        s->session_bars = s->reset_bar ? 1 : s->session_bars + 1;
    }
    int reset = s->reset_bar;
    tr_fxc_input_t cin;
    memset(&cin, 0, sizeof(cin));
    cin.session_reset = reset;
    cin.session_bars = s->session_bars;
    cin.high = in->high;
    cin.low = in->low;
    cin.ticks = s->cfg.predict_ticks;
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
    pin.value_mult = s->cfg.value_mult;
    pin.min_bars = s->cfg.min_bars;
    tr_fxprof_eval(&s->profile, &pin);
    int fut = 0;
    if (!reset) fut = s->curve.direction > 0 ? 1 : (s->curve.direction < 0 ? -1 : 0);
    tr_fxdec_input_t din;
    memset(&din, 0, sizeof(din));
    din.future_dir = fut;
    din.state5 = s->profile.out.state5;
    din.profile_valid = s->profile.out.valid;
    din.center = s->profile.out.center;
    din.close = in->close;
    din.mode = s->cfg.profile_mode;
    din.adx = s->adx.adx;
    din.plus_di = s->adx.plus_di;
    din.minus_di = s->adx.minus_di;
    din.adx_valid = s->adx.valid;
    din.adx_trend = s->cfg.adx_trend;
    din.adx_strong = s->cfg.adx_strong;
    tr_fxdec_out_t dec = tr_fxdec_eval(&din);
    tr_fxsq_input_t q1, q4;
    sq_in(&q1, s, in, reset, 0, 0);
    double ticks = s->cfg.break_ticks_in < 0 ? 2 : s->cfg.break_ticks_in;
    sq_in(&q4, s, in, reset, ticks, s->cfg.rearm_bars);
    tr_fxsq_eval(&s->squeeze_v1, &q1);
    tr_fxsq_eval(&s->squeeze_v4, &q4);
    const tr_fxfv_output_t *fv = in->fv;
    tr_fxec_bar_t bar;
    memset(&bar, 0, sizeof(bar));
    bar.reset = reset;
    bar.bars = s->session_bars;
    bar.cur_bar = s->evals;
    bar.time_hms = (int)(in->time % 1000000);
    bar.high = in->high;
    bar.low = in->low;
    bar.close = in->close;
    bar.price_scale = s->price_scale;
    bar.unified = dec.unified;
    bar.future_dir = fut;
    bar.adx_valid = s->adx.valid;
    bar.plus_di = s->adx.plus_di;
    bar.minus_di = s->adx.minus_di;
    bar.profile_valid = s->profile.out.valid;
    bar.center = s->profile.out.center;
    bar.sq_hold = s->squeeze_v1.out.hold;
    bar.sq_ratio = s->squeeze_v1.out.ratio;
    bar.sq_release = s->squeeze_v1.out.release;
    bar.sq_release_dir = s->squeeze_v1.out.release_dir;
    bar.sq_confirm_dir = s->squeeze_v1.out.confirm_dir;
    bar.sq_release_len = s->squeeze_v1.out.release_len;
    bar.box_dir = s->squeeze_v4.out.box_dir;
    bar.box_hi = s->squeeze_v4.out.box_hi;
    bar.box_lo = s->squeeze_v4.out.box_lo;
    if (fv != 0) {
        bar.fv_ok = 1;
        bar.v1_ok = 1;
        bar.up_hi = fv->lup_high;
        bar.up_lo = fv->lup_low;
        bar.up_382 = fv->lup_382;
        bar.dn_hi = fv->ldn_high;
        bar.dn_lo = fv->ldn_low;
        bar.dn_618 = fv->ldn_618;
        bar.tgt[0] = fv->persist_target[0];
        bar.tgt[1] = fv->persist_target[1];
        bar.tgt[2] = fv->persist_target[2];
    }
    tr_fxec_decide(&s->st, &s->prev, &s->cfg, &bar, &s->out);
}
