/* 위클리 페어 주문: 양매수 V34와 양매도 V3는 청산 사슬을 공유한다.
 * 체결은 다음 Index 봉 시가. 같은 Index는 체결 직후로 되돌려 다시 계산한다. */

#include "test_util.h"

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
    b.d3_high = px + 1.0;
    b.d3_low = px - 1.0;
    b.d3_close = px;
    b.d2_date = date;
    b.d2_time = t;
    b.d2_compress = 2;
    b.d2_interval = 1;
    b.d2_day_index = day;
    b.calc_ready = 1;
    b.three_ready = 1;
    b.three_peak = 1.0;
    b.three_ratio = 10.0;
    b.break_ready = 1;
    b.two_max = 1.0;
    b.price_ratio = 50.0;
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
    tr_wpair_out_t sig = step(st, bar_at(index, date, 100000, day, px));
    TR_CHECK(sig.order_kind == TR_WPAIR_ORDER_ENTRY);
    TR_CHECK(sig.market_position == 0);
    TR_CHECK(sig.entry_pending == 1);
    *fill = step(st, bar_at(index + 1, date, 100100, day + 1, px));
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
    b = bar_at(next, TUE, 120000, 81, 10.0);
    o = step(&st, b);
    TR_CHECK(o.entry_elapsed == 60);
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
    b = bar_at(next, TUE, 120000, 81, 2.0);
    o = step(&st, b);
    TR_CHECK(o.order_reason == 13);
    TR_CHECK(o.order_qty == 3);
    TR_CHECK(o.first_done == 0);
    b = bar_at(next + 1, TUE, 120100, 82, 2.0);
    o = step(&st, b);
    TR_CHECK(o.contracts == 7);
    TR_CHECK(o.low_pending == 0);
    TR_CHECK(o.first_done == 0);
    TR_CHECK(o.order_kind == TR_WPAIR_ORDER_NONE);
    b = bar_at(next + 2, TUE, 120200, 83, 2.2);
    o = step(&st, b);
    TR_CHECK(o.order_reason == 1);
    TR_CHECK(o.order_qty == 5);
    TR_CHECK(o.first_done == 1);
    TR_CHECK(o.low_done == 1);

    fresh(&st, TR_WPAIR_LONG);
    next = open_pos(&st, 1, TUE, 20.0, 20, &o);
    TR_CHECK(o.contracts == 1);
    b = bar_at(next, TUE, 120000, 81, 20.0);
    o = step(&st, b);
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

int main(void) {
    test_defaults_and_reject();
    test_long_entry_filters();
    test_short_entry_filters();
    test_meet_mode_and_size();
    test_fill_model_and_reentry();
    test_confirm_miss_and_same_day_block();
    test_pnl_sign_stop_and_time();
    test_partial_profit_and_protection();
    test_expiry_priority();
    test_residual_take_and_low_cut();
    test_date_change_and_ledger();
    test_replay_signals();
    test_trade_off_keeps_exit();
    TR_TEST_SUMMARY();
}
