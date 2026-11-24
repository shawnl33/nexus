/* 위클리 페어 주문: 양매수 V34와 양매도 V3는 청산 사슬을 공유한다.
 * 체결은 다음 Index 봉 시가. 같은 Index는 체결 직후로 되돌려 다시 계산한다. */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/signals/weekly_pair_v1.h"

#define TUE 20241001
#define WED 20241002
#define THU 20241003
#define FRI 20241004
#define MON 20240930

static int near(double a, double b) {
    double d = a - b;
    if (d < 0.0) {
        d = -d;
    }
    return d < 1e-6;
}

static void fresh(tr_wpair_t *st, int side) {
    tr_wpair_config_t cfg;
    tr_wpair_default_config(&cfg);
    TR_CHECK(tr_wpair_init(st, side, &cfg) == 0);
}

static tr_wpair_bar_t bar_at(int32_t index, int64_t date, int64_t t, int32_t day, double px) {
    tr_wpair_bar_t b;
    memset(&b, 0, sizeof(b));
    b.index = index;
    b.bdate = date;
    b.sdate = date;
    b.stime = t;
    b.next_sdate = date;
    b.next_stime = t + 100;
    b.compress = 2;
    b.interval = 1;
    b.open = px;
    b.high = px + 1.0;
    b.low = px - 1.0;
    b.close = px;
    b.d3_date = date;
    b.d3_time = t;
    b.d3_compress = 2;
    b.d3_interval = 1;
    b.d3_open = px;
    b.d3_high = px + 1.0;
    b.d3_low = px - 1.0;
    b.d3_close = px;
    b.d2_date = date;
    b.d2_time = t;
    b.d2_compress = 2;
    b.d2_interval = 1;
    b.d2_day_index = day;
    b.d2_has_gap = 1;
    b.d2_prev_close = 100.0;
    b.d2_day_open = 100.0;
    b.calc_ready = 1;
    b.three_ready = 1;
    b.three_peak = 1.0;
    b.three_ratio = 10.0;
    b.break_ready = 1;
    b.two_max = 1.0;
    b.price_ratio = 50.0;
    b.sq_ready = 1;
    b.sq_ratio = 60.0;
    return b;
}

static void set_time(tr_wpair_bar_t *b, int64_t t, int64_t next_t) {
    b->stime = t;
    b->d2_time = t;
    b->d3_time = t;
    b->next_stime = next_t;
}

static tr_wpair_out_t step(tr_wpair_t *st, tr_wpair_bar_t b) {
    tr_wpair_out_t out;
    memset(&out, 0, sizeof(out));
    TR_CHECK(tr_wpair_eval(st, &b, &out) == 0);
    return out;
}

static void o_skip(tr_wpair_t *st, int32_t index, int64_t date);
static void again_ledger(tr_wpair_t *st, tr_wpair_bar_t b);

static int32_t open_pos(tr_wpair_t *st, int32_t index, int64_t date, double px, int32_t day,
                        tr_wpair_out_t *fill) {
    int32_t sig_day = day;
    if (st->side == TR_WPAIR_LONG && st->cfg.ruleset == TR_WPAIR_RULES_V12 && sig_day < 30) {
        sig_day = 30;
    }
    tr_wpair_bar_t sig_bar = bar_at(index, date, 100000, sig_day, px);
    if (st->side == TR_WPAIR_LONG && st->cfg.ruleset == TR_WPAIR_RULES_V12 && px >= 5.0) {
        sig_bar.low = 3.5;
        sig_bar.high = 4.5;
        sig_bar.d3_low = 3.5;
        sig_bar.d3_high = 4.5;
    }
    if (px < 2.1) {
        sig_bar.low = 1.2;
        sig_bar.high = 3.0;
        sig_bar.d3_low = 1.2;
        sig_bar.d3_high = 3.0;
    }
    tr_wpair_out_t sig = step(st, sig_bar);
    TR_CHECK(sig.order_kind == TR_WPAIR_ORDER_ENTRY);
    TR_CHECK(sig.market_position == 0);
    TR_CHECK(sig.entry_pending == 1);
    *fill = step(st, bar_at(index + 1, date, 100100, sig_day + 1, px));
    TR_CHECK(fill->market_position == st->side);
    TR_CHECK(fill->contracts == sig.order_qty);
    TR_CHECK(fill->model_valid == 1);
    TR_CHECK(fill->self_entry == px);
    TR_CHECK(fill->order_kind == TR_WPAIR_ORDER_NONE);
    return index + 2;
}

static void test_defaults_and_reject(void) {
    tr_wpair_config_t cfg;
    tr_wpair_t st;
    tr_wpair_default_config(&cfg);
    TR_CHECK(cfg.capital == 10000000.0);
    TR_CHECK(cfg.take_pct == 10.0);
    TR_CHECK(cfg.stop_pct == 10.0);
    TR_CHECK(cfg.entry_start_bar == 15);
    TR_CHECK(cfg.afternoon_switch_bar == 195);
    TR_CHECK(cfg.morning_three_max == 70.0);
    TR_CHECK(cfg.afternoon_three_max == 50.0);
    TR_CHECK(cfg.entry_start_time == 90000);
    TR_CHECK(cfg.flat_time == 151500);
    TR_CHECK(cfg.meet_mode == 1);
    TR_CHECK(cfg.split_exit == 1);
    TR_CHECK(cfg.expiry_resid_pct == 60.0);
    TR_CHECK(cfg.low_wait_min == 60);
    TR_CHECK(cfg.low_cut_pct == 30.0);
    TR_CHECK(tr_wpair_init(0, TR_WPAIR_LONG, &cfg) == -1);
    TR_CHECK(tr_wpair_init(&st, 0, &cfg) == -1);
    TR_CHECK(tr_wpair_init(&st, 2, &cfg) == -1);
    TR_CHECK(tr_wpair_init(&st, TR_WPAIR_LONG, 0) == -1);
    TR_CHECK(tr_wpair_eval(0, 0, 0) == -1);
}

static void test_long_entry_filters(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;

    fresh(&st, TR_WPAIR_LONG);
    o = step(&st, bar_at(1, TUE, 100000, 14, 10.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.used_today == 0);

    o = step(&st, bar_at(2, TUE, 100000, 15, 10.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);
    TR_CHECK(o.order_qty == 2);
    TR_CHECK(o.self_qty == 2);
    TR_CHECK(o.opp_qty == 2);
    TR_CHECK(strcmp(o.order_name, "페어진입") == 0);
    TR_CHECK(o.market_position == 0);

    fresh(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 100000, 20, 10.0);
    b.three_ratio = 70.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    b.three_ratio = 69.9;
    b.index = 2;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);

    fresh(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 85900, 20, 10.0);
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);

    fresh(&st, TR_WPAIR_LONG);
    b = bar_at(1, FRI, 100000, 20, 10.0);
    b.price_ratio = 100.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    b.index = 2;
    b.price_ratio = 99.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);

    fresh(&st, TR_WPAIR_LONG);
    b = bar_at(1, FRI, 100000, 195, 10.0);
    b.three_ratio = 60.0;
    b.price_ratio = 100.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    b.index = 2;
    b.three_ratio = 49.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);

    fresh(&st, TR_WPAIR_LONG);
    b = bar_at(1, MON, 100000, 20, 10.0);
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);
    TR_CHECK(o.order_qty == 2);
}

static void test_short_entry_filters(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;

    fresh(&st, TR_WPAIR_SHORT);
    b = bar_at(1, MON, 100000, 20, 10.0);
    b.three_ratio = 999.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);

    b = bar_at(2, THU, 100000, 20, 10.0);
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);

    b = bar_at(3, TUE, 85900, 1, 10.0);
    b.three_ratio = 999.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);

    b = bar_at(4, TUE, 90000, 1, 10.0);
    b.three_ratio = 999.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);
    TR_CHECK(strcmp(o.order_name, "양매도진입") == 0);
    TR_CHECK(o.order_qty == 2);

    fresh(&st, TR_WPAIR_SHORT);
    st.cfg.entry_start_time = 103000;
    o = step(&st, bar_at(1, WED, 100000, 1, 10.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    o = step(&st, bar_at(2, WED, 103000, 1, 10.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);

    fresh(&st, TR_WPAIR_SHORT);
    o = step(&st, bar_at(1, FRI, 100000, 1, 10.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);
}

static void test_meet_mode_and_size(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;

    fresh(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 100000, 20, 10.0);
    b.low = 9.0;
    b.high = 10.0;
    b.d3_low = 10.4;
    b.d3_high = 11.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    st.cfg.tolerance = 0.5;
    b.index = 2;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);

    fresh(&st, TR_WPAIR_LONG);
    st.cfg.meet_mode = 2;
    b = bar_at(1, TUE, 100000, 20, 10.0);
    b.d3_close = 10.4;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    st.cfg.tolerance = 0.5;
    b.index = 2;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);

    fresh(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 100000, 20, 10.0);
    b.d3_close = 1000.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.used_today == 1);
    TR_CHECK(o.self_qty == 2);
    TR_CHECK(o.opp_qty == 0);
    TR_CHECK(o.self_first == 10.0);
    b.index = 2;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);

    fresh(&st, TR_WPAIR_LONG);
    st.cfg.capital = 100000.0;
    o = step(&st, bar_at(1, TUE, 100000, 20, 10.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.used_today == 1);
    TR_CHECK(o.self_qty == 0);

    fresh(&st, TR_WPAIR_LONG);
    st.cfg.trade_off = 1;
    o = step(&st, bar_at(1, TUE, 100000, 20, 10.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.used_today == 0);
}

static void test_fill_model_and_reentry(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t sig, fill, again;

    fresh(&st, TR_WPAIR_LONG);
    sig = step(&st, bar_at(10, TUE, 100000, 20, 10.0));
    TR_CHECK(sig.order_kind == TR_WPAIR_ORDER_ENTRY);
    TR_CHECK(sig.order_qty == 2);
    b = bar_at(10, TUE, 100000, 20, 10.0);
    b.three_ratio = 90.0;
    again = step(&st, b);
    TR_CHECK(again.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(again.used_today == 0);
    TR_CHECK(again.market_position == 0);

    sig = step(&st, bar_at(10, TUE, 100000, 20, 10.0));
    TR_CHECK(sig.order_qty == 2);
    b = bar_at(11, TUE, 100100, 21, 10.0);
    b.open = 12.0;
    fill = step(&st, b);
    TR_CHECK(fill.market_position == 1);
    TR_CHECK(fill.contracts == 2);
    TR_CHECK(fill.model_valid == 1);
    TR_CHECK(fill.self_entry == 10.0);
    TR_CHECK(fill.self_first == 10.0);
    TR_CHECK(fill.book_realized == 0.0);
    TR_CHECK(fill.book_qty == 2);
    again = step(&st, b);
    TR_CHECK(again.contracts == 2);
    TR_CHECK(again.book_realized == 0.0);
    TR_CHECK(again.market_position == 1);

    fresh(&st, TR_WPAIR_LONG);
    sig = step(&st, bar_at(10, TUE, 100000, 20, 10.0));
    TR_CHECK(sig.entry_pending == 1);
    o_skip(&st, 12, WED);
}

static void o_skip(tr_wpair_t *st, int32_t index, int64_t date) {
    tr_wpair_out_t o = step(st, bar_at(index, date, 100000, 20, 10.0));
    TR_CHECK(o.market_position == 0);
    TR_CHECK(o.entry_pending == 0);
    TR_CHECK(o.used_today == 1);
    TR_CHECK(o.exit_req == 1);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    o = step(st, bar_at(index + 1, date, 100100, 21, 10.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    o = step(st, bar_at(index + 2, THU, 100000, 20, 10.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);
    TR_CHECK(o.used_today == 1);
}

static void test_confirm_miss_and_same_day_block(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;

    fresh(&st, TR_WPAIR_LONG);
    o = step(&st, bar_at(1, TUE, 100000, 20, 10.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);
    b = bar_at(2, TUE, 100100, 21, 10.0);
    b.d3_time = 100200;
    o = step(&st, b);
    TR_CHECK(o.market_position == 1);
    TR_CHECK(o.contracts == 2);
    TR_CHECK(o.model_valid == 0);
    TR_CHECK(o.holding == 1);
    TR_CHECK(o.self_entry == 0.0);
    b = bar_at(3, TUE, 100200, 22, 12.0);
    b.d3_time = 100300;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    set_time(&b, 151500, 151600);
    b.index = 3;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_ALL);
    TR_CHECK(o.order_reason == 3);
    TR_CHECK(strcmp(o.order_name, "시간청산") == 0);
    TR_CHECK(o.contracts == 2);

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 10.0, 20, &o);
    b = bar_at(next, TUE, 110000, 30, 10.0);
    set_time(&b, 151500, 151600);
    o = step(&st, b);
    TR_CHECK(o.order_reason == 3);
    TR_CHECK(o.contracts == 2);
    b = bar_at(next + 1, TUE, 151600, 31, 10.0);
    b.open = 12.0;
    o = step(&st, b);
    TR_CHECK(o.market_position == 0);
    TR_CHECK(o.book_realized == 1000000.0);
    TR_CHECK(o.used_today == 1);
    TR_CHECK(o.holding == 0);
    o = step(&st, bar_at(next + 2, TUE, 140000, 40, 10.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    o = step(&st, bar_at(next + 3, WED, 100000, 20, 10.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);
}

static void test_pnl_sign_stop_and_time(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;
    double expect;

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 10.0, 20, &o);
    b = bar_at(next, TUE, 110000, 30, 9.0);
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.pair_pct > -10.0);

    b = bar_at(next + 1, TUE, 110100, 31, 8.0);
    b.three_ratio = 40.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_ALL);
    TR_CHECK(o.order_reason == 2);
    TR_CHECK(strcmp(o.order_name, "합산손절") == 0);
    TR_CHECK(o.order_qty == 2);
    TR_CHECK(o.pair_pct <= -10.0);
    expect = ((8.0 / 10.0 - 1.0) + (8.0 / 10.0 - 1.0)) * 50.0;
    TR_CHECK(near(o.pair_pct, expect));
    expect = ((8.0 - 10.0) * 2.0 + (8.0 - 10.0) * 2.0) * 250000.0;
    TR_CHECK(near(o.pair_pnl, expect));

    fresh(&st, TR_WPAIR_SHORT);
    next = open_pos(&st, 1, TUE, 10.0, 20, &o);
    b = bar_at(next, TUE, 110000, 30, 9.0);
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.pair_pct < 10.0);
    b = bar_at(next + 1, TUE, 110100, 31, 8.0);
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 1);
    TR_CHECK(o.order_qty == 1);
    TR_CHECK(strcmp(o.order_name, "합산익절1차") == 0);
    TR_CHECK(o.pair_pct >= 10.0);
    TR_CHECK(o.exit_req == 0);
    TR_CHECK(o.first_done == 1);
    expect = ((1.0 - 8.0 / 10.0) + (1.0 - 8.0 / 10.0)) * 50.0;
    TR_CHECK(near(o.pair_pct, expect));

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 10.0, 20, &o);
    b = bar_at(next, TUE, 151500, 30, 8.0);
    o = step(&st, b);
    TR_CHECK(o.order_reason == 3);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_ALL);

    b = bar_at(next, TUE, 151400, 30, 8.0);
    b.next_stime = 151500;
    b.d2_time = 151400;
    b.d3_time = 151400;
    o = step(&st, b);
    TR_CHECK(o.order_reason == 3);

    b = bar_at(next, TUE, 110000, 30, 10.0);
    b.next_sdate = WED;
    o = step(&st, b);
    TR_CHECK(o.order_reason == 3);
    TR_CHECK(o.contracts == 2);
}

static void test_partial_profit_and_protection(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 10.0, 20, &o);
    b = bar_at(next, TUE, 110000, 30, 11.0);
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 1);
    TR_CHECK(o.order_qty == 1);
    TR_CHECK(o.first_done == 1);
    TR_CHECK(o.first_pending == 1);
    TR_CHECK(o.exit_req == 0);
    TR_CHECK(strcmp(o.order_name, "합산익절1차") == 0);
    TR_CHECK(o.contracts == 2);

    b = bar_at(next, TUE, 110000, 30, 10.0);
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.first_done == 0);
    TR_CHECK(o.contracts == 2);

    b = bar_at(next, TUE, 110000, 30, 11.0);
    o = step(&st, b);
    TR_CHECK(o.order_qty == 1);
    b = bar_at(next + 1, TUE, 110100, 31, 10.5);
    b.three_ready = 0;
    o = step(&st, b);
    TR_CHECK(o.contracts == 1);
    TR_CHECK(o.first_pending == 0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_ALL);
    TR_CHECK(o.order_reason == 10);
    TR_CHECK(strcmp(o.order_name, "잔량익절보호") == 0);
    TR_CHECK(o.order_qty == 1);

    fresh(&st, TR_WPAIR_LONG);
    st.cfg.split_exit = 0;
    next = open_pos(&st, 1, TUE, 10.0, 20, &o);
    b = bar_at(next, TUE, 110000, 30, 11.0);
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_ALL);
    TR_CHECK(o.order_reason == 1);
    TR_CHECK(strcmp(o.order_name, "합산익절") == 0);
    TR_CHECK(o.first_done == 0);
    TR_CHECK(o.order_qty == 2);

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 20.0, 20, &o);
    TR_CHECK(o.contracts == 1);
    b = bar_at(next, TUE, 110000, 30, 22.0);
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.first_done == 1);
    TR_CHECK(o.first_qty == 0);
    TR_CHECK(o.first_pending == 0);
    b = bar_at(next + 1, TUE, 110100, 31, 20.0);
    o = step(&st, b);
    TR_CHECK(o.order_reason == 10);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_ALL);
}

static void test_expiry_priority(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, THU, 10.0, 20, &o);
    b = bar_at(next, THU, 110000, 30, 11.0);
    b.price_ratio = 100.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    b = bar_at(next + 1, THU, 110100, 31, 11.5);
    b.price_ratio = 100.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    b = bar_at(next + 2, THU, 110200, 32, 11.6);
    b.price_ratio = 99.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    b = bar_at(next + 2, THU, 110200, 32, 11.6);
    b.price_ratio = 100.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 9);
    TR_CHECK(o.order_qty == 1);
    TR_CHECK(strcmp(o.order_name, "만기익절1차") == 0);
    TR_CHECK(o.first_done == 1);
    TR_CHECK(o.exit_req == 0);

    b = bar_at(next + 3, THU, 110300, 40, 9.0);
    b.price_ratio = 100.0;
    b.three_ratio = 80.0;
    o = step(&st, b);
    TR_CHECK(o.contracts == 1);
    TR_CHECK(o.order_reason == 12);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_ALL);
    TR_CHECK(strcmp(o.order_name, "만기잔량손실청산") == 0);
    TR_CHECK(o.self_resid_pct < 0.0);

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, THU, 10.0, 20, &o);
    b = bar_at(next, THU, 110000, 30, 11.6);
    b.price_ratio = 100.0;
    o = step(&st, b);
    TR_CHECK(o.order_reason == 9);
    b = bar_at(next + 1, THU, 110100, 31, 8.0);
    b.price_ratio = 100.0;
    b.three_ratio = 80.0;
    o = step(&st, b);
    TR_CHECK(o.order_reason == 2);
    TR_CHECK(strcmp(o.order_name, "합산손절") == 0);

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, THU, 6.0, 20, &o);
    TR_CHECK(o.contracts == 3);
    b = bar_at(next, THU, 110000, 30, 7.0);
    b.price_ratio = 100.0;
    o = step(&st, b);
    TR_CHECK(o.order_reason == 9);
    TR_CHECK(o.order_qty == 2);

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, THU, 10.0, 20, &o);
    b = bar_at(next, THU, 110000, 30, 11.6);
    b.price_ratio = 100.0;
    o = step(&st, b);
    TR_CHECK(o.order_reason == 9);
    b = bar_at(next + 1, THU, 110100, 40, 10.5);
    b.price_ratio = 100.0;
    b.three_ratio = 69.0;
    o = step(&st, b);
    TR_CHECK(o.contracts == 1);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    b.index = next + 2;
    b.d2_day_index = 41;
    b.three_ratio = 70.0;
    o = step(&st, b);
    TR_CHECK(o.order_reason == 10);
    TR_CHECK(strcmp(o.order_name, "잔량익절보호") == 0);
}

static void test_residual_take_and_low_cut(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, THU, 10.0, 20, &o);
    b = bar_at(next, THU, 110000, 30, 11.6);
    b.price_ratio = 100.0;
    o = step(&st, b);
    TR_CHECK(o.order_reason == 9);
    b = bar_at(next + 1, THU, 110100, 30, 11.6);
    b.price_ratio = 99.0;
    o = step(&st, b);
    TR_CHECK(o.contracts == 1);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.resid_elapsed == 0);
    b = bar_at(next + 2, THU, 120000, 44, 16.0);
    b.price_ratio = 100.0;
    b.three_ratio = 80.0;
    o = step(&st, b);
    TR_CHECK(o.resid_elapsed == 14);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    b = bar_at(next + 3, THU, 120100, 45, 16.0);
    b.price_ratio = 100.0;
    b.three_ratio = 80.0;
    st.cfg.take_pct = 80.0;
    o = step(&st, b);
    TR_CHECK(o.resid_elapsed == 15);
    TR_CHECK(o.self_resid_pct >= 60.0);
    TR_CHECK(o.self_resid_pct < 80.0);
    TR_CHECK(o.order_reason == 11);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_ALL);
    TR_CHECK(strcmp(o.order_name, "만기잔량익절") == 0);

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 10.0, 20, &o);
    b = bar_at(next, TUE, 120000, 81, 10.6);
    o = step(&st, b);
    TR_CHECK(o.entry_elapsed == 60);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    {
        const double px[5] = {10.5, 10.4, 10.3, 10.2, 10.1};
        int k;
        for (k = 0; k < 5; k++) {
            b = bar_at(next + 1 + k, TUE, 120100 + k * 100, 82 + k, px[k]);
            o = step(&st, b);
        }
    }
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 13);
    TR_CHECK(o.order_qty == 1);
    TR_CHECK(o.low_done == 1);
    TR_CHECK(o.first_done == 0);
    TR_CHECK(strcmp(o.order_name, "시간저수익부분청산") == 0);
    set_time(&b, 151500, 151600);
    o = step(&st, b);
    TR_CHECK(o.order_reason == 3);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_ALL);
    TR_CHECK(o.low_pending == 0);
    TR_CHECK(o.contracts == 2);
    TR_CHECK(o.low_done == 0);

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 2.0, 20, &o);
    TR_CHECK(o.contracts == 10);
    {
        const double px[6] = {2.12, 2.10, 2.08, 2.06, 2.04, 2.02};
        int k;
        for (k = 0; k < 6; k++) {
            b = bar_at(next + k, TUE, 120000 + k * 100, 81 + k, px[k]);
            o = step(&st, b);
        }
    }
    TR_CHECK(o.order_reason == 13);
    TR_CHECK(o.order_qty == 3);
    TR_CHECK(o.first_done == 0);
    b = bar_at(next + 6, TUE, 120600, 87, 2.0);
    o = step(&st, b);
    TR_CHECK(o.contracts == 7);
    TR_CHECK(o.low_pending == 0);
    TR_CHECK(o.first_done == 0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    b = bar_at(next + 7, TUE, 120700, 88, 2.2);
    o = step(&st, b);
    TR_CHECK(o.order_reason == 1);
    TR_CHECK(o.order_qty == 5);
    TR_CHECK(o.first_done == 1);
    TR_CHECK(o.low_done == 1);

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 20.0, 20, &o);
    TR_CHECK(o.contracts == 1);
    {
        const double px[6] = {21.2, 21.0, 20.8, 20.6, 20.4, 20.2};
        int k;
        for (k = 0; k < 6; k++) {
            b = bar_at(next + k, TUE, 120000 + k * 100, 81 + k, px[k]);
            o = step(&st, b);
        }
    }
    TR_CHECK(o.low_done == 1);
    TR_CHECK(o.low_qty == 0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    b.index = next + 1;
    b.d2_day_index = 90;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
}

static void test_date_change_and_ledger(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 10.0, 20, &o);
    b = bar_at(next, WED, 100000, 1, 10.0);
    o = step(&st, b);
    TR_CHECK(o.market_position == 1);
    TR_CHECK(o.order_reason == 4);
    TR_CHECK(strcmp(o.order_name, "날짜변경청산") == 0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_ALL);
    TR_CHECK(o.used_today == 1);
    TR_CHECK(o.model_valid == 1);

    b = bar_at(next, WED, 151500, 1, 10.0);
    o = step(&st, b);
    TR_CHECK(o.order_reason == 3);

    b = bar_at(next, WED, 100000, 1, 10.0);
    o = step(&st, b);
    TR_CHECK(o.order_reason == 4);
    b = bar_at(next + 1, WED, 100100, 2, 10.0);
    b.open = 12.0;
    o = step(&st, b);
    TR_CHECK(o.market_position == 0);
    TR_CHECK(o.book_realized == 1000000.0);
    TR_CHECK(o.used_today == 1);
    o = step(&st, bar_at(next + 2, WED, 110000, 10, 10.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 10.0, 20, &o);
    b = bar_at(next, TUE, 151500, 40, 10.0);
    o = step(&st, b);
    TR_CHECK(o.order_reason == 3);
    b = bar_at(next + 1, WED, 90000, 1, 10.0);
    b.open = 12.0;
    o = step(&st, b);
    TR_CHECK(o.market_position == 0);
    TR_CHECK(o.book_realized == 0.0);
    TR_CHECK(o.used_today == 0);
    TR_CHECK(o.book_qty == 0);

    fresh(&st, TR_WPAIR_LONG);
    o = step(&st, bar_at(5, TUE, 100000, 20, 10.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);
    b = bar_at(6, TUE, 100100, 21, 10.0);
    b.open = 11.0;
    o = step(&st, b);
    TR_CHECK(o.book_realized == 0.0);
    TR_CHECK(o.book_unreal == 0.0);
    TR_CHECK(o.book_qty == 2);
    b = bar_at(7, TUE, 110000, 30, 11.0);
    o = step(&st, b);
    TR_CHECK(o.order_reason == 1);
    TR_CHECK(o.order_qty == 1);
    b = bar_at(8, TUE, 110100, 31, 12.0);
    b.open = 12.0;
    o = step(&st, b);
    TR_CHECK(o.contracts == 1);
    TR_CHECK(o.book_realized == 500000.0);
    TR_CHECK(o.book_unreal == 500000.0);
    TR_CHECK(near(o.book_total_pct, 20.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    again_ledger(&st, b);

    fresh(&st, TR_WPAIR_SHORT);
    o = step(&st, bar_at(5, TUE, 100000, 20, 10.0));
    b = bar_at(6, TUE, 100100, 21, 10.0);
    o = step(&st, b);
    TR_CHECK(o.market_position == -1);
    b = bar_at(7, TUE, 110000, 30, 8.0);
    o = step(&st, b);
    TR_CHECK(o.order_reason == 1);
    b = bar_at(8, TUE, 110100, 31, 8.0);
    b.open = 8.0;
    o = step(&st, b);
    TR_CHECK(o.contracts == 1);
    TR_CHECK(o.book_realized == 500000.0);
}

static void again_ledger(tr_wpair_t *st, tr_wpair_bar_t b) {
    tr_wpair_out_t o = step(st, b);
    TR_CHECK(o.book_realized == 500000.0);
    TR_CHECK(o.contracts == 1);
}

static tr_wpair_pxbar_t px_at(int64_t ymd, int64_t hhmmss, int32_t day, double price, int64_t open_us) {
    tr_wpair_pxbar_t b;
    memset(&b, 0, sizeof(b));
    b.open_us = open_us;
    b.ymd = ymd;
    b.hhmmss = hhmmss;
    b.day_index = day;
    b.open = price;
    b.high = price + 1.0;
    b.low = price - 1.0;
    b.close = price;
    b.volume = 1.0;
    return b;
}

static void test_replay_signals(void) {
    tr_wpair_t st;
    tr_wpair_pxbar_t self[8];
    tr_wpair_pxbar_t opp[8];
    tr_wpair_pxbar_t fut[8];
    tr_wpair_event_t ev[8];
    int n;

    fresh(&st, TR_WPAIR_SHORT);
    self[0] = px_at(TUE, 90000, 0, 10.0, 1000000);
    opp[0] = self[0];
    fut[0] = self[0];
    n = tr_wpair_replay(&st, self, 1, opp, 1, fut, 1, 0, ev, 8);
    TR_CHECK(n == 1);
    TR_CHECK(ev[0].kind == TR_WPAIR_ORDER_ENTRY);
    TR_CHECK(ev[0].pending == 1);
    TR_CHECK(ev[0].qty == 2);
    TR_CHECK(strcmp(ev[0].name, "양매도진입") == 0);
    TR_CHECK(st.now.market_position == 0);
    TR_CHECK(st.now.entry_pending == 1);

    self[1] = px_at(TUE, 90100, 1, 10.0, 1060000);
    opp[1] = self[1];
    fut[1] = self[1];
    self[2] = px_at(TUE, 90200, 2, 8.0, 1120000);
    opp[2] = self[2];
    fut[2] = self[2];
    n = tr_wpair_replay(&st, self, 3, opp, 3, fut, 3, 0, ev, 8);
    TR_CHECK(n == 2);
    TR_CHECK(ev[0].kind == TR_WPAIR_ORDER_ENTRY);
    TR_CHECK(ev[0].pending == 0);
    TR_CHECK(ev[0].time_sec == 1060000 / 1000000);
    TR_CHECK(ev[1].kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(ev[1].reason == 1);
    TR_CHECK(ev[1].qty == 1);
    TR_CHECK(ev[1].pending == 1);
    TR_CHECK(strcmp(ev[1].name, "합산익절1차") == 0);
    TR_CHECK(st.now.market_position == TR_WPAIR_SHORT);
    TR_CHECK(st.now.contracts == 2);

    opp[0].hhmmss = 85900;
    n = tr_wpair_replay(&st, self, 2, opp, 2, fut, 2, 0, ev, 8);
    TR_CHECK(n == 1);
    TR_CHECK(ev[0].time_sec == 1060000 / 1000000);
    TR_CHECK(ev[0].pending == 1);
    TR_CHECK(st.now.market_position == 0);

    n = tr_wpair_replay(&st, self, 2, opp, 2, fut, 0, 0, ev, 8);
    TR_CHECK(n == 0);
    TR_CHECK(st.now.market_position == 0);

    fresh(&st, TR_WPAIR_LONG);
    opp[0] = self[0];
    fut[0] = self[0];
    n = tr_wpair_replay(&st, self, 3, opp, 3, fut, 3, 0, ev, 8);
    TR_CHECK(n == 0);

    fresh(&st, TR_WPAIR_SHORT);
    fut[0] = px_at(TUE, 85900, 0, 10.0, 940000);
    fut[1] = px_at(TUE, 90000, 1, 10.0, 1000000);
    self[0] = px_at(TUE, 90000, 0, 10.0, 1000000);
    self[1] = px_at(TUE, 90100, 1, 10.0, 1060000);
    opp[0] = self[0];
    opp[1] = self[1];
    n = tr_wpair_replay(&st, self, 2, opp, 2, fut, 2, 0, ev, 8);
    TR_CHECK(n == 1);
    TR_CHECK(ev[0].pending == 0);
    TR_CHECK(st.now.market_position == TR_WPAIR_SHORT);
    TR_CHECK(st.now.contracts == 2);

    TR_CHECK(tr_wpair_replay(0, self, 1, opp, 1, fut, 1, 0, ev, 8) == -1);
    TR_CHECK(tr_wpair_replay(&st, 0, 1, opp, 1, fut, 1, 0, ev, 8) == -1);
}

static void test_trade_off_keeps_exit(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 10.0, 20, &o);
    st.cfg.trade_off = 1;
    b = bar_at(next, TUE, 151500, 40, 10.0);
    o = step(&st, b);
    TR_CHECK(o.order_reason == 3);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_ALL);
}

static void fresh_v45(tr_wpair_t *st, int side) {
    tr_wpair_config_t cfg;
    tr_wpair_default_config(&cfg);
    cfg.ruleset = TR_WPAIR_RULES_V12;
    TR_CHECK(tr_wpair_init(st, side, &cfg) == 0);
}

static int32_t v45_first_done(tr_wpair_t *st) {
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;

    fresh_v45(st, TR_WPAIR_LONG);
    next = open_pos(st, 1, THU, 10.0, 20, &o);
    b = bar_at(next, THU, 110000, 40, 11.6);
    b.price_ratio = 100.0;
    o = step(st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 9);
    TR_CHECK(o.first_done == 1);
    TR_CHECK(o.first_pending == 1);
    b = bar_at(next + 1, THU, 110100, 41, 11.6);
    b.price_ratio = 100.0;
    o = step(st, b);
    TR_CHECK(o.contracts == 1);
    TR_CHECK(o.first_done == 1);
    TR_CHECK(o.first_pending == 0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    return next + 2;
}

static void test_v45_defaults_and_mode_scope(void) {
    tr_wpair_config_t cfg;
    tr_wpair_t st;
    tr_wpair_out_t o;
    int32_t next;

    tr_wpair_default_config(&cfg);
    TR_CHECK(cfg.ruleset == TR_WPAIR_RULES_V34);
    TR_CHECK(cfg.expiry_override == 0);
    TR_CHECK(cfg.early_second_min == 30);
    TR_CHECK(near(cfg.trail_interval, 10.0));
    TR_CHECK(near(cfg.expiry_resid_pct, 60.0));
    TR_CHECK(cfg.resid_wait_min == 15);

    fresh(&st, TR_WPAIR_LONG);
    o = step(&st, bar_at(1, TUE, 100000, 20, 1.5));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);

    fresh_v45(&st, TR_WPAIR_SHORT);
    o = step(&st, bar_at(1, TUE, 100000, 20, 1.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);
    TR_CHECK(o.order_qty == 20);

    fresh_v45(&st, TR_WPAIR_SHORT);
    st.cfg.expiry_override = 1;
    next = open_pos(&st, 1, TUE, 10.0, 20, &o);
    o = step(&st, bar_at(next, TUE, 110000, 30, 8.9));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 1);
    TR_CHECK(strcmp(o.order_name, "합산익절1차") == 0);
}

static void test_v45_entry_price_gate(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;

    fresh_v45(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 100000, 20, 1.5);
    b.d3_close = 2.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.used_today == 0);
    TR_CHECK(o.self_first == 0.0);
    TR_CHECK(o.self_qty == 0);

    fresh_v45(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 100000, 20, 2.0);
    b.d3_close = 1.5;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.used_today == 0);
    TR_CHECK(o.opp_first == 0.0);
    TR_CHECK(o.opp_qty == 0);

    fresh_v45(&st, TR_WPAIR_LONG);
    o = step(&st, bar_at(1, TUE, 100000, 20, 1.5));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    b = bar_at(2, TUE, 100100, 41, 2.0);
    b.d3_close = 4.0;
    b.d3_low = 3.1;
    b.d3_high = 4.1;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.used_today == 0);
    b = bar_at(3, TUE, 100200, 42, 2.0);
    b.low = 1.2;
    b.d3_low = 1.2;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);
    TR_CHECK(o.self_first == 2.0);
    TR_CHECK(o.opp_first == 2.0);
}

static void test_v45_entry_budget_after_price_gate(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;

    fresh_v45(&st, TR_WPAIR_LONG);
    st.cfg.capital = 100000.0;
    b = bar_at(1, TUE, 100000, 40, 2.0);
    b.low = 1.2;
    b.high = 3.0;
    b.d3_low = 1.2;
    b.d3_high = 3.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.used_today == 1);
    TR_CHECK(o.self_first == 2.0);
    TR_CHECK(o.opp_first == 2.0);
    TR_CHECK(o.self_qty == 0);
    TR_CHECK(o.opp_qty == 0);
    st.cfg.capital = 10000000.0;
    b = bar_at(2, TUE, 100100, 41, 2.0);
    b.low = 1.2;
    b.high = 3.0;
    b.d3_low = 1.2;
    b.d3_high = 3.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.used_today == 1);
}

static void test_v45_first_profit_thresholds(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;

    fresh_v45(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 2.0, 20, &o);
    b = bar_at(next, TUE, 110000, 40, 2.2);
    b.price_ratio = 50.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.first_done == 0);
    b = bar_at(next + 1, TUE, 110100, 41, 2.4);
    b.price_ratio = 50.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 1);
    TR_CHECK(o.first_done == 1);
    TR_CHECK(o.first_pending == 1);

    fresh_v45(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, THU, 3.0, 20, &o);
    b = bar_at(next, THU, 110000, 40, 3.31);
    b.price_ratio = 50.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 9);
    TR_CHECK(strcmp(o.order_name, "만기익절1차_삼선") == 0);
    TR_CHECK(o.first_done == 1);
}

static void test_v45_first_low_price_exact_boundary(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;

    fresh_v45(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, THU, 2.0, 20, &o);
    st.cfg.take_pct = 25.0;
    b = bar_at(next, THU, 110000, 40, nextafter(2.75, 0.0));
    b.price_ratio = 50.0;
    o = step(&st, b);
    TR_CHECK(o.pair_pct < 37.5);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.first_done == 0);

    b.close = 2.75;
    b.d3_close = 2.75;
    o = step(&st, b);
    TR_CHECK(o.pair_pct == 37.5);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 9);
    TR_CHECK(o.first_done == 1);

    b.close = nextafter(2.75, INFINITY);
    b.d3_close = b.close;
    o = step(&st, b);
    TR_CHECK(o.pair_pct > 37.5);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 9);
}

static void test_v45_first_high_price_exact_boundary(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;

    fresh_v45(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, THU, 4.0, 20, &o);
    st.cfg.take_pct = 25.0;
    b = bar_at(next, THU, 110000, 40, nextafter(5.0, 0.0));
    b.price_ratio = 50.0;
    o = step(&st, b);
    TR_CHECK(o.pair_pct < 25.0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.first_done == 0);

    b.close = 5.0;
    b.d3_close = 5.0;
    o = step(&st, b);
    TR_CHECK(o.pair_pct == 25.0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 9);
    TR_CHECK(o.first_done == 1);

    b.close = nextafter(5.0, INFINITY);
    b.d3_close = b.close;
    o = step(&st, b);
    TR_CHECK(o.pair_pct > 25.0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 9);
}

static void test_v45_expiry_override_keeps_friday_filter(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;

    fresh_v45(&st, TR_WPAIR_LONG);
    b = bar_at(1, FRI, 100000, 40, 2.0);
    b.low = 1.2;
    b.high = 3.0;
    b.d3_low = 1.2;
    b.d3_high = 3.0;
    b.three_ratio = 65.0;
    b.price_ratio = 100.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    b.index = 2;
    b.price_ratio = 99.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);

    fresh_v45(&st, TR_WPAIR_LONG);
    b = bar_at(1, FRI, 100000, 40, 2.0);
    b.low = 1.2;
    b.high = 3.0;
    b.d3_low = 1.2;
    b.d3_high = 3.0;
    b.three_ratio = 10.0;
    b.price_ratio = 100.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);

    fresh_v45(&st, TR_WPAIR_LONG);
    st.cfg.expiry_override = 1;
    next = open_pos(&st, 1, TUE, 10.0, 20, &o);
    b = bar_at(next, TUE, 110000, 40, 11.0);
    b.price_ratio = 50.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 9);
    TR_CHECK(strcmp(o.order_name, "만기익절1차_삼선") == 0);
}

static void test_v45_second_final_above_target_at_entry_thirty(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next = v45_first_done(&st);

    b = bar_at(next, THU, 120000, 61, 14.01);
    b.d2_time = 115959;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.trail_active == 0);
    b.d2_time = b.stime;
    o = step(&st, b);
    TR_CHECK(o.entry_elapsed == 30);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.trail_active == 1);
}

static void test_v45_trailing_exact_target_boundary(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next = v45_first_done(&st);

    st.cfg.take_pct = 12.5;
    b = bar_at(next, THU, 115000, 60, nextafter(15.0, 0.0));
    o = step(&st, b);
    TR_CHECK(o.entry_elapsed == 29);
    TR_CHECK(o.pair_pct < 50.0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.trail_active == 0);

    b.close = 15.0;
    b.d3_close = 15.0;
    o = step(&st, b);
    TR_CHECK(o.pair_pct == 50.0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.trail_active == 1);
    TR_CHECK(o.trail_target == 50.0);
    TR_CHECK(o.trail_peak == 50.0);
    TR_CHECK(o.trail_protect == 40.0);

    b.close = nextafter(15.0, INFINITY);
    b.d3_close = b.close;
    o = step(&st, b);
    TR_CHECK(o.pair_pct > 50.0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.trail_active == 1);
    TR_CHECK(o.trail_peak > 50.0);
}

static void test_v45_second_final_exact_target_boundary(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next = v45_first_done(&st);

    st.cfg.take_pct = 12.5;
    b = bar_at(next, THU, 120000, 61, nextafter(15.0, 0.0));
    o = step(&st, b);
    TR_CHECK(o.entry_elapsed == 30);
    TR_CHECK(o.pair_pct < 50.0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.trail_active == 0);

    b.close = 15.0;
    b.d3_close = 15.0;
    o = step(&st, b);
    TR_CHECK(o.pair_pct == 50.0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.trail_active == 1);

    b.close = nextafter(15.0, INFINITY);
    b.d3_close = b.close;
    o = step(&st, b);
    TR_CHECK(o.pair_pct > 50.0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.trail_active == 1);
}

static void test_v45_trailing_steps_checkpoint_and_reset(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next = v45_first_done(&st);

    b = bar_at(next, THU, 115000, 60, 14.01);
    o = step(&st, b);
    TR_CHECK(o.entry_elapsed == 29);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.trail_active == 1);
    TR_CHECK(near(o.trail_target, 40.0));
    TR_CHECK(near(o.trail_peak, 40.1));
    TR_CHECK(near(o.trail_protect, 30.0));

    b = bar_at(next + 1, THU, 115100, 61, 14.5);
    o = step(&st, b);
    TR_CHECK(o.entry_elapsed == 30);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.trail_active == 1);
    TR_CHECK(near(o.trail_peak, 45.0));
    TR_CHECK(near(o.trail_protect, 30.0));

    b = bar_at(next + 2, THU, 115200, 62, 15.0);
    o = step(&st, b);
    TR_CHECK(near(o.trail_peak, 50.0));
    TR_CHECK(near(o.trail_protect, 40.0));

    b = bar_at(next + 3, THU, 115300, 63, 16.0);
    o = step(&st, b);
    TR_CHECK(near(o.trail_peak, 60.0));
    TR_CHECK(near(o.trail_protect, 50.0));
    b.close = 14.5;
    b.d3_close = 14.5;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(near(o.trail_peak, 50.0));
    TR_CHECK(near(o.trail_protect, 40.0));

    b = bar_at(next + 4, THU, 115400, 64, 9.0);
    b.d3_close = 17.0;
    b.price_ratio = 100.0;
    b.three_ratio = 80.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_ALL);
    TR_CHECK(o.order_reason == 14);
    TR_CHECK(strcmp(o.order_name, "2차추적청산") == 0);
    TR_CHECK(o.self_resid_pct < 0.0);

    o = step(&st, bar_at(next + 5, FRI, 100000, 10, 10.0));
    TR_CHECK(o.market_position == 0);
    TR_CHECK(o.used_today == 0);
    TR_CHECK(o.trail_active == 0);
    TR_CHECK(o.trail_target == 0.0);
    TR_CHECK(o.trail_peak == 0.0);
    TR_CHECK(o.trail_protect == 0.0);
}

static void test_v45_disables_v34_residual_exits(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next = v45_first_done(&st);

    b = bar_at(next, THU, 110200, 42, 9.0);
    b.d3_close = 10.0;
    b.price_ratio = 50.0;
    b.three_ratio = 80.0;
    o = step(&st, b);
    TR_CHECK(o.self_resid_pct < 0.0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    b = bar_at(next + 1, THU, 110300, 43, 6.0);
    b.d3_close = 10.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_ALL);
    TR_CHECK(o.order_reason == 2);

    next = v45_first_done(&st);
    b = bar_at(next, THU, 110200, 42, 11.6);
    b.price_ratio = 99.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    b = bar_at(next + 1, THU, 120000, 56, 16.0);
    b.d3_close = 11.8;
    b.price_ratio = 100.0;
    b.three_ratio = 69.0;
    st.cfg.take_pct = 80.0;
    o = step(&st, b);
    TR_CHECK(o.resid_elapsed == 15);
    TR_CHECK(near(o.self_resid_pct, 60.0));
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
}

static void test_v45_low_cut_unchanged(void) {
    tr_wpair_t st;
    tr_wpair_out_t o;
    int32_t next;

    fresh_v45(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 10.0, 20, &o);
    {
        const double px[6] = {10.6, 10.5, 10.4, 10.3, 10.2, 10.1};
        int k;
        for (k = 0; k < 6; k++) {
            o = step(&st, bar_at(next + k, TUE, 120000 + k * 100, 91 + k, px[k]));
        }
    }
    TR_CHECK(o.entry_elapsed >= 60);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 13);
    TR_CHECK(o.order_qty == 1);
    TR_CHECK(o.low_done == 1);
}

static void test_v45_replay_long_entry(void) {
    enum { N = 40 };
    tr_wpair_t st;
    tr_wpair_pxbar_t self[N];
    tr_wpair_pxbar_t opp[N];
    tr_wpair_pxbar_t fut[N];
    tr_wpair_event_t ev[N];
    int n;

    fresh_v45(&st, TR_WPAIR_LONG);
    st.cfg.entry_start_bar = 0;
    for (int i = 0; i < N; i++) {
        double option_px = i + 1 == N ? 2.0 : 1.5;
        self[i] = px_at(TUE, 90000 + i * 100, 20 + i, option_px, 1000000 + i * 60000);
        opp[i] = self[i];
        {
            double c = 100.0 + (i < 15 ? i * 8.0 : 40.0);
            fut[i] = px_at(TUE, 90000 + i * 100, i == 0 ? 0 : 90 + i, c, 900000 + i * 60000);
        }
        self[i].day_index = 100 + i;
        opp[i].day_index = 100 + i;
    }
    self[N - 1].low = 1.2;
    opp[N - 1].low = 1.2;
    n = tr_wpair_replay(&st, self, N, opp, N, fut, N, 0, ev, N);
    TR_CHECK(n == 1);
    TR_CHECK(ev[0].kind == TR_WPAIR_ORDER_ENTRY);
    TR_CHECK(ev[0].pending == 1);
    TR_CHECK(ev[0].qty == 10);
    TR_CHECK(st.now.signal_bar == N);
    TR_CHECK(st.now.self_first == 2.0);
}

static void test_v12_gap_and_high_block(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int i;

    fresh_v45(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 100000, 20, 2.2);
    b.low = 1.2;
    b.high = 3.0;
    b.d3_low = 1.2;
    b.d3_high = 3.0;
    b.d2_prev_close = 100.0;
    b.d2_day_open = 102.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.used_today == 0);

    fresh_v45(&st, TR_WPAIR_LONG);
    for (i = 0; i < 3; i++) {
        b = bar_at(i + 1, TUE, 100000 + i * 100, 20 + i, 3.0);
        b.high = 21.0;
        b.three_ready = 0;
        o = step(&st, b);
        TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    }
    b = bar_at(4, TUE, 100300, 23, 3.0);
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.used_today == 0);

    fresh_v45(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 100000, 20, 3.0);
    b.high = 21.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.used_today == 0);
    b = bar_at(2, TUE, 130000, 195, 3.0);
    b.three_ratio = 60.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);

    fresh_v45(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 130000, 195, 3.0);
    b.three_ratio = 50.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);
    fresh_v45(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 130000, 195, 3.0);
    b.three_ratio = 51.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
}

static void test_v12_either_leg_take_and_weekday_trail(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;

    fresh_v45(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 100000, 40, 3.1);
    b.low = 2.6;
    b.high = 3.4;
    b.d3_close = 2.9;
    b.d3_low = 2.4;
    b.d3_high = 3.2;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);
    b = bar_at(2, TUE, 100100, 41, 3.1);
    b.d3_open = 2.9;
    b.d3_close = 2.9;
    o = step(&st, b);
    TR_CHECK(o.market_position == TR_WPAIR_LONG);
    TR_CHECK(o.model_valid == 1);

    b = bar_at(3, TUE, 100200, 42, 3.41);
    b.d3_close = 3.19;
    o = step(&st, b);
    TR_CHECK(o.pair_pct < 15.0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);

    b = bar_at(4, TUE, 100300, 43, 3.565);
    b.d3_close = 3.335;
    o = step(&st, b);
    TR_CHECK(o.pair_pct >= 15.0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 1);
    TR_CHECK(strcmp(o.order_name, "합산익절1차_삼선") == 0);

    b = bar_at(5, TUE, 100400, 44, 3.565);
    b.d3_close = 3.335;
    o = step(&st, b);
    TR_CHECK(o.first_pending == 0);
    TR_CHECK(o.trail_active == 0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);

    b = bar_at(6, TUE, 100500, 45, 4.65);
    b.d3_close = 4.35;
    o = step(&st, b);
    TR_CHECK(o.trail_active == 1);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);

    b = bar_at(7, TUE, 100600, 46, 3.72);
    b.d3_close = 3.48;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_ALL);
    TR_CHECK(o.order_reason == 14);
    TR_CHECK(strcmp(o.order_name, "2차추적청산") == 0);
}

static void test_v14_open_loss_share_and_bands(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;
    int i;

    fresh_v45(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 100000, 40, 4.0);
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);
    b = bar_at(2, TUE, 100200, 41, 6.0);
    o = step(&st, b);
    TR_CHECK(o.market_position == TR_WPAIR_LONG);
    TR_CHECK(o.model_valid == 0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);

    fresh_v45(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 100000, 40, 3.0);
    b.d3_close = 4.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);

    fresh_v45(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 100000, 40, 2.2);
    b.low = 2.0;
    b.high = 2.6;
    b.d3_close = 2.6;
    b.d3_low = 2.2;
    b.d3_high = 2.8;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);

    fresh_v45(&st, TR_WPAIR_LONG);
    st.cfg.stop_pct = 20.0;
    next = open_pos(&st, 1, TUE, 10.0, 20, &o);
    b = bar_at(next, TUE, 120000, 101, 7.9);
    o = step(&st, b);
    TR_CHECK(strcmp(o.order_name, "합산손절") == 0);

    fresh_v45(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 4.0, 100, &o);
    for (i = 0; i < 7; i++) {
        b = bar_at(next + i, TUE, 100200 + (int64_t)i * 100, 102 + i, 3.6);
        o = step(&st, b);
        TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    }
    b = bar_at(next + 7, TUE, 110000, 131, 4.24);
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(strcmp(o.order_name, "시간저수익부분청산_삼선") == 0);
}

static void test_v12_spread_and_soft_stop(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;

    fresh(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 100000, 10, 2.0);
    b.d3_close = 4.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);

    fresh(&st, TR_WPAIR_LONG);
    st.cfg.stop_pct = 20.0;
    next = open_pos(&st, 1, TUE, 10.0, 20, &o);
    b = bar_at(next, TUE, 110000, 30, 8.0);
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    b = bar_at(next + 1, TUE, 110100, 31, 7.5);
    o = step(&st, b);
    TR_CHECK(strcmp(o.order_name, "합산손절") == 0);
}

static void test_v12_width_price_and_release(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;

    fresh_v45(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 100000, 40, 2.2);
    b.sq_ratio = 49.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.used_today == 0);

    b.sq_ratio = 60.0;
    b.index = 2;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);
    TR_CHECK(strcmp(o.order_name, "삼선진입") == 0);

    fresh_v45(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 110000, 70, 2.2);
    b.three_ratio = 90.0;
    b.sq_release = 1;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_ENTRY);
    TR_CHECK(strcmp(o.order_name, "압축진입") == 0);

    fresh_v45(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 100000, 40, 2.2);
    b.low = 4.5;
    b.high = 5.5;
    b.d3_low = 4.5;
    b.d3_high = 5.5;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    TR_CHECK(o.used_today == 0);
}

static void test_v9_stop_and_idle_cut(void);
static void test_v9_book_flat_cut(void);
static void test_v11_compress_take_and_range(void);

int main(void) {
    test_defaults_and_reject();
    test_long_entry_filters();
    test_short_entry_filters();
    test_meet_mode_and_size();
    test_fill_model_and_reentry();
    test_confirm_miss_and_same_day_block();
    test_pnl_sign_stop_and_time();
    test_v12_spread_and_soft_stop();
    test_v14_open_loss_share_and_bands();
    test_v12_gap_and_high_block();
    test_v12_width_price_and_release();
    test_v12_either_leg_take_and_weekday_trail();
    test_partial_profit_and_protection();
    test_expiry_priority();
    test_residual_take_and_low_cut();
    test_date_change_and_ledger();
    test_replay_signals();
    test_trade_off_keeps_exit();
    test_v45_defaults_and_mode_scope();
    test_v45_entry_price_gate();
    test_v45_entry_budget_after_price_gate();
    test_v45_first_profit_thresholds();
    test_v45_first_low_price_exact_boundary();
    test_v45_first_high_price_exact_boundary();
    test_v45_expiry_override_keeps_friday_filter();
    test_v45_second_final_above_target_at_entry_thirty();
    test_v45_trailing_exact_target_boundary();
    test_v45_second_final_exact_target_boundary();
    test_v45_trailing_steps_checkpoint_and_reset();
    test_v45_disables_v34_residual_exits();
    test_v45_low_cut_unchanged();
    test_v45_replay_long_entry();
    test_v9_stop_and_idle_cut();
    test_v9_book_flat_cut();
    test_v11_compress_take_and_range();
    TR_TEST_SUMMARY();
}

/* V9: -22는 손절, -25 예외는 없다. 30분·압축 60 미만·최고 5 미만의 하락은 잔량 30%. */
static void test_v9_stop_and_idle_cut(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;
    int i;

    fresh_v45(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 10.0, 40, &o);
    b = bar_at(next, TUE, 100200, 42, 7.8);
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_ALL);
    TR_CHECK(o.order_reason == 2);
    TR_CHECK(strcmp(o.order_name, "합산손절") == 0);

    fresh_v45(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 10.0, 40, &o);
    for (i = 0; i < 36; i++) {
        b = bar_at(next + i, TUE, 100200 + (int64_t)i * 100, 42 + i, 8.95 - 0.01 * (i + 1));
        b.sq_ratio = 59.0;
        o = step(&st, b);
        if (o.order_kind != TR_WPAIR_ORDER_NONE) {
            break;
        }
    }
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 20);
    TR_CHECK(o.order_qty == 1);
    TR_CHECK(strcmp(o.order_name, "무수익시간부분청산_삼선") == 0);
}

/* 호가가 순방향이었다가 뒤집히고 평탄 카운트가 같이 반대로 가면 잔량 30%. */
static void test_v9_book_flat_cut(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;
    int i;

    fresh_v45(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 2.5, 40, &o);
    for (i = 0; i < 16; i++) {
        b = bar_at(next + i, TUE, 100200 + (int64_t)i * 100, 42 + i, i == 15 ? 2.7 : 3.1);
        b.d3_close = 2.5;
        b.book_valid = 1;
        b.book_score = i == 15 ? -10.0 : 10.0;
        b.flat_valid = 1;
        b.flat_pos = i == 15 ? -1 : 1;
        b.flat_slope = i == 15 ? -1 : 1;
        o = step(&st, b);
        if (o.order_kind != TR_WPAIR_ORDER_NONE) {
            break;
        }
    }
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 19);
    TR_CHECK(strcmp(o.order_name, "호가평탄반감30_삼선") == 0);
}

/* 진입 30분, 압축 50 미만, 합산 7%면 1차 익절. 갭 2%에 오늘 진폭이 더 크면 진입하지 않는다. */
static void test_v11_compress_take_and_range(void) {
    tr_wpair_t st;
    tr_wpair_bar_t b;
    tr_wpair_out_t o;
    int32_t next;
    int i;

    fresh_v45(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 2.5, 40, &o);
    for (i = 0; i < 31; i++) {
        b = bar_at(next + i, TUE, 100200 + (int64_t)i * 100, 42 + i, 2.90);
        b.d3_close = 2.5;
        b.sq_ratio = 49.0;
        o = step(&st, b);
        if (o.order_kind != TR_WPAIR_ORDER_NONE) break;
    }
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_EXIT_PART);
    TR_CHECK(o.order_reason == 1);

    fresh_v45(&st, TR_WPAIR_LONG);
    b = bar_at(1, TUE, 100000, 40, 2.5);
    b.d2_prev_close = 100.0;
    b.d2_day_open = 103.0;
    b.d2_range_valid = 1;
    b.d2_today_range = 12.0;
    b.d2_yday_range = 8.0;
    o = step(&st, b);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
}
