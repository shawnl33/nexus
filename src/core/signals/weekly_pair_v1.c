#include "core/signals/weekly_pair_v1.h"

#include "core/backtest/yl_fill.h"
#include "core/functions/fx_session_profile_v2.h"

#include <stdlib.h>
#include <string.h>

#include "core/model/civil_time.h"

/* 승수. 양매수.txt 779·797·802·1074, 양매도.txt 782·800·805·1068. 비용 제외. */
static const double k_mult = 250000.0;

static int32_t int_portion(double v) {
    return (int32_t)v;
}

static int32_t imax(int32_t a, int32_t b) {
    return a > b ? a : b;
}

static int32_t imin(int32_t a, int32_t b) {
    return a < b ? a : b;
}

static int64_t imax64(int64_t a, int64_t b) {
    return a > b ? a : b;
}

static double dmax(double a, double b) {
    return a > b ? a : b;
}

static double dmin(double a, double b) {
    return a < b ? a : b;
}

/* 예스랭귀지 DayOfWeek: 월 1 … 일 7. */
static int yl_dow(int64_t bdate) {
    int year = (int)(bdate / 10000);
    unsigned month = (unsigned)((bdate / 100) % 100);
    unsigned day = (unsigned)(bdate % 100);
    unsigned wd = tr_weekday_from_days(tr_days_from_civil(year, month, day));
    if (wd == 0) {
        return 7;
    }
    return (int)wd;
}

static int d2_1m(const tr_wpair_bar_t *b) {
    return b->d2_compress == 2 && b->d2_interval == 1;
}

static int d2_sync(const tr_wpair_bar_t *b) {
    return b->d2_date == b->sdate && b->d2_time == b->stime && d2_1m(b);
}

static void clear_order(tr_wpair_rt *r) {
    r->order_kind = TR_WPAIR_ORDER_NONE;
    r->order_qty = 0;
    r->order_reason = 0;
    r->order_name = "";
}

static void mark_order(tr_wpair_rt *r, int kind, int32_t qty, int reason, const char *name) {
    r->order_kind = kind;
    r->order_qty = qty;
    r->order_reason = reason;
    r->order_name = name;
}

static const char *path_exit_name(const tr_wpair_rt *r, const char *three, const char *squeeze,
                                  const char *plain) {
    if (r->entry_path == 1) {
        return three;
    }
    if (r->entry_path == 2) {
        return squeeze;
    }
    return plain;
}

static const char *exit_all_name(int reason, int v12) {
    switch (reason) {
    case 1:
        return "합산익절";
    case 2:
        return "합산손절";
    case 3:
        return "시간청산";
    case 4:
        return "날짜변경청산";
    case 9:
        return "만기익절";
    case 10:
        return "잔량익절보호";
    case 11:
        return v12 ? "만기익절2차최종" : "만기잔량익절";
    case 12:
        return "만기잔량손실청산";
    case 14:
        return v12 ? "2차추적청산" : "만기2차추적청산";
    case 16:
        return "잔량1차절반손절";
    case 18:
        return "익절후압축손실청산";
    default:
        return "기타청산";
    }
}

static void reset_day(tr_wpair_rt *r) {
    /* 양매수.txt 722~757. 청산사유·신호봉·진입대기는 여기서 지우지 않는다. */
    r->entry_fut_bar = -1;
    r->entry_fut_date = 0;
    r->entry_elapsed = 0;
    r->trail_active = 0;
    r->trail_target = 0;
    r->trail_peak = 0;
    r->trail_protect = 0;
    r->low_done = 0;
    r->low_pending = 0;
    r->low_qty = 0;
    r->low_prev_qty = 0;
    r->book_qty = 0;
    r->book_realized = 0;
    r->book_unreal = 0;
    r->book_total = 0;
    r->book_total_pct = 0;
    r->book_principal = 0;
    r->used_today = 0;
    r->first_done = 0;
    r->first_pending = 0;
    r->first_qty = 0;
    r->first_prev_qty = 0;
    r->first_reason = 0;
    r->first_fut_bar = -1;
    r->first_fut_date = 0;
    r->resid_elapsed = 0;
    r->ratio_dipped = 0;
    r->first_basis_pct = 0;
    r->holding = 0;
    r->exit_req = 0;
    r->model_valid = 0;
    r->self_qty = 0;
    r->opp_qty = 0;
    r->model_notional = 0;
    r->self_first = 0;
    r->opp_first = 0;
    r->self_entry = 0;
    r->opp_entry = 0;
    r->open_target_min = -1;
    r->rec_meet = 0;
    r->rec_bar = -1;
    r->rec_fire_bar = -1;
    r->rec_total = 0;
    r->rec_loss = 0;
    r->rec_cur_loss = 0;
    r->rec_date = 0;
}

static void apply_fill(tr_wpair_rt *r, int side) {
    if (r->order_kind == TR_WPAIR_ORDER_ENTRY) {
        r->market_position = side;
        r->contracts = r->order_qty;
    } else if (r->order_kind == TR_WPAIR_ORDER_EXIT_PART) {
        r->contracts -= r->order_qty;
        if (r->contracts <= 0) {
            r->contracts = 0;
            r->market_position = 0;
            r->entry_path = 0;
        }
    } else if (r->order_kind == TR_WPAIR_ORDER_EXIT_ALL) {
        r->contracts = 0;
        r->market_position = 0;
        r->entry_path = 0;
    }
    clear_order(r);
}

static int minute_of(int64_t hhmmss) {
    int64_t hour = hhmmss / 10000;
    int64_t minute = (hhmmss / 100) % 100;
    return (int)(hour * 60 + minute);
}

/* 잔량 30% 반올림. 최소 1, 최대 잔량. 2→1, 3→1, 5→2. */
static int32_t round_pct30(int32_t qty) {
    int32_t rounded;
    if (qty < 1) {
        return 0;
    }
    rounded = (int32_t)((double)qty * 0.3 + 0.5);
    if (rounded < 1) {
        rounded = 1;
    }
    if (rounded > qty) {
        rounded = qty;
    }
    return rounded;
}

static void v9_reset(tr_wpair_rt *r, int32_t qty, int minute, int64_t date) {
    memset(r->v9_p, 0, sizeof(r->v9_p));
    memset(r->v9_m, 0, sizeof(r->v9_m));
    r->v9_saw10 = 0;
    r->v9_early_done = 0;
    r->v9_book_done = 0;
    r->v9_lead = 0;
    r->v9_book_with = 0;
    r->v9_book_flip = 0;
    r->v9_book_min = -1;
    r->v9_book_date = 0;
    r->v9_full_cut = 0;
    r->v9_recovering = 0;
    r->v9_full_qty = qty;
    r->v9_entry_min = minute;
    r->v9_entry_date = date;
    r->v9_peak = 0.0;
    r->v9_peak_min = -1;
    r->v9_trough = 0.0;
    r->v9_recover_high = 0.0;
    r->v9_obs = 0;
    r->v9_loss = 0;
    r->v9_hist_n = 0;
    r->v9_last_bar = -1;
    r->v9_hist_date = 0;
}

/* 이전 유효봉을 [1]..[5]에 둔 뒤 이번 합산수익률을 기록한다. */
static void v9_observe(tr_wpair_rt *r, const tr_wpair_bar_t *b, double pct) {
    int minute = minute_of(b->stime);
    int i;
    if (r->v9_last_bar >= 0 && b->sdate == r->v9_hist_date && b->d2_day_index == r->v9_last_bar + 1) {
        for (i = 5; i >= 1; i--) {
            r->v9_p[i] = r->v9_p[i - 1];
            r->v9_m[i] = r->v9_m[i - 1];
        }
        if (r->v9_hist_n < 5) {
            r->v9_hist_n++;
        }
    } else if (r->v9_last_bar >= 0 && b->d2_day_index != r->v9_last_bar) {
        r->v9_hist_n = 0;
    }
    r->v9_p[0] = pct;
    r->v9_m[0] = minute;
    r->v9_last_bar = b->d2_day_index;
    r->v9_hist_date = b->sdate;
    if (pct > r->v9_peak) {
        r->v9_peak = pct;
        r->v9_peak_min = minute;
    }
    if (!r->first_done && pct >= 10.0) {
        r->v9_saw10 = 1;
    }
    r->v9_obs++;
    if (pct < 0.0) {
        r->v9_loss++;
    }
    if (pct < r->v9_trough) {
        r->v9_trough = pct;
        r->v9_recover_high = pct;
        r->v9_recovering = 0;
    } else if (r->v9_trough < 0.0) {
        if (!r->v9_recovering && pct - r->v9_trough >= 5.0) {
            r->v9_recovering = 1;
            r->v9_recover_high = pct;
        }
        if (r->v9_recovering) {
            r->v9_recover_high = dmax(r->v9_recover_high, pct);
        }
    }
    if (r->v9_book_min < 0 && pct > 10.0) {
        r->v9_book_min = minute;
        r->v9_book_date = b->sdate;
        if (b->close > r->rec_meet && b->close > b->d3_close) {
            r->v9_lead = 1;
        } else if (b->d3_close > r->rec_meet && b->d3_close > b->close) {
            r->v9_lead = -1;
        }
    }
    if (r->v9_book_min >= 0 && r->v9_book_date == b->sdate && b->book_valid) {
        if ((r->v9_lead == 1 && b->book_score > 0.0) || (r->v9_lead == -1 && b->book_score < 0.0)) {
            r->v9_book_with = 1;
            r->v9_book_flip = 0;
        }
        if (r->v9_book_with &&
            ((r->v9_lead == 1 && b->book_score < 0.0) || (r->v9_lead == -1 && b->book_score > 0.0))) {
            r->v9_book_flip = 1;
        }
    }
}

static double meet_px(const tr_wpair_config_t *cfg, const tr_wpair_bar_t *b) {
    if (cfg->meet_mode == 1) {
        return (dmax(b->low, b->d3_low) + dmin(b->high, b->d3_high)) / 2.0;
    }
    return (b->close + b->d3_close) / 2.0;
}

/* V14 양매수 첫 진입. 30봉 미만 종가간격 15%, 만남가격 2.1 이상,
 * 45봉까지 4.0 이상은 종가간격이 15% 미만일 때만.
 * 전 구간은 만남가격 3 미만이면 간격 25% 미만, 3 이상이면 20% 미만. */
static int long_entry_px_ok(const tr_wpair_config_t *cfg, const tr_wpair_bar_t *b) {
    double sum;
    double gap;
    double px;
    if (!(b->close > 0.0) || !(b->d3_close > 0.0)) {
        return 0;
    }
    sum = b->close + b->d3_close;
    gap = b->close - b->d3_close;
    if (gap < 0.0) {
        gap = -gap;
    }
    if (b->d2_day_index < 30 && gap > sum * 0.075) {
        return 0;
    }
    px = (b->close + b->d3_close) / 2.0;
    if (cfg->meet_mode == 1) {
        px = (dmax(b->low, b->d3_low) + dmin(b->high, b->d3_high)) / 2.0;
    }
    if (!(px >= 2.1) || !(px < 5.0)) {
        return 0;
    }
    if (b->d2_day_index <= 45 && !(px < 4.0) && !(gap < sum * 0.075)) {
        return 0;
    }
    /* 90봉 전: 전일 종가가 없으면 막고, 갭 1% 이상이면서 만남가격 2.3 미만도 막는다. */
    if (b->d2_day_index < 90) {
        double rate;
        if (!b->d2_has_gap || !(b->d2_prev_close > 0.0) || !(b->d2_day_open > 0.0)) {
            return 0;
        }
        rate = b->d2_day_open - b->d2_prev_close;
        if (rate < 0.0) {
            rate = -rate;
        }
        rate = rate / b->d2_prev_close * 100.0;
        if (rate >= 1.0 && px < 2.3) {
            return 0;
        }
    }
    if (px < 3.0) {
        if (!(gap < sum * 0.125)) {
            return 0;
        }
    } else if (!(gap < sum * 0.10)) {
        return 0;
    }
    return 1;
}

/* 양매수 V12 2391~2423. 오전 60 이하는 삼선 상한만, 60 초과는 가격비율 100 미만이 더 필요하다.
 * 오후는 오전 고가가 20을 넘은 적이 있으면 70 이하, 없으면 오후 상한 이하. */
static int v12_three_ok(const tr_wpair_config_t *cfg, const tr_wpair_bar_t *b, int morn_high) {
    double applied;
    if (!(b->d2_date == b->sdate && d2_1m(b) && b->three_ready == 1 && b->three_peak > 0.0)) {
        return 0;
    }
    if (b->d2_day_index < imax(0, cfg->afternoon_switch_bar)) {
        int under;
        applied = dmin(70.0, cfg->morning_three_max);
        under = b->three_ratio < applied;
        if (b->three_ratio <= 60.0) {
            return under;
        }
        return under && b->d2_time == b->stime && b->calc_ready == 1 && b->break_ready == 1 &&
               b->two_max > 0.0 && b->price_ratio < 100.0;
    }
    applied = morn_high ? 70.0 : dmin(50.0, cfg->afternoon_three_max);
    return b->three_ratio <= applied;
}

static int ranges_meet(const tr_wpair_config_t *cfg, const tr_wpair_bar_t *b) {
    double tol = dmax(0.0, cfg->tolerance);
    if (cfg->meet_mode == 1) {
        return dmax(b->low, b->d3_low) <= dmin(b->high, b->d3_high) + tol;
    }
    if (cfg->meet_mode == 2) {
        return b->close - tol <= b->d3_close && b->close + tol >= b->d3_close;
    }
    return 0;
}

static void run_bar(tr_wpair_t *st, const tr_wpair_bar_t *b) {
    tr_wpair_rt *r = &st->now;
    const tr_wpair_config_t *cfg = &st->cfg;
    const int side = st->side;
    const int held = r->market_position == side;
    const double sgn = (double)side;
    const int v12 = side == TR_WPAIR_LONG && cfg->ruleset == TR_WPAIR_RULES_V12;
    const int expiry = yl_dow(b->bdate) == 1 || yl_dow(b->bdate) == 4 ||
                       (v12 && cfg->expiry_override == 1);
    const int period_ok = b->compress == 2 && b->interval == 1 && b->d3_compress == 2 &&
                          b->d3_interval == 1;
    const int synced = b->d3_date == b->sdate && b->d3_time == b->stime && b->close > 0.0 &&
                       b->d3_close > 0.0;

    clear_order(r);

    if (side > 0 && r->high_date != b->bdate) {
        r->high_date = b->bdate;
        r->morn_hi_self = 0;
        r->morn_hi_opp = 0;
        r->high_block = 0;
        r->morn_hi_any = 0;
    }
    if (side > 0 && b->d2_date == b->sdate &&
        b->d2_day_index < imax(0, cfg->afternoon_switch_bar)) {
        if (b->high > 20.0) {
            r->morn_hi_self++;
            r->morn_hi_any = 1;
        }
        if (b->d3_date == b->sdate && b->d3_high > 20.0) {
            r->morn_hi_opp++;
            r->morn_hi_any = 1;
        }
        if (r->morn_hi_self >= 3 || r->morn_hi_opp >= 3) {
            r->high_block = 1;
        }
    }

    /* 양매수.txt 718~765. 남은 포지션과 진입대기는 날짜가 바뀌어도 유지하고 청산이 우선. */
    if (r->saved_date != b->bdate) {
        r->saved_date = b->bdate;
        if (r->market_position == 0 && !r->entry_pending) {
            reset_day(r);
        } else {
            r->used_today = 1;
            r->exit_req = 1;
            r->exit_reason = 4;
        }
    }

    /* 모델은 신호 다음 봉에서 잠근다. V14 양매수는 그 봉이 신호 시각+1분일 때만 시가를 쓴다. */
    if (held && !r->holding) {
        r->holding = 1;
        r->three40_watch = 0;
        r->three40_hit = 0;
        if (v12) {
            v9_reset(r, r->contracts, minute_of(b->stime), b->sdate);
        }
        r->entry_pending = 0;
        r->rec_bar = -1;
        r->rec_fire_bar = -1;
        r->rec_total = 0;
        r->rec_loss = 0;
        r->rec_cur_loss = 0;
        r->rec_date = 0;
        if (!v12 && r->signal_date == b->bdate && b->index == r->signal_bar + 1 && synced) {
            r->self_entry = r->self_first;
            r->opp_entry = r->opp_first;
            r->model_notional =
                (r->self_first * (double)r->self_qty + r->opp_first * (double)r->opp_qty) * k_mult;
            r->model_valid = r->model_notional > 0.0;
        } else if (!v12) {
            r->model_valid = 0;
        }
    }
    if (v12 && held && r->holding && !r->model_valid && r->signal_date == b->bdate) {
        if (!(r->self_entry > 0.0) && b->index == r->signal_bar + 1 &&
            minute_of(b->stime) == r->open_target_min && b->open > 0.0) {
            r->open_date = b->sdate;
            r->open_time = b->stime;
            r->self_entry = b->open;
        }
        if (r->self_entry > 0.0 && !(r->opp_entry > 0.0) && b->d3_date == r->open_date &&
            b->d3_time == r->open_time && b->d3_open > 0.0) {
            r->opp_entry = b->d3_open;
        }
        if (r->self_entry > 0.0 && r->opp_entry > 0.0 && r->self_qty > 0 && r->opp_qty > 0) {
            r->model_notional =
                (r->self_entry * (double)r->self_qty + r->opp_entry * (double)r->opp_qty) * k_mult;
            r->model_valid = r->model_notional > 0.0;
        }
    }

    /* 양매수.txt 791~804, 양매도.txt 800·805. 장부는 이 차트만, 수량 감소 봉 시가. */
    if ((r->self_entry > 0.0 || r->self_first > 0.0) && r->self_qty > 0 &&
        (r->holding || r->book_qty > 0)) {
        const double basis = r->self_entry > 0.0 ? r->self_entry : r->self_first;
        int32_t dropped = imax(0, r->book_qty - r->contracts);
        r->book_principal = basis * (double)r->self_qty * k_mult;
        if (dropped > 0) {
            r->book_realized += sgn * (b->open - basis) * (double)dropped * k_mult;
        }
        r->book_qty = r->contracts;
        r->book_unreal = sgn * (b->close - basis) * (double)r->book_qty * k_mult;
        r->book_total = r->book_realized + r->book_unreal;
        r->book_total_pct = r->book_total / r->book_principal * 100.0;
    }

    /* 양매수.txt 813~822. 당일사용은 청산 뒤에도 남는다. */
    if (r->holding && r->market_position == 0) {
        r->holding = 0;
        r->low_pending = 0;
        r->first_pending = 0;
        r->exit_req = 0;
        r->model_valid = 0;
    }
    if (r->entry_pending && r->market_position == 0 && b->index > r->signal_bar + 1) {
        r->entry_pending = 0;
    }

    if (held && r->holding && d2_sync(b)) {
        if (r->entry_fut_bar < 0) {
            r->entry_fut_bar = b->d2_day_index;
            r->entry_fut_date = b->sdate;
        }
        if (r->entry_fut_date == b->sdate) {
            r->entry_elapsed = imax(0, b->d2_day_index - r->entry_fut_bar);
        }
    }
    if (held && r->low_pending && r->contracts <= r->low_prev_qty - r->low_qty) {
        r->low_pending = 0;
    }
    if (held && r->first_pending && r->contracts <= r->first_prev_qty - r->first_qty) {
        r->first_pending = 0;
    }
    if (held && r->first_done && !r->first_pending && d2_sync(b)) {
        if (r->first_fut_bar < 0) {
            r->first_fut_bar = b->d2_day_index;
            r->first_fut_date = b->sdate;
        }
        if (r->first_fut_date == b->sdate) {
            r->resid_elapsed = imax(0, b->d2_day_index - r->first_fut_bar);
        }
        if (b->calc_ready == 1 && b->break_ready == 1 && b->two_max > 0.0 && b->price_ratio < 100.0) {
            r->ratio_dipped = 1;
        }
    }

    if (held) {
        int rec_ok = 0;
        /* 양매수.txt 864~871. 시간 청산은 모델이 없어도 실행되고 사유 4를 덮어쓴다. */
        if (b->stime >= cfg->flat_time ||
            (b->next_sdate == b->sdate && b->next_stime >= cfg->flat_time) ||
            b->next_sdate != b->sdate) {
            r->exit_req = 1;
            r->exit_reason = 3;
        }
        if (r->model_valid && synced && !r->exit_req) {
            const double self_basis = v12 ? r->self_entry : r->self_first;
            const double opp_basis = v12 ? r->opp_entry : r->opp_first;
            const double self_px = b->close;
            const double opp_px = b->d3_close;
            const int ratio_ok = d2_sync(b) && b->calc_ready == 1 && b->break_ready == 1 &&
                                 b->two_max > 0.0;
            const int three_ok = d2_sync(b) && b->calc_ready == 1 && b->three_ready == 1 &&
                                 b->three_peak > 0.0 && b->three_ratio >= 70.0;
            const int second_time_valid = r->entry_fut_bar >= 0 && r->entry_fut_date == b->sdate &&
                                          d2_sync(b);
            r->pair_pnl = (sgn * (self_px - self_basis) * (double)r->self_qty +
                           sgn * (opp_px - opp_basis) * (double)r->opp_qty) *
                          k_mult;
            r->pair_pct = (sgn * (self_px / self_basis - 1.0) +
                           sgn * (opp_px / opp_basis - 1.0)) *
                          50.0;
            if (v12 && d2_sync(b)) {
                double goal;
                if (r->rec_date != b->sdate) {
                    r->rec_total = 0;
                    r->rec_loss = 0;
                    r->rec_bar = -1;
                    r->rec_cur_loss = 0;
                    r->rec_date = b->sdate;
                }
                if (r->rec_bar < 0 || b->d2_day_index > r->rec_bar) {
                    r->rec_total++;
                    r->rec_cur_loss = r->pair_pct < 0.0 ? 1 : 0;
                    r->rec_loss += r->rec_cur_loss;
                    r->rec_bar = b->d2_day_index;
                } else if (b->d2_day_index == r->rec_bar) {
                    r->rec_loss -= r->rec_cur_loss;
                    r->rec_cur_loss = r->pair_pct < 0.0 ? 1 : 0;
                    r->rec_loss += r->rec_cur_loss;
                }
                goal = r->rec_meet >= 3.0 ? 5.0 : 10.0;
                rec_ok = r->rec_meet > 0.0 && r->rec_bar == b->d2_day_index && r->rec_total > 0 &&
                         r->rec_loss * 100 >= r->rec_total * 70 && r->entry_fut_bar >= 0 &&
                         r->entry_fut_date == b->sdate && r->entry_elapsed >= 30 &&
                         r->pair_pct >= goal && !r->low_done && !r->first_done && !r->first_pending &&
                         r->contracts >= 2 && cfg->low_cut_pct > 0.0;
            }
            r->self_resid_pct = sgn * (self_px - r->self_entry) / r->self_entry * 100.0;
            if (v12 && cfg->split_exit == 1 && r->first_done && !r->first_pending) {
                const double interval = dmax(0.01, cfg->trail_interval);
                if (!r->trail_active && second_time_valid &&
                    r->pair_pct >= cfg->take_pct * 4.0) {
                    r->trail_active = 1;
                    r->trail_target = cfg->take_pct * 4.0;
                    r->trail_peak = r->pair_pct;
                    r->trail_protect = r->trail_target - interval;
                }
                if (r->trail_active) {
                    r->trail_peak = dmax(r->trail_peak, r->pair_pct);
                    r->trail_protect = dmax(r->trail_protect,
                                            r->trail_target +
                                                int_portion(dmax(0.0, r->trail_peak - r->trail_target) /
                                                            interval) *
                                                    interval -
                                                interval);
                }
            }
            if (side > 0 && d2_sync(b) && b->three_ready == 1 && b->three_peak > 0.0) {
                r->three40_watch = 1;
                if (b->three_ratio >= 40.0) {
                    r->three40_hit = 1;
                }
            }
            if (v12 && d2_sync(b)) {
                v9_observe(r, b, r->pair_pct);
            }
            {
                /* V9: 양매수 공통손절은 -20 고정. 삼선 40 미도달 -25 예외는 없다. */
                double stop = v12 ? 20.0 : cfg->stop_pct;
                if (r->pair_pct <= -stop) {
                    r->exit_req = 1;
                    r->exit_reason = 2;
                }
            }
            if (r->exit_req) {
                /* 위에서 손절을 정했다. 아래 사슬은 손절이 아닐 때만 탄다. */
            } else if (v12) {
                const int minute = minute_of(b->stime);
                const int sq60 = b->sq_ready == 1 && b->sq_ratio < 60.0;
                const int since_entry = r->v9_entry_date == b->sdate && r->v9_entry_min >= 0
                                             ? minute - r->v9_entry_min
                                             : -1;
                const int elapsed30 = since_entry >= 30;
                const int hist5 = r->v9_hist_n >= 5 && minute - r->v9_m[5] == 5;
                int full_ok;
                int v9_reason = 0;
                if (r->v9_full_qty > 0 && r->contracts < r->v9_full_qty) {
                    r->v9_full_cut = 1;
                }
                if (r->first_done || r->low_done || r->low_extra_done || r->v9_early_done) {
                    r->v9_full_cut = 1;
                }
                full_ok = r->v9_full_qty > 0 && r->contracts == r->v9_full_qty &&
                          !r->v9_full_cut && !r->first_pending && !r->low_pending;
                if (r->v9_saw10 && r->first_done && !r->first_pending && r->contracts > 0 &&
                    r->contracts < r->first_prev_qty && b->sq_ready == 1 && b->sq_ratio < 40.0 &&
                    r->pair_pct < 0.0) {
                    r->exit_req = 1;
                    r->exit_reason = 18;
                } else if (cfg->split_exit == 1 && r->first_done && !r->first_pending &&
                    r->first_basis_pct > 0.0 && r->pair_pct <= -r->first_basis_pct * 0.5) {
                    r->exit_req = 1;
                    r->exit_reason = 16;
                } else if (cfg->split_exit == 1 && r->first_done && !r->first_pending &&
                           r->trail_active && r->pair_pct <= r->trail_protect) {
                    r->exit_req = 1;
                    r->exit_reason = 14;
                } else if (!r->v9_early_done && elapsed30 && sq60 && full_ok && r->v9_obs > 0 &&
                           r->v9_loss * 100 >= r->v9_obs * 70 && r->v9_recovering &&
                           r->pair_pct < 0.0 && r->v9_recover_high - r->pair_pct >= 2.0) {
                    v9_reason = 22;
                } else if (!r->v9_early_done && elapsed30 && sq60 && full_ok &&
                           r->v9_peak >= 5.0 && r->v9_peak <= 10.0 && r->v9_peak_min >= 0 &&
                           minute - r->v9_peak_min >= 10 && r->pair_pct < 0.0 &&
                           r->v9_hist_n >= 2 && r->v9_p[1] < 0.0 && r->v9_p[2] < 0.0 &&
                           minute - r->v9_m[1] == 1 && minute - r->v9_m[2] == 2) {
                    v9_reason = 23;
                } else if (!r->v9_early_done && sq60 && r->v9_entry_date == b->sdate &&
                           r->contracts > 0 && !r->first_pending && elapsed30 &&
                           r->v9_peak < 5.0 && r->pair_pct < 0.0 && hist5 &&
                           r->pair_pct < r->v9_p[5]) {
                    v9_reason = 20;
                } else if (!r->v9_early_done && sq60 && r->v9_entry_date == b->sdate &&
                           r->contracts > 0 && !r->first_pending && r->v9_peak >= 5.0 &&
                           r->v9_peak <= 10.0 && r->v9_peak_min >= 0 &&
                           minute - r->v9_peak_min >= 10 && r->pair_pct > 0.0 &&
                           r->pair_pct < r->v9_peak * 0.5) {
                    v9_reason = 21;
                } else if (!r->v9_book_done && !r->first_pending && r->contracts > 0 &&
                           r->v9_book_flip && r->v9_book_min >= 0 && r->v9_book_date == b->sdate &&
                           minute - r->v9_book_min >= 15 && r->pair_pct < r->v9_peak * 0.5 &&
                           b->book_valid && b->flat_valid &&
                           ((r->v9_lead == 1 && b->book_score < 0.0 && b->flat_pos < 0 && b->flat_slope < 0) ||
                            (r->v9_lead == -1 && b->book_score > 0.0 && b->flat_pos > 0 && b->flat_slope > 0))) {
                    v9_reason = 19;
                }
                if (v9_reason != 0) {
                    const int32_t qty = round_pct30(r->contracts);
                    if (qty > 0) {
                        const char *name = v9_reason == 22 ? path_exit_name(r, "손실회복재악화30_삼선", "손실회복재악화30_압축", "손실회복재악화30")
                            : v9_reason == 23 ? path_exit_name(r, "소폭수익손실전환30_삼선", "소폭수익손실전환30_압축", "소폭수익손실전환30")
                            : v9_reason == 20 ? path_exit_name(r, "무수익시간부분청산_삼선", "무수익시간부분청산_압축", "무수익시간부분청산")
                            : v9_reason == 19 ? path_exit_name(r, "호가평탄반감30_삼선", "호가평탄반감30_압축", "호가평탄반감30")
                            : path_exit_name(r, "소폭수익보호부분청산_삼선", "소폭수익보호부분청산_압축", "소폭수익보호부분청산");
                        if (v9_reason == 19) {
                            r->v9_book_done = 1;
                        } else {
                            r->v9_early_done = 1;
                        }
                        r->v9_full_cut = 1;
                        mark_order(r, TR_WPAIR_ORDER_EXIT_PART, qty, v9_reason, name);
                    }
                } else if (!r->first_done && !rec_ok &&
                           (r->rec_fire_bar < 0 || b->d2_day_index > r->rec_fire_bar) &&
                           second_time_valid &&
                           r->pair_pct >=
                               cfg->take_pct * ((r->self_first < 3.0 || r->opp_first < 3.0) ? 1.5
                                                                                           : 1.0)) {
                    r->exit_req = 1;
                    r->exit_reason = expiry ? 9 : 1;
                }
            } else if (cfg->split_exit == 1 && r->first_done && !r->first_pending && expiry &&
                       r->self_resid_pct < 0.0) {
                r->exit_req = 1;
                r->exit_reason = 12;
            } else if (cfg->split_exit == 1 && r->first_done && !r->first_pending && expiry &&
                       r->first_fut_bar >= 0 && r->first_fut_date == b->sdate &&
                       r->resid_elapsed >= imax(0, cfg->resid_wait_min) && r->ratio_dipped &&
                       ratio_ok && b->price_ratio == 100.0 && cfg->expiry_resid_pct > 0.0 &&
                       r->self_resid_pct >= cfg->expiry_resid_pct) {
                r->exit_req = 1;
                r->exit_reason = 11;
            } else if (cfg->split_exit == 1 && r->first_done && !r->first_pending &&
                       ((!expiry && r->pair_pct < cfg->take_pct) ||
                        (expiry && r->self_resid_pct < cfg->take_pct && three_ok && ratio_ok &&
                         b->price_ratio >= 100.0))) {
                r->exit_req = 1;
                r->exit_reason = 10;
            } else if (!r->low_pending && !r->first_done && expiry &&
                       r->pair_pct > cfg->take_pct * 1.5 && ratio_ok && b->price_ratio == 100.0) {
                r->exit_req = 1;
                r->exit_reason = 9;
            } else if (!r->low_pending && !r->first_done && !expiry &&
                       r->pair_pct >= cfg->take_pct) {
                r->exit_req = 1;
                r->exit_reason = 1;
            }
        }

        /* 5봉 연속 관측에서 현재가 직전·5봉 전보다 낮고 기준률 이하일 때만 저수익. */
        if (r->model_valid && synced && d2_sync(b)) {
            r->low_drop_ok = 0;
            r->low_extra_ok = 0;
            if (r->low_obs_date != b->sdate || r->low_obs_bar < 0 ||
                b->d2_day_index < r->low_obs_bar || b->d2_day_index > r->low_obs_bar + 1) {
                r->low_extra_peak = r->pair_pct;
                r->low_extra_wait = -1;
                r->low_hist_n = 0;
                r->low_p1 = r->low_p2 = r->low_p3 = r->low_p4 = r->low_p5 = 0.0;
            } else if (b->d2_day_index == r->low_obs_bar + 1) {
                r->low_p5 = r->low_p4;
                r->low_p4 = r->low_p3;
                r->low_p3 = r->low_p2;
                r->low_p2 = r->low_p1;
                r->low_p1 = r->low_cur;
                if (r->low_hist_n < 5) {
                    r->low_hist_n++;
                }
            }
            r->low_obs_date = b->sdate;
            r->low_obs_bar = b->d2_day_index;
            r->low_cur = r->pair_pct;
            r->low_drop_ok = r->low_hist_n >= 5 && r->pair_pct <= cfg->low_pct &&
                             r->pair_pct < r->low_p1 && r->pair_pct < r->low_p5;
            if (r->low_done && !r->low_extra_done && !r->first_done && !r->first_pending &&
                r->low_extra_bar >= 0 && b->d2_day_index > r->low_extra_bar) {
                const double width = dmax(0.01, cfg->low_rewiden);
                r->low_extra_peak = dmax(r->low_extra_peak, r->pair_pct);
                if (r->pair_pct > cfg->low_pct || r->low_extra_peak - r->pair_pct < width) {
                    r->low_extra_wait = -1;
                } else if (r->low_extra_wait < 0) {
                    r->low_extra_wait = b->d2_day_index;
                }
                r->low_extra_ok = r->low_hist_n >= 1 && r->low_extra_wait >= 0 &&
                                  b->d2_day_index - r->low_extra_wait >= imax(0, cfg->low_recover_min) &&
                                  r->pair_pct < r->low_p1 && r->pair_pct <= cfg->low_pct;
            }
        } else {
            r->low_drop_ok = 0;
            r->low_extra_ok = 0;
        }
        if (r->low_pending && !r->low_drop_ok && !rec_ok) {
            r->low_pending = 0;
        }
        /* 60분 하락 저수익 또는 30분 이후 손실봉 70%의 즉시 부분청산. 이름은 같다. */
        if (!r->exit_req && r->order_kind == TR_WPAIR_ORDER_NONE && !r->first_pending && !r->low_done && r->model_valid && synced &&
            r->entry_fut_bar >= 0 && r->entry_fut_date == b->sdate && d2_sync(b) &&
            ((r->entry_elapsed >= imax(0, cfg->low_wait_min) && r->low_drop_ok &&
              r->pair_pct <= cfg->low_pct) ||
             rec_ok)) {
            const double cut = dmin(100.0, dmax(0.0, cfg->low_cut_pct));
            r->low_done = 1;
            r->low_prev_qty = r->contracts;
            r->low_qty = int_portion((double)r->low_prev_qty * cut / 100.0);
            if (r->low_prev_qty >= 2 && cfg->low_cut_pct > 0.0) {
                r->low_qty = imin(r->low_prev_qty - 1, imax(1, r->low_qty));
            } else {
                r->low_qty = 0;
            }
            r->low_pending = r->low_qty > 0;
            if (r->low_pending) {
                r->low_extra_bar = b->d2_day_index;
                r->low_extra_peak = r->pair_pct;
                r->low_extra_wait = -1;
                if (rec_ok) {
                    r->rec_fire_bar = b->d2_day_index;
                }
            }
        }
        if (r->exit_req) {
            r->low_pending = 0;
        }
        if (r->low_pending && !r->exit_req) {
            mark_order(r, TR_WPAIR_ORDER_EXIT_PART, r->low_qty, 13,
                       path_exit_name(r, "시간저수익부분청산_삼선", "시간저수익부분청산_압축", "시간저수익부분청산"));
        }
        if (!r->exit_req && !r->first_done && !r->first_pending && r->low_extra_ok &&
            !r->low_extra_done && r->order_kind == TR_WPAIR_ORDER_NONE &&
            b->d2_day_index > r->low_extra_bar && r->pair_pct <= cfg->low_pct) {
            int32_t extra = int_portion((double)r->contracts * 0.3);
            if (r->contracts >= 2) {
                extra = imin(r->contracts - 1, imax(1, extra));
            } else {
                extra = 0;
            }
            if (extra > 0) {
                r->low_extra_done = 1;
                r->low_extra_wait = -1;
                mark_order(r, TR_WPAIR_ORDER_EXIT_PART, extra, 15,
                           path_exit_name(r, "저수익재악화추가청산_삼선", "저수익재악화추가청산_압축", "저수익재악화추가청산"));
            }
        }

        /* 양매수.txt 976~989. 익절 1·9만 나누고 손절·시간·날짜는 잔량을 모두 낸다. */
        if (r->exit_req && cfg->split_exit == 1 && (r->exit_reason == 1 || r->exit_reason == 9)) {
            r->first_done = 1;
            r->first_basis_pct = r->pair_pct;
            r->first_prev_qty = r->contracts;
            if (r->first_prev_qty == int_portion((double)r->first_prev_qty / 2.0) * 2) {
                r->first_qty = int_portion((double)r->first_prev_qty * 0.5);
            } else {
                r->first_qty = int_portion((double)r->first_prev_qty * 0.75);
            }
            r->first_reason = r->exit_reason;
            r->first_pending = r->first_qty > 0;
            r->exit_req = 0;
            r->exit_reason = 0;
        }
        if (r->exit_req) {
            r->first_pending = 0;
        }
        if (r->first_pending && !r->exit_req) {
            mark_order(r, TR_WPAIR_ORDER_EXIT_PART, r->first_qty, r->first_reason,
                       r->first_reason == 9
                           ? path_exit_name(r, "만기익절1차_삼선", "만기익절1차_압축", "만기익절1차")
                           : path_exit_name(r, "합산익절1차_삼선", "합산익절1차_압축", "합산익절1차"));
        }
        if (r->exit_req) {
            mark_order(r, TR_WPAIR_ORDER_EXIT_ALL, r->contracts, r->exit_reason,
                       exit_all_name(r->exit_reason, v12));
        }
    }

    /* 양매수.txt 1038~1082, 양매도.txt 1037~1076. */
    {
        int meet = 0;
        int entry_path = 0;
        if (side > 0) {
            const double applied = b->d2_day_index < imax(0, cfg->afternoon_switch_bar)
                                       ? cfg->morning_three_max
                                       : cfg->afternoon_three_max;
            const int morning = b->d2_day_index < imax(0, cfg->afternoon_switch_bar);
            int three_ok = b->d2_date == b->sdate && d2_1m(b) && b->three_ready == 1 &&
                           b->three_peak > 0.0 && b->three_ratio < applied;
            if (v12) {
                const int three_line = v12_three_ok(cfg, b, r->morn_hi_any);
                const int release = b->sq_release && b->three_ready == 1 && b->three_peak > 0.0 &&
                                    b->three_ratio <= 100.0;
                const int wide = b->sq_ready && b->sq_ratio >= 50.0;
                if (three_line) {
                    entry_path = 1;
                } else if (release) {
                    entry_path = 2;
                }
                three_ok = wide && entry_path != 0;
            } else if (yl_dow(b->bdate) == 5 && morning) {
                three_ok = three_ok && b->d2_time == b->stime && b->calc_ready == 1 &&
                           b->break_ready == 1 && b->two_max > 0.0 && b->price_ratio < 100.0;
            }
            if (v12 && b->d2_day_index < 30) {
                three_ok = 0;
            }
            if (period_ok && synced && three_ok && b->d2_day_index >= imax(0, cfg->entry_start_bar) &&
                b->stime < cfg->flat_time && b->next_sdate == b->sdate &&
                b->next_stime < cfg->flat_time && cfg->capital > 0.0 && cfg->take_pct > 0.0 &&
                (v12 || cfg->stop_pct > 0.0) &&
                (!v12 || (long_entry_px_ok(cfg, b) && !r->high_block &&
                          !(morning && r->morn_hi_any)))) {
                meet = ranges_meet(cfg, b);
            }
        } else {
            const int dow = yl_dow(b->bdate);
            const int day_ok = (dow == 2 || dow == 3 || dow == 5) && d2_1m(b);
            if (period_ok && synced && day_ok && b->stime >= imax64(INT64_C(90000), cfg->entry_start_time) &&
                b->stime < cfg->flat_time && b->next_sdate == b->sdate &&
                b->next_stime < cfg->flat_time && cfg->capital > 0.0 && cfg->take_pct > 0.0 &&
                cfg->stop_pct > 0.0) {
                meet = ranges_meet(cfg, b);
            }
        }
        if (cfg->trade_off == 0 && r->market_position == 0 && !r->used_today && !r->entry_pending &&
            !r->exit_req && meet && (!v12 || (b->close > 1.5 && b->d3_close > 1.5))) {
            r->used_today = 1;
            r->self_first = b->close;
            r->opp_first = b->d3_close;
            r->signal_time = b->stime;
            r->open_target_min = minute_of(b->stime) + 1;
            r->rec_meet = meet_px(cfg, b);
            r->self_entry = 0.0;
            r->opp_entry = 0.0;
            r->self_qty = int_portion((cfg->capital / 2.0) / (b->close * k_mult));
            r->opp_qty = int_portion((cfg->capital / 2.0) / (b->d3_close * k_mult));
            if (r->self_qty >= 1 && r->opp_qty >= 1) {
                r->signal_bar = b->index;
                r->signal_date = b->bdate;
                r->entry_pending = 1;
                r->entry_path = side > 0 && v12 ? entry_path : 0;
                mark_order(r, TR_WPAIR_ORDER_ENTRY, r->self_qty, 0,
                           r->entry_path == 2 ? "압축진입" : r->entry_path == 1 ? "삼선진입" : side > 0 ? "페어진입" : "양매도진입");
            }
        }
    }
}

static void fill_out(const tr_wpair_t *st, tr_wpair_out_t *out) {
    const tr_wpair_rt *r = &st->now;
    out->market_position = r->market_position;
    out->contracts = r->contracts;
    out->order_kind = r->order_kind;
    out->order_qty = r->order_qty;
    out->order_reason = r->order_reason;
    out->order_name = r->order_name != 0 ? r->order_name : "";
    out->entry_pending = r->entry_pending;
    out->exit_req = r->exit_req;
    out->exit_reason = r->exit_reason;
    out->used_today = r->used_today;
    out->model_valid = r->model_valid;
    out->holding = r->holding;
    out->first_done = r->first_done;
    out->first_pending = r->first_pending;
    out->first_qty = r->first_qty;
    out->first_reason = r->first_reason;
    out->low_done = r->low_done;
    out->low_pending = r->low_pending;
    out->low_qty = r->low_qty;
    out->pair_pnl = r->pair_pnl;
    out->pair_pct = r->pair_pct;
    out->self_resid_pct = r->self_resid_pct;
    out->book_realized = r->book_realized;
    out->book_unreal = r->book_unreal;
    out->book_total = r->book_total;
    out->book_total_pct = r->book_total_pct;
    out->book_qty = r->book_qty;
    out->self_qty = r->self_qty;
    out->opp_qty = r->opp_qty;
    out->self_first = r->self_first;
    out->opp_first = r->opp_first;
    out->self_entry = r->self_entry;
    out->entry_elapsed = r->entry_elapsed;
    out->resid_elapsed = r->resid_elapsed;
    out->signal_bar = r->signal_bar;
    out->trail_active = r->trail_active;
    out->trail_target = r->trail_target;
    out->trail_peak = r->trail_peak;
    out->trail_protect = r->trail_protect;
}

void tr_wpair_default_config(tr_wpair_config_t *cfg) {
    if (cfg == 0) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->capital = 10000000.0;
    cfg->take_pct = 10.0;
    cfg->stop_pct = 10.0;
    cfg->entry_start_bar = 15;
    cfg->afternoon_switch_bar = 195;
    cfg->morning_three_max = 70.0;
    cfg->afternoon_three_max = 50.0;
    cfg->entry_start_time = 90000;
    cfg->flat_time = 151500;
    cfg->meet_mode = 1;
    cfg->diag = 1;
    cfg->split_exit = 1;
    cfg->expiry_resid_pct = 60.0;
    cfg->resid_wait_min = 15;
    cfg->low_wait_min = 60;
    cfg->low_pct = 5.0;
    cfg->low_cut_pct = 30.0;
    cfg->low_rewiden = 5.0;
    cfg->low_recover_min = 30;
    cfg->ruleset = TR_WPAIR_RULES_V34;
    cfg->expiry_override = 0;
    cfg->early_second_min = 30;
    cfg->trail_interval = 10.0;
}

int tr_wpair_init(tr_wpair_t *st, int side, const tr_wpair_config_t *cfg) {
    if (st == 0 || cfg == 0 || (side != TR_WPAIR_LONG && side != TR_WPAIR_SHORT)) {
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->side = side;
    st->cfg = *cfg;
    st->now.entry_fut_bar = -1;
    st->now.signal_bar = -1;
    st->now.low_obs_bar = -1;
    st->now.low_extra_bar = -1;
    st->now.low_extra_wait = -1;
    st->now.first_fut_bar = -1;
    st->now.open_target_min = -1;
    st->now.rec_bar = -1;
    st->now.rec_fire_bar = -1;
    st->now.v9_peak_min = -1;
    st->now.v9_entry_min = -1;
    st->now.v9_last_bar = -1;
    st->now.order_name = "";
    return 0;
}

int tr_wpair_eval(tr_wpair_t *st, const tr_wpair_bar_t *bar, tr_wpair_out_t *out) {
    if (st == 0 || bar == 0) {
        return -1;
    }
    if (!st->has_bar || bar->index != st->bar_index) {
        if (st->has_bar && bar->index == st->bar_index + 1) {
            apply_fill(&st->now, st->side);
        } else if (st->has_bar) {
            clear_order(&st->now);
        }
        st->checkpoint = st->now;
        st->bar_index = bar->index;
        st->has_bar = 1;
    } else {
        st->now = st->checkpoint;
    }
    run_bar(st, bar);
    if (out != 0) {
        fill_out(st, out);
    }
    return 0;
}

/* 재진입하지 않는다. 명령 처리와 테스트가 한 스레드에서 순서대로 부른다. */
static tr_ksscore_t g_wpair_score;

static int64_t px_key(const tr_wpair_pxbar_t *b) {
    return b->ymd * INT64_C(1000000) + b->hhmmss;
}

/* 같은 시각의 첫 봉. 없으면 -1. 배열은 오래된 순이다. */
static int find_stamp(const tr_wpair_pxbar_t *a, size_t n, size_t *cursor, int64_t key) {
    while (*cursor < n) {
        int64_t k = px_key(&a[*cursor]);
        if (k < key) {
            (*cursor)++;
            continue;
        }
        if (k == key) {
            return (int)(*cursor)++;
        }
        return -1;
    }
    return -1;
}

static void push_event(tr_wpair_event_t *ev, size_t cap, size_t *n, size_t *total,
                       const tr_wpair_event_t *e) {
    (*total)++;
    if (ev == 0 || cap == 0) {
        return;
    }
    if (*n < cap) {
        ev[(*n)++] = *e;
        return;
    }
    memmove(ev, ev + 1, (cap - 1) * sizeof(*ev));
    ev[cap - 1] = *e;
}

static void apply_score(tr_wpair_bar_t *b, const tr_ksscore_out_t *o) {
    b->calc_ready = o->calc_ready;
    b->three_ready = o->three_ready;
    b->three_peak = o->three_peak;
    b->three_ratio = o->three_ratio;
    b->break_ready = o->break_ready;
    b->two_max = o->two_max;
    b->price_ratio = o->price_ratio;
}

static int replay_body(tr_wpair_t *st, const tr_wpair_pxbar_t *self, size_t nself,
                       const tr_wpair_pxbar_t *opp, size_t nopp, const tr_wpair_pxbar_t *fut,
                       size_t nfut, const tr_ksscore_config_t *score_cfg, tr_wpair_event_t *ev,
                       size_t cap, tr_yl_book_t *book) {
    tr_wpair_config_t cfg;
    tr_ksscore_config_t fallback;
    const tr_ksscore_config_t *scfg;
    size_t total = 0;
    size_t kept = 0;
    size_t fed = 0;
    size_t opp_at = 0;
    size_t fut_at = 0;
    int64_t gap_ymd = 0;
    double gap_open = 0.0;
    double gap_prev = 0.0;
    double gap_last = 0.0;
    int gap_has_prev = 0;
    int gap_has_today = 0;
    tr_wsq_point_t *sq = 0;

    if (st == 0 || (nself > 0 && self == 0) || (nopp > 0 && opp == 0) || (nfut > 0 && fut == 0) ||
        (cap > 0 && ev == 0)) {
        return -1;
    }
    cfg = st->cfg;
    if (tr_wpair_init(st, st->side, &cfg) != 0) {
        return -1;
    }
    scfg = score_cfg;
    if (scfg == 0) {
        tr_ksscore_default_config(&fallback, 0.05);
        scfg = &fallback;
    }
    if (!tr_ksscore_init(&g_wpair_score, scfg)) {
        return -1;
    }
    if (nfut > 0) {
        sq = calloc(nfut, sizeof(*sq));
        if (sq == 0 || tr_wsq_eval(fut, nfut, 0, sq) != 0) {
            free(sq);
            return -1;
        }
    }

    for (size_t i = 0; i < nself; i++) {
        const tr_wpair_pxbar_t *s = &self[i];
        tr_wpair_bar_t bar;
        tr_wpair_out_t out;
        int oi;
        int fi;
        int last = i + 1 == nself;

        while (fed < nfut && fut[fed].open_us <= s->open_us) {
            tr_ksscore_input_t in;
            if (fut[fed].ymd != gap_ymd) {
                if (gap_has_today) {
                    gap_prev = gap_last;
                    gap_has_prev = 1;
                }
                gap_ymd = fut[fed].ymd;
                gap_open = fut[fed].open;
                gap_has_today = 1;
            }
            gap_last = fut[fed].close;
            memset(&in, 0, sizeof(in));
            in.bdate = fut[fed].ymd;
            in.day_index = fut[fed].day_index;
            in.current_bar = (int32_t)(fed + 1);
            in.bar_open = fut[fed].open_us;
            in.cur_time = fut[fed].hhmmss;
            in.high = fut[fed].high;
            in.low = fut[fed].low;
            in.close = fut[fed].close;
            in.volume = fut[fed].volume;
            tr_ksscore_eval(&g_wpair_score, &in);
            fed++;
        }

        memset(&bar, 0, sizeof(bar));
        bar.index = (int32_t)(i + 1);
        bar.bdate = s->ymd;
        bar.sdate = s->ymd;
        bar.stime = s->hhmmss;
        bar.compress = 2;
        bar.interval = 1;
        bar.open = s->open;
        bar.high = s->high;
        bar.low = s->low;
        bar.close = s->close;
        if (last) {
            bar.next_sdate = s->ymd;
            bar.next_stime = s->hhmmss;
        } else {
            bar.next_sdate = self[i + 1].ymd;
            bar.next_stime = self[i + 1].hhmmss;
        }

        oi = find_stamp(opp, nopp, &opp_at, px_key(s));
        if (oi >= 0) {
            const tr_wpair_pxbar_t *o = &opp[oi];
            bar.d3_date = o->ymd;
            bar.d3_time = o->hhmmss;
            bar.d3_compress = 2;
            bar.d3_interval = 1;
            bar.d3_open = o->open;
            bar.d3_high = o->high;
            bar.d3_low = o->low;
            bar.d3_close = o->close;
        }
        fi = find_stamp(fut, nfut, &fut_at, px_key(s));
        if (fi >= 0) {
            const tr_wpair_pxbar_t *f = &fut[fi];
            bar.d2_date = f->ymd;
            bar.d2_time = f->hhmmss;
            bar.d2_compress = 2;
            bar.d2_interval = 1;
            bar.d2_day_index = f->day_index;
            if (gap_has_prev && gap_has_today && f->ymd == gap_ymd) {
                bar.d2_has_gap = 1;
                bar.d2_prev_close = gap_prev;
                bar.d2_day_open = gap_open;
            }
            if (sq != 0) {
                bar.sq_ready = sq[fi].ready;
                bar.sq_ratio = sq[fi].ratio;
                bar.sq_release = sq[fi].release;
            }
        }
        if (fed > 0) {
            apply_score(&bar, &g_wpair_score.out);
        }

        if (book != 0) {
            tr_yl_bar_t yb;
            memset(&yb, 0, sizeof(yb));
            yb.time = s->open_us / INT64_C(1000000);
            yb.open = s->open;
            yb.high = s->high;
            yb.low = s->low;
            yb.close = s->close;
            tr_yl_book_open(book, &yb);
        }
        if (tr_wpair_eval(st, &bar, &out) != 0) {
            free(sq);
            return -1;
        }
        if (book != 0 && out.order_kind != TR_WPAIR_ORDER_NONE) {
            tr_yl_order_t yo;
            memset(&yo, 0, sizeof(yo));
            yo.name = out.order_name != 0 ? out.order_name : "";
            yo.qty = out.order_qty;
            yo.price_type = TR_YL_MARKET;
            if (out.order_kind == TR_WPAIR_ORDER_ENTRY) {
                yo.action = st->side == TR_WPAIR_LONG ? TR_YL_BUY : TR_YL_SELL;
            } else {
                yo.action = st->side == TR_WPAIR_LONG ? TR_YL_EXIT_LONG : TR_YL_EXIT_SHORT;
            }
            tr_yl_book_submit(book, &yo);
        }
        if (book != 0) {
            tr_yl_book_close(book);
        }
        if (out.order_kind != TR_WPAIR_ORDER_NONE) {
            tr_wpair_event_t e;
            memset(&e, 0, sizeof(e));
            /* HTS는 시장가 주문을 다음 봉에 표시한다. 신호봉이 마지막이면 아직 체결봉이 없다. */
            e.time_sec = (last ? s->open_us : self[i + 1].open_us) / INT64_C(1000000);
            e.kind = out.order_kind;
            e.reason = out.order_reason;
            e.qty = out.order_qty;
            e.pending = last ? 1 : 0;
            e.contracts = out.contracts;
            e.name = out.order_name != 0 ? out.order_name : "";
            push_event(ev, cap, &kept, &total, &e);
        }
    }
    (void)kept;
    if (book != 0) {
        tr_yl_book_finish(book);
    }
    free(sq);
    return (int)total;
}

int tr_wpair_replay(tr_wpair_t *st, const tr_wpair_pxbar_t *self, size_t nself,
                    const tr_wpair_pxbar_t *opp, size_t nopp, const tr_wpair_pxbar_t *fut,
                    size_t nfut, const tr_ksscore_config_t *score_cfg, tr_wpair_event_t *ev,
                    size_t cap) {
    return replay_body(st, self, nself, opp, nopp, fut, nfut, score_cfg, ev, cap, 0);
}

int tr_wpair_fill(tr_wpair_t *st, const tr_wpair_pxbar_t *self, size_t nself,
                  const tr_wpair_pxbar_t *opp, size_t nopp, const tr_wpair_pxbar_t *fut,
                  size_t nfut, const tr_ksscore_config_t *score_cfg, tr_yl_book_t *book) {
    if (book == 0) {
        return -1;
    }
    return replay_body(st, self, nself, opp, nopp, fut, nfut, score_cfg, 0, 0, book);
}

static int minute_of_bar(int64_t hhmmss) {
    return (int)((hhmmss / 10000) * 60 + (hhmmss / 100) % 100);
}

static void push_width(double *hist, int *n, double width) {
    if (*n < 256) {
        (*n)++;
    }
    for (int i = *n - 1; i > 0; i--) {
        hist[i] = hist[i - 1];
    }
    hist[0] = width;
}

static double max_width(const double *hist, int n, int window) {
    double max = 0.0;
    int use = window < n ? window : n;
    for (int i = 0; i < use; i++) {
        if (hist[i] > max) {
            max = hist[i];
        }
    }
    return max;
}

void tr_wsq_default_config(tr_wsq_config_t *cfg) {
    if (cfg == 0) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->value_mult = 1.5;
    cfg->min_bars = 5;
    cfg->width_bars = 30;
    cfg->ratio_bars = 90;
    cfg->start_bar = 30;
    cfg->price_scale = 0.05;
}

int tr_wsq_eval(const tr_wpair_pxbar_t *fut, size_t n, const tr_wsq_config_t *cfg, tr_wsq_point_t *out) {
    tr_fxprof_t prof;
    double hist[256];
    int hist_n = 0;
    int session_bars = 0;
    int start_ok = 0;
    int prev_day = -1;
    int64_t prev_date = 0;
    int streak = 0;
    int prev_ready = 0;
    double prev_width = 0.0;
    int rel_min = -1;
    int64_t rel_date = 0;
    int n30 = 0;
    int n40 = 0;
    int n50 = 0;
    int n60 = 0;
    int begun = 0;
    tr_wsq_config_t fallback;

    if ((n > 0 && fut == 0) || (n > 0 && out == 0)) {
        return -1;
    }
    if (cfg == 0) {
        tr_wsq_default_config(&fallback);
        cfg = &fallback;
    }
    tr_fxprof_init(&prof);
    for (size_t i = 0; i < n; i++) {
        const tr_wpair_pxbar_t *b = &fut[i];
        const int day = b->day_index;
        const int reset = !begun || b->ymd != prev_date || day < prev_day || (day == 0 && prev_day != 0);
        tr_fxprof_input_t in;
        double width = 0.0;
        double span = 0.0;
        double ratio = 0.0;
        int valid = 0;
        int ready = 0;
        int prev_streak = streak;
        int minute = minute_of_bar(b->hhmmss);

        begun = 1;
        if (reset) {
            session_bars = 1;
            start_ok = day == 0 ? 1 : 0;
            streak = 0;
            prev_streak = 0;
            prev_ready = 0;
            prev_width = 0.0;
            rel_min = -1;
            rel_date = 0;
            n30 = n40 = n50 = n60 = 0;
            hist_n = 0;
        } else {
            session_bars++;
        }
        memset(&in, 0, sizeof(in));
        in.session_reset = reset;
        in.is_new_bar = true;
        in.bar_open = b->open_us;
        in.high = b->high;
        in.low = b->low;
        in.close = b->close;
        in.volume = b->volume;
        in.price_scale = cfg->price_scale > 0.0 ? cfg->price_scale : 0.05;
        in.value_mult = cfg->value_mult > 0.0 ? cfg->value_mult : 1.5;
        in.min_bars = cfg->min_bars > 0 ? cfg->min_bars : 5;
        in.period = cfg->width_bars > 0 ? cfg->width_bars : 30;
        tr_fxprof_eval(&prof, &in);
        valid = prof.out.valid;
        if (valid) {
            width = prof.out.hi1 - prof.out.lo1;
            if (width < 0.0) {
                width = 0.0;
            }
        }
        push_width(hist, &hist_n, width);
        {
            int ratio_bars = cfg->ratio_bars > 0 ? cfg->ratio_bars : 90;
            int window = session_bars < ratio_bars ? session_bars : ratio_bars;
            span = max_width(hist, hist_n, window);
        }
        if (span > 0.0) {
            ratio = width / span * 100.0;
        }
        {
            int start_bar = cfg->start_bar > 0 ? cfg->start_bar : 30;
            int width_bars = cfg->width_bars > 0 ? cfg->width_bars : 30;
            ready = start_ok && day >= start_bar && session_bars >= width_bars && valid && span > 0.0;
        }
        if (!ready) {
            n30 = n40 = n50 = n60 = 0;
            streak = 0;
            rel_min = -1;
        } else {
            n30 = ratio < 30.0 ? (reset ? 1 : n30 + 1) : 0;
            n40 = ratio < 40.0 ? (reset ? 1 : n40 + 1) : 0;
            n50 = ratio < 50.0 ? (reset ? 1 : n50 + 1) : 0;
            n60 = ratio < 60.0 ? (reset ? 1 : n60 + 1) : 0;
            if (ratio < 50.0) {
                streak = reset ? 1 : prev_streak + 1;
                rel_min = -1;
            } else {
                streak = 0;
                if (!reset && prev_ready && prev_streak >= 15 && width > prev_width) {
                    rel_min = minute;
                    rel_date = b->ymd;
                }
            }
        }
        out[i].ready = ready;
        out[i].ratio = ratio;
        out[i].release = ready && day >= 60 && rel_min >= 0 && b->ymd == rel_date && minute >= rel_min &&
                         minute - rel_min <= 30;
        out[i].n30 = ready && n30 > 0 ? n30 : 0;
        out[i].n40 = ready && n40 > 0 ? n40 : 0;
        out[i].n50 = ready && n50 > 0 ? n50 : 0;
        out[i].n60 = ready && n60 > 0 ? n60 : 0;
        prev_ready = ready;
        prev_width = width;
        prev_day = day;
        prev_date = b->ymd;
    }
    return 0;
}
