#include "core/backtest/yl_fill.h"
#include "core/backtest/yl_link.h"
#include "core/signals/weekly_pair_v1.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect_i(int got, int want, const char *msg) {
    if (got != want) {
        fprintf(stderr, "fail %s got %d want %d\n", msg, got, want);
        fails++;
    }
}

static void expect_d(double got, double want, const char *msg) {
    double d = got - want;
    if (d < 0) {
        d = -d;
    }
    if (d > 1e-9) {
        fprintf(stderr, "fail %s got %.10g want %.10g\n", msg, got, want);
        fails++;
    }
}

static tr_yl_bar_t bar(int64_t time, double open, double high, double low, double close) {
    tr_yl_bar_t b;
    b.time = time;
    b.open = open;
    b.high = high;
    b.low = low;
    b.close = close;
    return b;
}

static tr_yl_order_t order(const char *name, int action, int price_type, double price, int qty) {
    tr_yl_order_t o;
    o.name = name;
    o.action = action;
    o.price_type = price_type;
    o.price = price;
    o.qty = qty;
    return o;
}

static void test_onclose_fills_this_bar(void) {
    tr_yl_book_t book;
    tr_yl_bar_t b = bar(10, 9.0, 11.0, 8.0, 10.5);
    tr_yl_order_t o = order("후보매수", TR_YL_BUY, TR_YL_ONCLOSE, 0, 0);
    tr_yl_pos_t pos;
    const tr_yl_trade_t *t;
    tr_yl_book_init(&book, 1, 0.25);
    tr_yl_book_open(&book, &b);
    pos = tr_yl_book_pos(&book);
    expect_i(pos.side, 0, "onclose before");
    expect_i(tr_yl_book_submit(&book, &o), 1, "onclose submit");
    tr_yl_book_close(&book);
    pos = tr_yl_book_pos(&book);
    expect_i(pos.side, 1, "onclose side");
    expect_i(pos.contracts, 1, "onclose qty");
    expect_d(pos.avg, 10.5, "onclose avg");
    expect_i(tr_yl_book_trade_count(&book), 1, "onclose trades");
    t = tr_yl_book_trade(&book, 0);
    expect_i(t->filled, 1, "onclose filled");
    expect_d(t->price, 10.5, "onclose price");
    expect_i((int)t->time, 10, "onclose time");
    expect_i(t->has_pnl, 0, "onclose no pnl");
    expect_i(strcmp(t->name, "후보매수"), 0, "onclose name");
}

static void test_market_fills_next_open(void) {
    tr_yl_book_t book;
    tr_yl_bar_t b1 = bar(1, 10, 10, 10, 10);
    tr_yl_bar_t b2 = bar(2, 11, 12, 10.5, 11.5);
    tr_yl_order_t o = order("페어진입", TR_YL_BUY, TR_YL_MARKET, 0, 2);
    tr_yl_pos_t pos;
    const tr_yl_trade_t *t;
    tr_yl_book_init(&book, 1, 0.25);
    tr_yl_book_open(&book, &b1);
    tr_yl_book_submit(&book, &o);
    tr_yl_book_close(&book);
    pos = tr_yl_book_pos(&book);
    expect_i(pos.side, 0, "market same bar");
    expect_i(tr_yl_book_trade_count(&book), 0, "market not yet");
    tr_yl_book_open(&book, &b2);
    pos = tr_yl_book_pos(&book);
    expect_i(pos.side, 1, "market side");
    expect_i(pos.contracts, 2, "market qty");
    expect_d(pos.avg, 11, "market avg");
    t = tr_yl_book_trade(&book, 0);
    expect_i(t->filled, 1, "market filled");
    expect_d(t->price, 11, "market price");
    expect_i((int)t->time, 2, "market time");
}

static void test_market_without_next_bar_is_pending(void) {
    tr_yl_book_t book;
    tr_yl_bar_t b = bar(5, 10, 10, 10, 10);
    tr_yl_order_t o = order("페어진입", TR_YL_BUY, TR_YL_MARKET, 0, 1);
    const tr_yl_trade_t *t;
    tr_yl_book_init(&book, 1, 0.25);
    tr_yl_book_open(&book, &b);
    tr_yl_book_submit(&book, &o);
    tr_yl_book_close(&book);
    tr_yl_book_finish(&book);
    expect_i(tr_yl_book_pos(&book).side, 0, "pending flat");
    expect_i(tr_yl_book_trade_count(&book), 1, "pending row");
    t = tr_yl_book_trade(&book, 0);
    expect_i(t->filled, 0, "pending flag");
    expect_i((int)t->time, 5, "pending time");
    expect_i(t->has_pnl, 0, "pending pnl");
}

static void test_stop_and_limit_touch_and_gap(void) {
    tr_yl_book_t book;
    tr_yl_bar_t signal = bar(1, 9, 9, 9, 9);
    tr_yl_bar_t touch = bar(2, 9, 12, 8, 11);
    tr_yl_order_t stop = order("스톱매수", TR_YL_BUY, TR_YL_STOP, 10, 1);
    tr_yl_order_t limit = order("지정매도", TR_YL_SELL, TR_YL_LIMIT, 10, 1);
    const tr_yl_trade_t *t;
    tr_yl_book_init(&book, 1, 1);
    tr_yl_book_open(&book, &signal);
    tr_yl_book_submit(&book, &stop);
    tr_yl_book_close(&book);
    tr_yl_book_open(&book, &touch);
    t = tr_yl_book_trade(&book, 0);
    expect_i(t->filled, 1, "stop filled");
    expect_d(t->price, 10, "stop price");

    tr_yl_book_init(&book, 1, 1);
    tr_yl_book_open(&book, &signal);
    tr_yl_book_submit(&book, &limit);
    tr_yl_book_close(&book);
    tr_yl_book_open(&book, &touch);
    t = tr_yl_book_trade(&book, 0);
    expect_i(t->filled, 1, "limit filled");
    expect_d(t->price, 10, "limit price");

    {
        tr_yl_bar_t gap = bar(3, 12, 13, 11, 12.5);
        tr_yl_book_init(&book, 1, 1);
        tr_yl_book_open(&book, &signal);
        tr_yl_book_submit(&book, &stop);
        tr_yl_book_close(&book);
        tr_yl_book_open(&book, &gap);
        t = tr_yl_book_trade(&book, 0);
        expect_d(t->price, 12, "stop gap uses open");
    }
}

static void test_last_bar_onclose_fills_rest_pending(void) {
    tr_yl_book_t book;
    tr_yl_bar_t b = bar(8, 10, 11, 9, 10.5);
    tr_yl_order_t close_o = order("후보매수", TR_YL_BUY, TR_YL_ONCLOSE, 0, 1);
    tr_yl_order_t market = order("시장", TR_YL_BUY, TR_YL_MARKET, 0, 1);
    tr_yl_order_t stop = order("스톱", TR_YL_BUY, TR_YL_STOP, 12, 1);
    tr_yl_book_init(&book, 1, 0.25);
    tr_yl_book_open(&book, &b);
    tr_yl_book_submit(&book, &close_o);
    tr_yl_book_submit(&book, &market);
    tr_yl_book_submit(&book, &stop);
    tr_yl_book_close(&book);
    tr_yl_book_finish(&book);
    expect_i(tr_yl_book_trade_count(&book), 3, "last rows");
    expect_i(tr_yl_book_trade(&book, 0)->filled, 1, "last onclose");
    expect_d(tr_yl_book_trade(&book, 0)->price, 10.5, "last onclose price");
    expect_i(tr_yl_book_trade(&book, 1)->filled, 0, "last market");
    expect_i(tr_yl_book_trade(&book, 2)->filled, 0, "last stop");
    expect_i(tr_yl_book_pos(&book).contracts, 1, "last position from onclose only");
}

static void test_exit_without_qty_flattens(void) {
    tr_yl_book_t book;
    tr_yl_bar_t b = bar(1, 10, 10, 10, 10);
    tr_yl_order_t buy = order("진입", TR_YL_BUY, TR_YL_ONCLOSE, 0, 3);
    tr_yl_order_t exit_o = order("청산", TR_YL_EXIT_LONG, TR_YL_ONCLOSE, 0, 0);
    tr_yl_pos_t pos;
    tr_yl_book_init(&book, 1, 1);
    tr_yl_book_open(&book, &b);
    tr_yl_book_submit(&book, &buy);
    tr_yl_book_close(&book);
    tr_yl_book_submit(&book, &exit_o);
    tr_yl_book_close(&book);
    pos = tr_yl_book_pos(&book);
    expect_i(pos.side, 0, "flat side");
    expect_i(pos.contracts, 0, "flat qty");
    expect_d(pos.avg, 0, "flat avg");
    expect_i(tr_yl_book_trade(&book, 1)->qty, 3, "exit qty");
    expect_i(tr_yl_book_trade(&book, 1)->has_pnl, 1, "exit pnl");
    expect_d(tr_yl_book_trade(&book, 1)->pnl, 0, "exit same price");
}

static void test_reverse_entry_pnl_is_closed_qty(void) {
    tr_yl_book_t book;
    tr_yl_bar_t b1 = bar(1, 10, 10, 10, 10);
    tr_yl_bar_t b2 = bar(2, 12, 12, 12, 12);
    tr_yl_order_t buy = order("매수", TR_YL_BUY, TR_YL_ONCLOSE, 0, 2);
    tr_yl_order_t sell = order("매도", TR_YL_SELL, TR_YL_ONCLOSE, 0, 3);
    tr_yl_pos_t pos;
    const tr_yl_trade_t *t;
    tr_yl_book_init(&book, 1, 1);
    tr_yl_book_open(&book, &b1);
    tr_yl_book_submit(&book, &buy);
    tr_yl_book_close(&book);
    tr_yl_book_open(&book, &b2);
    tr_yl_book_submit(&book, &sell);
    tr_yl_book_close(&book);
    expect_i(tr_yl_book_trade_count(&book), 2, "reverse rows");
    t = tr_yl_book_trade(&book, 1);
    expect_i(t->qty, 3, "reverse order qty");
    expect_i(t->has_pnl, 1, "reverse has pnl");
    expect_d(t->pnl, 4, "reverse pnl");
    pos = tr_yl_book_pos(&book);
    expect_i(pos.side, -1, "reverse side");
    expect_i(pos.contracts, 1, "reverse remain");
    expect_d(pos.avg, 12, "reverse avg");
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

static void test_pair_book_matches_replay(void) {
    tr_wpair_t replay_st;
    tr_wpair_t fill_st;
    tr_wpair_config_t cfg;
    tr_wpair_pxbar_t self[3];
    tr_wpair_pxbar_t opp[3];
    tr_wpair_pxbar_t fut[3];
    tr_wpair_event_t ev[8];
    tr_yl_book_t book;
    int n;
    int i;
    tr_wpair_default_config(&cfg);
    expect_i(tr_wpair_init(&replay_st, TR_WPAIR_SHORT, &cfg), 0, "replay init");
    expect_i(tr_wpair_init(&fill_st, TR_WPAIR_SHORT, &cfg), 0, "fill init");
    self[0] = px_at(20241001, 90000, 0, 10.0, 1000000);
    self[1] = px_at(20241001, 90100, 1, 10.0, 1060000);
    self[2] = px_at(20241001, 90200, 2, 8.0, 1120000);
    for (i = 0; i < 3; i++) {
        opp[i] = self[i];
        fut[i] = self[i];
    }
    n = tr_wpair_replay(&replay_st, self, 3, opp, 3, fut, 3, 0, ev, 8);
    tr_yl_book_init(&book, 1, 0.05);
    expect_i(tr_wpair_fill(&fill_st, self, 3, opp, 3, fut, 3, 0, &book), n, "fill count");
    expect_i(tr_yl_book_trade_count(&book), n, "book rows");
    for (i = 0; i < n && i < tr_yl_book_trade_count(&book); i++) {
        const tr_yl_trade_t *t = tr_yl_book_trade(&book, i);
        expect_i(strcmp(t->name, ev[i].name), 0, "pair name");
        expect_i(t->qty, ev[i].qty, "pair qty");
        expect_i(t->filled, ev[i].pending ? 0 : 1, "pair filled");
        expect_i((int)t->time, (int)ev[i].time_sec, "pair time");
    }
    expect_d(tr_yl_book_trade(&book, 0)->price, 10.0, "pair fill is next open");
    expect_i(tr_yl_book_pos(&book).side, -1, "pair side");
    expect_i(tr_yl_book_pos(&book).contracts, 2, "pair contracts");
}

static void test_fxsig_onclose_name_and_position(void) {
    tr_yl_book_t book;
    tr_fxos_t scope;
    tr_fxos_input_t in;
    tr_yl_bar_t b = bar(20, 10.0, 11.0, 9.0, 10.5);
    tr_yl_book_init(&book, 1, 0.25);
    tr_yl_book_open(&book, &b);
    expect_i(tr_yl_fxsig_emit(&book, 1, 0, 1, 1), 1, "emit buy");
    tr_yl_book_close(&book);
    expect_i(strcmp(tr_yl_book_trade(&book, 0)->name, "후보매수"), 0, "fx name");
    expect_i(tr_yl_book_trade(&book, 0)->filled, 1, "fx filled");
    expect_d(tr_yl_book_trade(&book, 0)->price, 10.5, "fx close");
    expect_i(tr_yl_book_pos(&book).side, 1, "fx side");
    expect_i(tr_yl_book_pos(&book).contracts, 1, "fx qty");

    expect_i(tr_fxos_init(&scope, 100.0), 1, "fx init");
    memset(&in, 0, sizeof(in));
    in.date = 20261013;
    in.time = 220000;
    in.bar_open = 1789489260000000LL;
    in.high = 11;
    in.low = 9;
    in.close = 10.5;
    in.is_new_bar = true;
    expect_i(tr_yl_fxsig_bar(&book, &scope, &in), 0, "quiet bar adds no order");
    expect_i((int)book.bar.time, 1789489260, "book time is unix seconds");
    expect_i(scope.mkt, 1, "formula sees book position");
    expect_d(scope.avg_entry, 10.5, "formula avg");
}

static void test_negative_qty_rejected(void) {
    tr_yl_book_t book;
    tr_yl_bar_t b = bar(1, 10, 10, 10, 10);
    tr_yl_order_t o = order("거절", TR_YL_BUY, TR_YL_ONCLOSE, 0, -1);
    tr_yl_book_init(&book, 1, 0.25);
    tr_yl_book_open(&book, &b);
    expect_i(tr_yl_book_submit(&book, &o), 0, "reject");
    tr_yl_book_close(&book);
    expect_i(tr_yl_book_trade_count(&book), 0, "no trade");
    expect_i(tr_yl_book_pos(&book).side, 0, "still flat");
}

int main(void) {
    test_onclose_fills_this_bar();
    test_market_fills_next_open();
    test_market_without_next_bar_is_pending();
    test_stop_and_limit_touch_and_gap();
    test_last_bar_onclose_fills_rest_pending();
    test_exit_without_qty_flattens();
    test_reverse_entry_pnl_is_closed_qty();
    test_pair_book_matches_replay();
    test_fxsig_onclose_name_and_position();
    test_negative_qty_rejected();
    if (fails != 0) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    return 0;
}
