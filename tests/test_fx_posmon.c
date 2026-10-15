#include "test_util.h"

#include <string.h>

#include "core/indicators/fx_posmon.h"

static void feed(tr_fxpos_t *s, tr_fxpos_out_t *o, int v2, int64_t date, int64_t time, int mkt, int qty,
                 double avg, double close) {
    tr_fxpos_in_t in;
    memset(&in, 0, sizeof(in));
    in.v2 = v2;
    in.date = date;
    in.time = time;
    in.market = mkt;
    in.contracts = qty;
    in.avg = avg;
    in.high = close;
    in.low = close;
    in.close = close;
    in.price_scale = 1;
    in.tick_mult = 1;
    in.partial_ticks = 10;
    tr_fxpos_eval(s, &in, o);
}

static void test_flat_close_books_previous_qty(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    tr_fxpos_init(&s);
    feed(&s, &o, 0, 20260715, 100000, 1, 2, 100, 100);
    feed(&s, &o, 0, 20260715, 100100, 0, 0, 0, 110);
    TR_CHECK(o.closed_long == 20);
    TR_CHECK(o.wins == 1);
    TR_CHECK(o.losses == 0);
}

static void test_reverse_books_before_new_side(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    tr_fxpos_init(&s);
    feed(&s, &o, 1, 20260715, 100000, 1, 2, 100, 100);
    feed(&s, &o, 1, 20260715, 100100, -1, 2, 110, 110);
    TR_CHECK(o.closed_long == 20);
    TR_CHECK(o.side == -1);
    TR_CHECK(o.wins == 1);
}

static void test_partial_multiplies_qty_and_ignores_add(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    tr_fxpos_in_t in;
    tr_fxpos_init(&s);
    feed(&s, &o, 0, 20260715, 100000, 1, 2, 100, 100);
    memset(&in, 0, sizeof(in));
    in.date = 20260715;
    in.time = 100100;
    in.market = 1;
    in.contracts = 1;
    in.avg = 100;
    in.high = 112;
    in.low = 100;
    in.close = 105;
    in.price_scale = 1;
    in.tick_mult = 1;
    in.partial_ticks = 10;
    tr_fxpos_eval(&s, &in, &o);
    TR_CHECK(o.exit_ticks == 10);
    TR_CHECK(o.closed_long == 0);
    in.time = 100200;
    in.contracts = 3;
    in.close = 106;
    tr_fxpos_eval(&s, &in, &o);
    TR_CHECK(o.exit_ticks == 10);
}

static void test_v1_date_resets_and_v2_uses_session(void) {
    tr_fxpos_t a, b;
    tr_fxpos_out_t o;
    tr_fxpos_init(&a);
    tr_fxpos_init(&b);
    feed(&a, &o, 0, 20260715, 100000, 1, 1, 100, 100);
    feed(&a, &o, 0, 20260715, 100100, 0, 0, 0, 110);
    feed(&a, &o, 0, 20260716, 100000, 0, 0, 0, 110);
    TR_CHECK(o.closed_long == 0);
    feed(&b, &o, 1, 20260715, 65900, 1, 1, 100, 100);
    feed(&b, &o, 1, 20260715, 65900, 0, 0, 0, 110);
    TR_CHECK(o.closed_long == 10);
    feed(&b, &o, 1, 20260715, 70000, 0, 0, 0, 110);
    TR_CHECK(o.closed_long == 0);
}

static void test_v2_ignores_zero_stored_avg(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    tr_fxpos_init(&s);
    feed(&s, &o, 1, 20260715, 100000, 1, 2, 0, 100);
    feed(&s, &o, 1, 20260715, 100100, 0, 0, 0, 110);
    TR_CHECK(o.closed_long == 0);
    TR_CHECK(o.losses == 1);
}

static void bar(tr_fxpos_t *s, tr_fxpos_out_t *o, int mkt, int qty, double avg, double hi, double lo,
                double close) {
    tr_fxpos_in_t in;
    memset(&in, 0, sizeof(in));
    in.date = 20260715;
    in.time = 100000 + s->bars * 100;
    in.market = mkt;
    in.contracts = qty;
    in.avg = avg;
    in.high = hi;
    in.low = lo;
    in.close = close;
    in.price_scale = 1;
    in.tick_mult = 1;
    in.partial_ticks = 10;
    tr_fxpos_eval(s, &in, o);
}

/* 21봉 연속 손실이고 한 번도 플러스가 없으면 진입손익은 청록이다. */
static void test_long_open_21_negative_is_cyan(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    int i;
    tr_fxpos_init(&s);
    bar(&s, &o, 1, 1, 100, 100, 100, 100);
    for (i = 0; i < 21; i++) {
        bar(&s, &o, 1, 1, 100, 100, 85, 85);
    }
    TR_CHECK(o.open_ticks == -15);
    TR_CHECK(o.open_rgb == 0x00ffffu);
}

/* 최근 10봉이 손실이고 이번 봉이 저점을 깨면, -10틱보다 깊어도 청록이다. */
static void test_long_open_10_negative_new_low_is_cyan(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    int i;
    tr_fxpos_init(&s);
    bar(&s, &o, 1, 1, 100, 100, 100, 100);
    for (i = 0; i < 9; i++) {
        bar(&s, &o, 1, 1, 100, 100, 85, 85);
    }
    bar(&s, &o, 1, 1, 100, 100, 80, 85);
    TR_CHECK(o.open_rgb == 0x00ffffu);
}

/* 플러스가 6봉을 넘긴 뒤 최근 3봉이 -30~-50틱이고 저점이 깨지면 청록이다. */
static void test_long_open_3_negative_after_profit_is_cyan(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    int i;
    tr_fxpos_init(&s);
    bar(&s, &o, 1, 1, 100, 100, 100, 100);
    for (i = 0; i < 7; i++) {
        bar(&s, &o, 1, 1, 100, 110, 100, 110);
    }
    bar(&s, &o, 1, 1, 100, 100, 95, 60);
    bar(&s, &o, 1, 1, 100, 100, 95, 60);
    bar(&s, &o, 1, 1, 100, 100, 90, 60);
    TR_CHECK(o.open_ticks == -40);
    TR_CHECK(o.open_rgb == 0x00ffffu);
}

/* 거래의 90%가 손실이고 30봉을 넘으면 진입손익 굵기는 5다. */
static void test_long_open_width_five_when_mostly_negative(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    int i;
    tr_fxpos_init(&s);
    bar(&s, &o, 1, 1, 100, 100, 100, 100);
    for (i = 0; i < 31; i++) {
        bar(&s, &o, 1, 1, 100, 100, 90, 90);
    }
    TR_CHECK(o.open_w == 5);
}

/* 매도 손실이 -8틱보다 깊으면 청록이고, 최고가 갱신 횟수가 적으면 굵기 0이다. */
static void test_short_mae_under_eight_is_cyan(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    tr_fxpos_init(&s);
    bar(&s, &o, -1, 1, 100, 100, 100, 100);
    bar(&s, &o, -1, 1, 100, 110, 90, 100);
    TR_CHECK(o.mae_ticks == -10);
    TR_CHECK(o.mae_rgb == 0x00ffffu);
    TR_CHECK(o.mae_w == 0);
}

/* 매도 보유가 180봉 미만이고 직전 최고가 정체가 120봉을 넘은 뒤 고가가 갱신되면 굵기 5다. */
static void test_short_mfe_width_five_after_stale_high(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    int i;
    tr_fxpos_init(&s);
    bar(&s, &o, -1, 1, 1000, 1000, 1000, 1010);
    for (i = 0; i < 121; i++) {
        bar(&s, &o, -1, 1, 1000, 1000, 990 - i, 1010);
    }
    bar(&s, &o, -1, 1, 1000, 1020, 800, 1010);
    TR_CHECK(o.open_ticks < 0);
    TR_CHECK(o.mfe_w == 5);
}

/* 청산 뒤 최근 봉이 -10틱을 한 번 깨고, 지금은 -3~0틱이면 매도 손익은 녹색이다. */
static void test_short_open_green_after_deep_dip(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    int i;
    tr_fxpos_init(&s);
    bar(&s, &o, -1, 2, 100, 100, 100, 100);
    bar(&s, &o, -1, 1, 100, 100, 90, 111);
    for (i = 0; i < 5; i++) {
        bar(&s, &o, -1, 1, 100, 100, 90, 101);
    }
    TR_CHECK(o.open_ticks > -3);
    TR_CHECK(o.open_ticks < 0);
    TR_CHECK(o.open_rgb == 0x008000u);
}

/* 청산 경과가 15봉을 넘고 그 창의 대부분이 손실이면 매도 손익 굵기는 2다. */
static void test_short_open_width_two_after_exit(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    int i;
    tr_fxpos_init(&s);
    bar(&s, &o, -1, 2, 100, 100, 100, 90);
    for (i = 0; i < 10; i++) {
        bar(&s, &o, -1, 2, 100, 100, 90, 90);
    }
    bar(&s, &o, -1, 1, 100, 100, 90, 101);
    for (i = 0; i < 14; i++) {
        bar(&s, &o, -1, 1, 100, 100, 90, 101);
    }
    TR_CHECK(o.open_w == 2);
}

/* 직전 손익이 0 이하이고 이번 봉이 플러스면 수익전환을 한 번 센다. */
static void test_profit_flip_counts_cross_above_zero(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    tr_fxpos_init(&s);
    bar(&s, &o, 1, 1, 100, 100, 100, 100);
    bar(&s, &o, 1, 1, 100, 110, 100, 110);
    TR_CHECK(o.flips == 1);
    TR_CHECK(o.danger == 0);
    bar(&s, &o, 0, 0, 0, 110, 110, 110);
    TR_CHECK(o.flips == 0);
}

/* 손실 구간이 70%를 넘고 20봉을 넘긴 뒤의 첫 플러스는 위험수익전환이다. */
static void test_danger_flip_after_long_loss(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    int i;
    tr_fxpos_init(&s);
    bar(&s, &o, 1, 1, 100, 100, 100, 100);
    for (i = 0; i < 21; i++) {
        bar(&s, &o, 1, 1, 100, 100, 90, 90);
    }
    bar(&s, &o, 1, 1, 100, 110, 100, 105);
    TR_CHECK(o.flips == 1);
    TR_CHECK(o.danger == 1);
}

/* 청산 누적이 있으면 총손익 선을 틱으로 낸다. */
static void test_total_line_sums_closed_sides(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    tr_fxpos_init(&s);
    feed(&s, &o, 0, 20260715, 100000, 1, 2, 100, 100);
    feed(&s, &o, 0, 20260715, 100100, 0, 0, 0, 110);
    TR_CHECK(o.total_on == 1);
    TR_CHECK(o.total_ticks == 20);
}

/* 보유 봉의 80%가 손실이고 진입손실이 -25틱보다 깊으면 진입수익의 0.7을 그린다. */
static void test_keep_line_when_mostly_negative(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    int i;
    tr_fxpos_init(&s);
    bar(&s, &o, 1, 1, 100, 100, 100, 100);
    for (i = 0; i < 5; i++) {
        bar(&s, &o, 1, 1, 100, 110, 70, 90);
    }
    TR_CHECK(o.keep_mfe_on == 1);
    TR_CHECK(o.keep_mfe == 7);
    TR_CHECK(o.keep_mae_on == 1);
    TR_CHECK(o.keep_mae == -21);
}

static void test_plot_colors_follow_tick_rules(void) {
    tr_fxpos_t s;
    tr_fxpos_out_t o;
    tr_fxpos_in_t in;
    int i;
    tr_fxpos_init(&s);
    memset(&in, 0, sizeof(in));
    in.date = 20260715;
    in.time = 100000;
    in.market = 1;
    in.contracts = 1;
    in.avg = 100;
    in.high = 100;
    in.low = 100;
    in.close = 100;
    in.price_scale = 1;
    in.tick_mult = 1;
    in.partial_ticks = 10;
    tr_fxpos_eval(&s, &in, &o);
    in.time = 100100;
    in.low = 70;
    in.close = 125;
    in.high = 125;
    tr_fxpos_eval(&s, &in, &o);
    TR_CHECK(o.mae_rgb == 0x00ffffu);
    TR_CHECK(o.open_rgb == 0xff00ffu);
    for (i = 0; i < 61; i++) {
        in.time = 100200 + i * 100;
        in.high = 230;
        in.low = 100;
        in.close = 120;
        tr_fxpos_eval(&s, &in, &o);
    }
    TR_CHECK(o.mfe_rgb == 0x0000ffu);
    feed(&s, &o, 0, 20260715, 200000, 0, 0, 0, 401);
    TR_CHECK(o.long_rgb == 0xff0000u);
}

int main(void) {
    test_flat_close_books_previous_qty();
    test_reverse_books_before_new_side();
    test_partial_multiplies_qty_and_ignores_add();
    test_v1_date_resets_and_v2_uses_session();
    test_v2_ignores_zero_stored_avg();
    test_long_open_21_negative_is_cyan();
    test_long_open_10_negative_new_low_is_cyan();
    test_long_open_3_negative_after_profit_is_cyan();
    test_long_open_width_five_when_mostly_negative();
    test_short_mae_under_eight_is_cyan();
    test_short_mfe_width_five_after_stale_high();
    test_short_open_green_after_deep_dip();
    test_short_open_width_two_after_exit();
    test_profit_flip_counts_cross_above_zero();
    test_danger_flip_after_long_loss();
    test_total_line_sums_closed_sides();
    test_keep_line_when_mostly_negative();
    test_plot_colors_follow_tick_rules();
    TR_TEST_SUMMARY();
}
