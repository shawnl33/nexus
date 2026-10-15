#include "core/indicators/fx_os_filter.h"

#include "core/model/civil_time.h"

#include <stddef.h>
#include <string.h>

void tr_fxos_filter_cfg_default(tr_fxos_filter_cfg_t *cfg) {
    if (cfg == 0) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->dst_auto = 1;
    cfg->rth_manual = 223000;
    cfg->rth_summer = 223000;
    cfg->rth_winter = 233000;
    cfg->pre_min = 5;
    cfg->adj_sell_allow = 60;
    cfg->s_buy_min = 150;
    cfg->s_sell_min = 150;
    cfg->b_three_max = 30;
    cfg->b_two_max = 60;
    cfg->b_buy_pre = 1;
    cfg->sell_pos_lim = -10;
    cfg->sell_width_lim = 100;
    cfg->sell_engulf = 1;
    cfg->brk_gap = 5;
    cfg->sell_engulf2 = 1;
    cfg->sell_two_min = 99;
    cfg->tall_on = 1;
    cfg->tall_amp = 80;
    cfg->adj_prev_up = 30;
    cfg->adj_sell_swing = 90;
    cfg->adj_buy_swing = 100;
    cfg->adj_slope = -20;
    cfg->adj_engulf = 1;
    cfg->adj_engulf_dn = 1;
    cfg->adj_engulf3 = 1;
    cfg->adj_flat_on = 1;
    cfg->adj_two_max = 90;
    cfg->adj_flat_max = 90;
    cfg->turn_buy_on = 1;
    cfg->turn_sell_on = 1;
    cfg->turn_pos_lo = -3;
    cfg->turn_swing_max = 50;
    cfg->turn_two_max = 90;
    cfg->turn_excl = 1;
    cfg->turn_sq_lim = 60;
    cfg->turn_gray = 1;
    cfg->turn_color_n = 10;
    cfg->turn_opp_bars = 10;
    cfg->turn_sell_u = 1;
    cfg->turn_sell_prev = 10;
    cfg->turn_buy_prev = 10;
    cfg->adj_a_mid_block = 1;
}

static int clock_min(int hms) {
    return (hms / 10000) * 60 + (hms / 100) % 100;
}

void tr_fxos_tall_flags(const double *open, const double *high, const double *low, const double *close,
                        int avg_n, int confirm_n, double mult, double wick_pct, double body_pct,
                        int *buy_flag, int *sell_flag) {
    double avg = 0;
    int k;
    if (buy_flag != 0) {
        *buy_flag = 0;
    }
    if (sell_flag != 0) {
        *sell_flag = 0;
    }
    if (open == 0 || high == 0 || low == 0 || close == 0 || avg_n <= 0 || confirm_n <= 0) {
        return;
    }
    for (k = confirm_n; k < confirm_n + avg_n; k++) {
        avg += high[k] - low[k];
    }
    avg /= avg_n;
    if (avg <= 0) {
        return;
    }
    for (k = 0; k < confirm_n; k++) {
        double range = high[k] - low[k];
        double body_hi = open[k] > close[k] ? open[k] : close[k];
        double body_lo = open[k] < close[k] ? open[k] : close[k];
        if (range < avg * mult) {
            continue;
        }
        if (buy_flag != 0 && (high[k] - body_hi >= range * wick_pct / 100.0 ||
                              close[k] - open[k] >= range * body_pct / 100.0)) {
            *buy_flag = 1;
        }
        if (sell_flag != 0 && (body_lo - low[k] >= range * wick_pct / 100.0 ||
                               open[k] - close[k] >= range * body_pct / 100.0)) {
            *sell_flag = 1;
        }
    }
}

static int rise_wraps_fall(const tr_fxos_filter_in_t *in) {
    return in->rise_hi > 0 && in->rise_lo > 0 && in->fall_hi > 0 && in->fall_lo > 0 &&
           in->fall_hi <= in->rise_hi && in->fall_lo >= in->rise_lo;
}

static int fall_wraps_rise(const tr_fxos_filter_in_t *in) {
    return in->rise_hi > 0 && in->rise_lo > 0 && in->fall_hi > 0 && in->fall_lo > 0 &&
           in->fall_hi >= in->rise_hi && in->fall_lo <= in->rise_lo;
}

static void drop_preopen(int pre, int unified, int *dir) {
    if (!pre || dir == 0 || *dir == 0) {
        return;
    }
    if (*dir == 1 && unified != 100) {
        *dir = 0;
    }
    if (*dir == -1 && unified != -100) {
        *dir = 0;
    }
}

int tr_fxos_filter_cfg_set(tr_fxos_filter_cfg_t *cfg, const char *key, double value) {
    /* 필터가 실제로 읽는 키만. 장대봉 배율·꼬리와 조정A중하락구조금지는 여기 없다. */
    static const struct {
        const char *key;
        size_t off;
    } keys[] = {
        {"서머타임자동", offsetof(tr_fxos_filter_cfg_t, dst_auto)},
        {"본장시작시각", offsetof(tr_fxos_filter_cfg_t, rth_manual)},
        {"서머타임본장", offsetof(tr_fxos_filter_cfg_t, rth_summer)},
        {"표준시본장", offsetof(tr_fxos_filter_cfg_t, rth_winter)},
        {"본장전제한분", offsetof(tr_fxos_filter_cfg_t, pre_min)},
        {"조정매도허용분", offsetof(tr_fxos_filter_cfg_t, adj_sell_allow)},
        {"후보S매수허용분", offsetof(tr_fxos_filter_cfg_t, s_buy_min)},
        {"후보S매도허용분", offsetof(tr_fxos_filter_cfg_t, s_sell_min)},
        {"후보S매수압축중금지", offsetof(tr_fxos_filter_cfg_t, s_buy_sq)},
        {"후보B삼선상한", offsetof(tr_fxos_filter_cfg_t, b_three_max)},
        {"후보B두구간상한", offsetof(tr_fxos_filter_cfg_t, b_two_max)},
        {"후보B매수본장전금지", offsetof(tr_fxos_filter_cfg_t, b_buy_pre)},
        {"후보매도위치기준", offsetof(tr_fxos_filter_cfg_t, sell_pos_lim)},
        {"후보매도폭비율기준", offsetof(tr_fxos_filter_cfg_t, sell_width_lim)},
        {"후보매도감싸기통합", offsetof(tr_fxos_filter_cfg_t, sell_engulf)},
        {"돌파매도목표5여유", offsetof(tr_fxos_filter_cfg_t, brk_gap)},
        {"후보매도감싸기삼선두구간", offsetof(tr_fxos_filter_cfg_t, sell_engulf2)},
        {"후보매도두구간기준", offsetof(tr_fxos_filter_cfg_t, sell_two_min)},
        {"장대봉필터", offsetof(tr_fxos_filter_cfg_t, tall_on)},
        {"장대진폭율기준", offsetof(tr_fxos_filter_cfg_t, tall_amp)},
        {"조정매수직전상승제한", offsetof(tr_fxos_filter_cfg_t, adj_prev_up)},
        {"조정매도스윙폭제한", offsetof(tr_fxos_filter_cfg_t, adj_sell_swing)},
        {"조정매수스윙폭제한", offsetof(tr_fxos_filter_cfg_t, adj_buy_swing)},
        {"조정매도기울기기준", offsetof(tr_fxos_filter_cfg_t, adj_slope)},
        {"조정매수감싸기금지", offsetof(tr_fxos_filter_cfg_t, adj_engulf)},
        {"조정매수하락감싸기금지", offsetof(tr_fxos_filter_cfg_t, adj_engulf_dn)},
        {"조정매수감싸기삼선", offsetof(tr_fxos_filter_cfg_t, adj_engulf3)},
        {"조정매수두구간평탄", offsetof(tr_fxos_filter_cfg_t, adj_flat_on)},
        {"조정매수두구간기준", offsetof(tr_fxos_filter_cfg_t, adj_two_max)},
        {"조정매수평탄기준", offsetof(tr_fxos_filter_cfg_t, adj_flat_max)},
        {"전환매수사용", offsetof(tr_fxos_filter_cfg_t, turn_buy_on)},
        {"전환매도사용", offsetof(tr_fxos_filter_cfg_t, turn_sell_on)},
        {"전환위치하한", offsetof(tr_fxos_filter_cfg_t, turn_pos_lo)},
        {"전환스윙폭상한", offsetof(tr_fxos_filter_cfg_t, turn_swing_max)},
        {"전환두구간상한", offsetof(tr_fxos_filter_cfg_t, turn_two_max)},
        {"전환제외사용", offsetof(tr_fxos_filter_cfg_t, turn_excl)},
        {"전환압축봉수제한", offsetof(tr_fxos_filter_cfg_t, turn_sq_lim)},
        {"전환삼선회색금지", offsetof(tr_fxos_filter_cfg_t, turn_gray)},
        {"전환압축색봉수", offsetof(tr_fxos_filter_cfg_t, turn_color_n)},
        {"전환반대금지봉수", offsetof(tr_fxos_filter_cfg_t, turn_opp_bars)},
        {"전환매도통합기준", offsetof(tr_fxos_filter_cfg_t, turn_sell_u)},
        {"전환매도직전100봉수", offsetof(tr_fxos_filter_cfg_t, turn_sell_prev)},
        {"전환매수직전100봉수", offsetof(tr_fxos_filter_cfg_t, turn_buy_prev)},
    };
    size_t i;
    if (cfg == 0 || key == 0) {
        return 0;
    }
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (strcmp(key, keys[i].key) == 0) {
            *(int *)((char *)cfg + keys[i].off) = (int)value;
            return 1;
        }
    }
    return 0;
}

int tr_fxos_rth_open(int yyyymmdd, const tr_fxos_filter_cfg_t *cfg) {
    int month;
    int day;
    int dow;
    int prev_sun;
    int dst;
    if (cfg == 0) {
        return 223000;
    }
    if (cfg->dst_auto != 1) {
        return cfg->rth_manual;
    }
    month = (yyyymmdd / 100) % 100;
    day = yyyymmdd % 100;
    dow = (int)tr_weekday_from_days(tr_days_from_civil(yyyymmdd / 10000, (unsigned)month, (unsigned)day));
    prev_sun = day - dow;
    dst = 0;
    if (month >= 4 && month <= 10) {
        dst = 1;
    }
    if (month == 3 && prev_sun >= 8) {
        dst = 1;
    }
    if (month == 11 && prev_sun < 1) {
        dst = 1;
    }
    return dst ? cfg->rth_summer : cfg->rth_winter;
}

void tr_fxos_filter(const tr_fxos_filter_cfg_t *cfg, const tr_fxos_filter_in_t *in,
                    tr_fxos_filter_out_t *out) {
    if (out == 0) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (cfg == 0 || in == 0) {
        return;
    }
    out->cand_dir = in->cand_dir;
    out->cand_grade = in->cand_grade;
    /* V49 후보B매도, V53 후보구조매수. Input 없이 항상 삭제. */
    if (out->cand_dir == -1 && out->cand_grade == 2) {
        out->cand_dir = 0;
    }
    if (out->cand_dir == 1 && out->cand_grade == 4) {
        out->cand_dir = 0;
    }
    if (cfg->b_three_max > 0 && out->cand_grade == 2 && in->three_ratio >= cfg->b_three_max) {
        out->cand_dir = 0;
    }
    if (cfg->b_two_max > 0 && out->cand_dir > 0 && out->cand_grade == 2 && in->two_ratio >= cfg->b_two_max) {
        out->cand_dir = 0;
    }
    if (cfg->sell_pos_lim < 0 && out->cand_dir < 0 && in->lead_valid == 1 && in->target1 > 0.0 &&
        in->lead_pos < cfg->sell_pos_lim && in->low > in->target1) {
        out->cand_dir = 0;
    }
    if (cfg->sell_width_lim > 0 && out->cand_dir < 0 && in->width_ratio >= cfg->sell_width_lim) {
        out->cand_dir = 0;
    }
    if (cfg->sell_engulf == 1 && out->cand_dir < 0 && rise_wraps_fall(in) && in->high > in->rise_lo &&
        in->unified > -100) {
        out->cand_dir = 0;
    }
    if (cfg->brk_gap > 0 && out->cand_dir < 0 && out->cand_grade == 8 && in->target5 > 0.0 &&
        in->target5 - in->close < cfg->brk_gap * (in->price_unit > 0.0 ? in->price_unit : 1.0)) {
        out->cand_dir = 0;
    }
    if (cfg->sell_engulf2 == 1 && out->cand_dir < 0 && rise_wraps_fall(in) && in->unified > -100 &&
        in->three_ratio >= 100.0 && in->two_ratio >= cfg->sell_two_min) {
        out->cand_dir = 0;
    }
    if (cfg->tall_on == 1 && in->amp >= cfg->tall_amp) {
        if (out->cand_dir > 0 && in->tall_buy == 1) {
            out->cand_dir = 0;
        }
        if (out->cand_dir < 0 && in->tall_sell == 1) {
            out->cand_dir = 0;
        }
    }
    /* V39 선행, V42·V45 구조돌파. 통합상태와 상관없이 항상 삭제. */
    out->lead_dir = 0;
    out->brk_dir = 0;
    out->adj_dir = in->adj_dir;
    out->adj_kind = in->adj_kind;
    out->adj_grade = in->adj_grade;
    /* V50·V53. 조정A매도_중, 조정B매수_강, 조정B매도_중, 조정A매수_중. */
    if ((out->adj_dir == -1 && out->adj_kind == 1 && out->adj_grade == 2) ||
        (out->adj_dir == 1 && out->adj_kind == 2 && out->adj_grade == 3) ||
        (out->adj_dir == -1 && out->adj_kind == 2 && out->adj_grade == 2) ||
        (out->adj_dir == 1 && out->adj_kind == 1 && out->adj_grade == 2)) {
        out->adj_dir = 0;
    }
    if (cfg->adj_prev_up > 0 && out->adj_dir > 0 && in->lead_valid == 1 && in->prev_up > cfg->adj_prev_up) {
        out->adj_dir = 0;
    }
    if (cfg->adj_sell_swing > 0 && out->adj_dir < 0 && in->swing >= cfg->adj_sell_swing) {
        out->adj_dir = 0;
    }
    if (cfg->adj_buy_swing > 0 && out->adj_dir > 0 && in->swing >= cfg->adj_buy_swing) {
        out->adj_dir = 0;
    }
    if (cfg->adj_slope < 0 && out->adj_dir < 0 && in->lead_valid == 1 && in->slope < cfg->adj_slope &&
        in->width_seen == 1) {
        out->adj_dir = 0;
    }
    if (cfg->adj_engulf == 1 && out->adj_dir > 0 && rise_wraps_fall(in) && in->close < in->rise_hi) {
        out->adj_dir = 0;
    }
    if (cfg->adj_engulf_dn == 1 && out->adj_dir > 0 && fall_wraps_rise(in) && in->close < in->rise_hi) {
        out->adj_dir = 0;
    }
    if (cfg->adj_engulf3 == 1 && out->adj_dir > 0 && rise_wraps_fall(in) && in->low < in->rise_hi &&
        in->three_ratio >= 100.0) {
        out->adj_dir = 0;
    }
    if (cfg->adj_flat_on == 1 && out->adj_dir > 0 && in->target5 > 0.0 && in->two_ratio > cfg->adj_two_max &&
        in->high > in->target5 && in->flat_ratio > cfg->adj_flat_max) {
        out->adj_dir = 0;
    }
    out->turn_dir = in->turn_dir;
    if (in->lead_valid == 1 && in->session_first == 0) {
        int swing_ok = cfg->turn_swing_max <= 0 || in->swing < cfg->turn_swing_max;
        out->turn_dir = 0;
        if (cfg->turn_buy_on == 1 && in->slope == 1 && in->slope_prev <= 0 && in->pos <= 0 &&
            in->pos >= cfg->turn_pos_lo &&
            in->unified > -100 && in->fall_lo > 0.0 && in->prev_dn > 0 && swing_ok &&
            in->prior_low > in->fall_lo) {
            out->turn_dir = 1;
        }
        if (cfg->turn_sell_on == 1 && in->slope == -1 && in->slope_prev >= 0 && in->pos >= 0 &&
            in->pos <= -cfg->turn_pos_lo &&
            in->unified < 100 && in->rise_hi > 0.0 && in->prev_up > 0 && swing_ok &&
            in->prior_high < in->rise_hi) {
            out->turn_dir = -1;
        }
    }
    {
        int rth_m = clock_min(tr_fxos_rth_open(in->date, cfg));
        int bar_m = clock_min(in->time);
        int elapsed = bar_m - rth_m;
        int pre = cfg->pre_min > 0 && bar_m >= rth_m - cfg->pre_min && bar_m < rth_m;
        if (elapsed < 0) {
            elapsed += 1440;
        }
        drop_preopen(pre, in->unified, &out->cand_dir);
        drop_preopen(pre, in->unified, &out->adj_dir);
        drop_preopen(pre, in->unified, &out->turn_dir);
        if (cfg->adj_sell_allow > 0 && in->time >= 70000 &&
            bar_m < rth_m + cfg->adj_sell_allow && out->adj_dir < 0) {
            out->adj_dir = 0;
        }
        if (cfg->s_buy_min > 0 && out->cand_dir > 0 && out->cand_grade == 5 && elapsed >= cfg->s_buy_min) {
            out->cand_dir = 0;
        }
        if (cfg->s_sell_min > 0 && out->cand_dir < 0 && out->cand_grade == 5 && elapsed >= cfg->s_sell_min) {
            out->cand_dir = 0;
        }
        if (cfg->s_buy_sq == 1 && out->cand_dir > 0 && out->cand_grade == 5 && in->sq_bars > 0) {
            out->cand_dir = 0;
        }
        if (cfg->b_buy_pre == 1 && out->cand_dir > 0 && out->cand_grade == 2 && in->time >= 70000 &&
            bar_m < rth_m) {
            out->cand_dir = 0;
        }
    }
    if (cfg->turn_two_max > 0 && out->turn_dir != 0 && in->two_ratio > cfg->turn_two_max) {
        out->turn_dir = 0;
    }
    if (cfg->turn_excl == 1 && out->turn_dir > 0 &&
        (in->unified < 0 || in->sq_bars >= cfg->turn_sq_lim || in->hot_up == 1)) {
        out->turn_dir = 0;
    }
    if (cfg->turn_excl == 1 && out->turn_dir < 0 &&
        (in->unified > 0 || in->sq_bars >= cfg->turn_sq_lim || in->hot_dn == 1)) {
        out->turn_dir = 0;
    }
    if (cfg->turn_gray == 1 && in->gray == 1 && out->turn_dir < 0) {
        out->turn_dir = 0;
    }
    if (cfg->turn_color_n > 0 && out->turn_dir < 0 && in->red_n * 2 > cfg->turn_color_n) {
        out->turn_dir = 0;
    }
    if (cfg->turn_opp_bars > 0 && out->turn_dir < 0 && in->bars_since_buy <= cfg->turn_opp_bars) {
        out->turn_dir = 0;
    }
    if (cfg->turn_sell_u == 1 && out->turn_dir < 0 && in->unified != -50) {
        out->turn_dir = 0;
    }
    if (cfg->turn_sell_prev > 0 && out->turn_dir < 0 && in->had_m100 == 1) {
        out->turn_dir = 0;
    }
    if (cfg->turn_buy_prev > 0 && out->turn_dir > 0 && (in->had_p100 == 1 || in->had_m100 == 1)) {
        out->turn_dir = 0;
    }
    if (out->turn_dir > 0) {
        out->turn_y = -190;
        out->turn_rgb = 0x006400u;
        out->turn_w = 7;
    } else if (out->turn_dir < 0) {
        out->turn_y = 190;
        out->turn_rgb = 0x960050u;
        out->turn_w = 7;
    }
    (void)cfg->adj_a_mid_block;
}

void tr_fxos_pick(int cand_dir, int brk_dir, int lead_dir, int turn_dir, int adj_dir, int *dir,
                  int *kind) {
    int d = 0;
    int k = 0;
    if (adj_dir != 0) {
        d = adj_dir;
        k = 4;
    }
    if (turn_dir != 0) {
        d = turn_dir;
        k = 5;
    }
    if (lead_dir != 0) {
        d = lead_dir;
        k = 3;
    }
    if (brk_dir != 0) {
        d = brk_dir;
        k = 2;
    }
    if (cand_dir != 0) {
        d = cand_dir;
        k = 1;
    }
    if (dir != 0) {
        *dir = d;
    }
    if (kind != 0) {
        *kind = k;
    }
}

const char *tr_fxos_order_name(int dir, int kind, int grade, int adj_kind, int adj_grade) {
    if (kind == 1) {
        if (grade == 8) {
            return dir < 0 ? "후보돌파매도" : "후보돌파매수";
        }
        if (grade == 7) {
            return dir < 0 ? "후보T매도" : "후보T매수";
        }
        if (grade == 6) {
            return dir < 0 ? "후보R매도" : "후보R매수";
        }
        if (grade == 5) {
            return dir < 0 ? "후보S매도" : "후보S매수";
        }
        if (grade == 4) {
            return dir < 0 ? "후보구조매도" : "후보구조매수";
        }
        if (grade == 3) {
            return dir < 0 ? "후보A매도" : "후보A매수";
        }
        if (grade == 2) {
            return dir < 0 ? "후보B매도" : "후보B매수";
        }
        if (grade == 1) {
            return dir < 0 ? "후보C매도" : "후보C매수";
        }
        return dir < 0 ? "후보매도" : "후보매수";
    }
    if (kind == 2) {
        return dir < 0 ? "돌파매도" : "돌파매수";
    }
    if (kind == 3) {
        return dir < 0 ? "선행매도" : "선행매수";
    }
    if (kind == 5) {
        return dir < 0 ? "전환매도" : "전환매수";
    }
    if (adj_kind == 1 && adj_grade == 3) {
        return dir < 0 ? "조정A매도_강" : "조정A매수_강";
    }
    if (adj_kind == 1 && adj_grade == 2) {
        return dir < 0 ? "조정A매도_중" : "조정A매수_중";
    }
    if (adj_kind == 1 && adj_grade == 1) {
        return dir < 0 ? "조정A매도_약" : "조정A매수_약";
    }
    if (adj_kind == 2 && adj_grade == 3) {
        return dir < 0 ? "조정B매도_강" : "조정B매수_강";
    }
    if (adj_kind == 2 && adj_grade == 2) {
        return dir < 0 ? "조정B매도_중" : "조정B매수_중";
    }
    if (adj_kind == 2 && adj_grade == 1) {
        return dir < 0 ? "조정B매도_약" : "조정B매수_약";
    }
    return dir < 0 ? "조정매도" : "조정매수";
}

const char *tr_fxos_exit_name(int sig_exit) {
    if (sig_exit == 2) {
        return "시간청산매수";
    }
    if (sig_exit == -2) {
        return "시간청산매도";
    }
    if (sig_exit < 0) {
        return "빨강청산";
    }
    return "파랑청산";
}
