#include "core/signals/weekly_pair_v1.h"

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

static const char *exit_all_name(int reason) {
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
        return "만기잔량익절";
    case 12:
        return "만기잔량손실청산";
    default:
        return "기타청산";
    }
}

static void reset_day(tr_wpair_rt *r) {
    /* 양매수.txt 722~757. 청산사유·신호봉·진입대기는 여기서 지우지 않는다. */
    r->entry_fut_bar = -1;
    r->entry_fut_date = 0;
    r->entry_elapsed = 0;
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
        }
    } else if (r->order_kind == TR_WPAIR_ORDER_EXIT_ALL) {
        r->contracts = 0;
        r->market_position = 0;
    }
    clear_order(r);
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
    const int expiry = yl_dow(b->bdate) == 1 || yl_dow(b->bdate) == 4;
    const int period_ok = b->compress == 2 && b->interval == 1 && b->d3_compress == 2 &&
                          b->d3_interval == 1;
    const int synced = b->d3_date == b->sdate && b->d3_time == b->stime && b->close > 0.0 &&
                       b->d3_close > 0.0;

    clear_order(r);

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

    /* 양매수.txt 771~788. 모델은 신호 다음 봉, 같은 날, 동시봉일 때만 잠근다. */
    if (held && !r->holding) {
        r->holding = 1;
        r->entry_pending = 0;
        if (r->signal_date == b->bdate && b->index == r->signal_bar + 1 && synced) {
            r->self_entry = r->self_first;
            r->opp_entry = r->opp_first;
            r->model_notional =
                (r->self_first * (double)r->self_qty + r->opp_first * (double)r->opp_qty) * k_mult;
            r->model_valid = r->model_notional > 0.0;
        } else {
            r->model_valid = 0;
        }
    }

    /* 양매수.txt 791~804, 양매도.txt 800·805. 장부는 이 차트만, 수량 감소 봉 시가. */
    if (r->self_first > 0.0 && r->self_qty > 0 && (r->holding || r->book_qty > 0)) {
        int32_t dropped = imax(0, r->book_qty - r->contracts);
        r->book_principal = r->self_first * (double)r->self_qty * k_mult;
        if (dropped > 0) {
            r->book_realized += sgn * (b->open - r->self_first) * (double)dropped * k_mult;
        }
        r->book_qty = r->contracts;
        r->book_unreal = sgn * (b->close - r->self_first) * (double)r->book_qty * k_mult;
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
        /* 양매수.txt 864~871. 시간 청산은 모델이 없어도 실행되고 사유 4를 덮어쓴다. */
        if (b->stime >= cfg->flat_time ||
            (b->next_sdate == b->sdate && b->next_stime >= cfg->flat_time) ||
            b->next_sdate != b->sdate) {
            r->exit_req = 1;
            r->exit_reason = 3;
        }
        if (r->model_valid && synced && !r->exit_req) {
            const double self_px = b->close;
            const double opp_px = b->d3_close;
            const int ratio_ok = d2_sync(b) && b->calc_ready == 1 && b->break_ready == 1 &&
                                 b->two_max > 0.0;
            const int three_ok = d2_sync(b) && b->calc_ready == 1 && b->three_ready == 1 &&
                                 b->three_peak > 0.0 && b->three_ratio >= 70.0;
            r->pair_pnl = (sgn * (self_px - r->self_first) * (double)r->self_qty +
                           sgn * (opp_px - r->opp_first) * (double)r->opp_qty) *
                          k_mult;
            r->pair_pct = (sgn * (self_px / r->self_first - 1.0) +
                           sgn * (opp_px / r->opp_first - 1.0)) *
                          50.0;
            r->self_resid_pct = sgn * (self_px - r->self_entry) / r->self_entry * 100.0;
            if (r->pair_pct <= -cfg->stop_pct) {
                r->exit_req = 1;
                r->exit_reason = 2;
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

        /* 양매수.txt 946~960. 일차 상태와 따로며 일차처리를 켜지 않는다. */
        if (!r->exit_req && !r->first_pending && !r->low_done && r->model_valid && synced &&
            r->entry_fut_bar >= 0 && r->entry_fut_date == b->sdate && d2_sync(b) &&
            r->entry_elapsed >= imax(0, cfg->low_wait_min) && r->pair_pct <= cfg->low_pct) {
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
        }
        if (r->exit_req) {
            r->low_pending = 0;
        }
        if (r->low_pending && !r->exit_req) {
            mark_order(r, TR_WPAIR_ORDER_EXIT_PART, r->low_qty, 13, "시간저수익부분청산");
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
                       r->first_reason == 9 ? "만기익절1차" : "합산익절1차");
        }
        if (r->exit_req) {
            mark_order(r, TR_WPAIR_ORDER_EXIT_ALL, r->contracts, r->exit_reason,
                       exit_all_name(r->exit_reason));
        }
    }

    /* 양매수.txt 1038~1082, 양매도.txt 1037~1076. */
    {
        int meet = 0;
        if (side > 0) {
            const double applied = b->d2_day_index < imax(0, cfg->afternoon_switch_bar)
                                       ? cfg->morning_three_max
                                       : cfg->afternoon_three_max;
            int three_ok = b->d2_date == b->sdate && d2_1m(b) && b->three_ready == 1 &&
                           b->three_peak > 0.0 && b->three_ratio < applied;
            if (yl_dow(b->bdate) == 5 && b->d2_day_index < imax(0, cfg->afternoon_switch_bar)) {
                three_ok = three_ok && b->d2_time == b->stime && b->calc_ready == 1 &&
                           b->break_ready == 1 && b->two_max > 0.0 && b->price_ratio < 100.0;
            }
            if (period_ok && synced && three_ok && b->d2_day_index >= imax(0, cfg->entry_start_bar) &&
                b->stime < cfg->flat_time && b->next_sdate == b->sdate &&
                b->next_stime < cfg->flat_time && cfg->capital > 0.0 && cfg->take_pct > 0.0 &&
                cfg->stop_pct > 0.0) {
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
            !r->exit_req && meet) {
            r->used_today = 1;
            r->self_first = b->close;
            r->opp_first = b->d3_close;
            r->self_qty = int_portion((cfg->capital / 2.0) / (b->close * k_mult));
            r->opp_qty = int_portion((cfg->capital / 2.0) / (b->d3_close * k_mult));
            if (r->self_qty >= 1 && r->opp_qty >= 1) {
                r->signal_bar = b->index;
                r->signal_date = b->bdate;
                r->entry_pending = 1;
                mark_order(r, TR_WPAIR_ORDER_ENTRY, r->self_qty, 0,
                           side > 0 ? "페어진입" : "양매도진입");
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
    st->now.first_fut_bar = -1;
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

int tr_wpair_replay(tr_wpair_t *st, const tr_wpair_pxbar_t *self, size_t nself,
                    const tr_wpair_pxbar_t *opp, size_t nopp, const tr_wpair_pxbar_t *fut,
                    size_t nfut, const tr_ksscore_config_t *score_cfg, tr_wpair_event_t *ev,
                    size_t cap) {
    tr_wpair_config_t cfg;
    tr_ksscore_config_t fallback;
    const tr_ksscore_config_t *scfg;
    size_t total = 0;
    size_t kept = 0;
    size_t fed = 0;
    size_t opp_at = 0;
    size_t fut_at = 0;

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

    for (size_t i = 0; i < nself; i++) {
        const tr_wpair_pxbar_t *s = &self[i];
        tr_wpair_bar_t bar;
        tr_wpair_out_t out;
        int oi;
        int fi;
        int last = i + 1 == nself;

        while (fed < nfut && fut[fed].open_us <= s->open_us) {
            tr_ksscore_input_t in;
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
        }
        if (fed > 0) {
            apply_score(&bar, &g_wpair_score.out);
        }

        if (tr_wpair_eval(st, &bar, &out) != 0) {
            return -1;
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
    return (int)total;
}
