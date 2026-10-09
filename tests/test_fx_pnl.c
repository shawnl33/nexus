#include "core/indicators/fx_pnl_2023.h"

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

static void feed(tr_fxpnl_t *s, tr_fxpnl_out_t *o, int64_t date, double high, double low, double close,
                 int market, int nb) {
    tr_fxpnl_in_t in;
    memset(&in, 0, sizeof(in));
    in.date = date;
    in.high = high;
    in.low = low;
    in.close = close;
    in.price_scale = 1;
    in.market = market;
    in.contracts = market != 0 ? 1 : 0;
    in.new_bar = nb;
    tr_fxpnl_eval(s, &in, o);
}

static void test_orange_and_cyan_and_width(void) {
    tr_fxpnl_t s;
    tr_fxpnl_out_t o;
    int i;
    tr_fxpnl_init(&s);
    feed(&s, &o, 20261008, 100, 100, 100, 1, 1);
    for (i = 0; i < 6; i++) {
        feed(&s, &o, 20261008, 99, 99, 99, 1, 1);
    }
    expect_i(o.open_rgb, 0xFF7F00, "six negatives are orange");
    expect_i(o.open_on, 1, "orange line on");

    tr_fxpnl_init(&s);
    feed(&s, &o, 20261008, 100, 100, 100, 1, 1);
    for (i = 0; i < 21; i++) {
        feed(&s, &o, 20261008, 85, 85, 85, 1, 1);
    }
    expect_i(o.open_rgb, 0x00FFFF, "21 negatives past -10 are cyan");
    expect_i(o.open_w, 2, "70 percent width");

    tr_fxpnl_init(&s);
    feed(&s, &o, 20261008, 100, 100, 100, 1, 1);
    for (i = 0; i < 32; i++) {
        feed(&s, &o, 20261008, 140, 120, 130, 1, 1);
    }
    expect_i(o.open_w, 6, "90 percent positive width");
    expect_i(o.open_rgb, 0xFF00FF, "pnl above 20 is magenta");
}

static void test_mfe_width_and_plot4(void) {
    tr_fxpnl_t s;
    tr_fxpnl_out_t o;
    int i;
    tr_fxpnl_init(&s);
    feed(&s, &o, 20261008, 110, 100, 100, 1, 1);
    expect_i(o.mfe_on, 0, "entry bar mfe hidden");
    expect_d(o.mfe_pts, 0, "entry bar mfe is 0");
    for (i = 0; i < 11; i++) {
        feed(&s, &o, 20261008, 100, 100, 100, 1, 1);
    }
    expect_i(o.mfe_w, 1, "unchanged high for 11 bars");
    expect_i(o.mfe_rgb, 0xFF0000, "mfe default red");

    tr_fxpnl_init(&s);
    feed(&s, &o, 20261008, 100, 100, 100, 1, 1);
    for (i = 0; i < 5; i++) {
        feed(&s, &o, 20261008, 100, 70, 99, 1, 1);
    }
    expect_i(o.p4_on, 1, "plot4 when mostly losing");
    expect_i(o.mae_pts < -25.0, 1, "mae under -25");
}

static void test_flip_once_and_close_uses_previous_open(void) {
    tr_fxpnl_t s;
    tr_fxpnl_out_t o;
    tr_fxpnl_init(&s);
    feed(&s, &o, 20261008, 100, 100, 100, 1, 1);
    feed(&s, &o, 20261008, 100, 90, 90, 1, 1);
    feed(&s, &o, 20261008, 110, 100, 110, 1, 1);
    expect_i(o.flips, 1, "cross back above zero");
    feed(&s, &o, 20261008, 110, 100, 110, 1, 0);
    expect_i(o.flips, 1, "replay does not count twice");
    expect_i(o.bars, 3, "replay keeps bar count");

    tr_fxpnl_init(&s);
    feed(&s, &o, 20261008, 100, 100, 100, 1, 1);
    feed(&s, &o, 20261008, 110, 110, 110, 1, 1);
    feed(&s, &o, 20261008, 80, 80, 80, 0, 1);
    expect_d(o.closed_pts, 20, "flat books previous open times 2");
    feed(&s, &o, 20261008, 80, 80, 80, 0, 0);
    expect_d(o.closed_pts, 20, "replay does not book twice");
    expect_i(o.wins, 1, "positive total is a win");

    feed(&s, &o, 20261008, 100, 100, 100, 1, 1);
    feed(&s, &o, 20261008, 50, 50, 50, 1, 1);
    feed(&s, &o, 20261008, 50, 50, 50, 0, 1);
    expect_d(o.closed_pts, -80, "loss drops the running total by 100");
    expect_i(o.p26_on, 1, "CountIF drop over 60");

    feed(&s, &o, 20261009, 50, 50, 50, 0, 1);
    expect_d(o.closed_pts, 0, "new date clears the total");
}

static void test_four_flats_and_short_keep(void) {
    tr_fxpnl_t s;
    tr_fxpnl_out_t o;
    int i;
    tr_fxpnl_init(&s);
    for (i = 0; i < 4; i++) {
        feed(&s, &o, 20261008, 100, 100, 100, 1, 1);
        feed(&s, &o, 20261008, 100, 100, 100, 0, 1);
    }
    expect_i(o.p27_on, 1, "four flats in the day");

    tr_fxpnl_init(&s);
    feed(&s, &o, 20261008, 200, 80, 200, -1, 1);
    feed(&s, &o, 20261008, 200, 80, 200, -1, 1);
    expect_d(o.mfe_pts, 120, "short mfe");
    expect_i(o.keep_on, 1, "short keep above 100");
    expect_d(o.keep_pts, 48, "short ratio 0.4");
    expect_i(o.keep2_on, 0, "0.7 line waits for 200");

    tr_fxpnl_init(&s);
    feed(&s, &o, 20261008, 200, 200, 200, -1, 1);
    feed(&s, &o, 20261008, 200, -50, 200, -1, 1);
    expect_d(o.mfe_pts, 250, "short mfe 250");
    expect_d(o.keep_pts, 137.5, "short ratio 0.55");
    expect_i(o.keep2_on, 1, "plot25");
    expect_d(o.keep2_pts, 175, "plot25 is 0.7");

    tr_fxpnl_init(&s);
    feed(&s, &o, 20261008, 180, 100, 100, 1, 1);
    feed(&s, &o, 20261008, 180, 100, 100, 1, 1);
    expect_d(o.mfe_pts, 80, "long mfe 80");
    expect_i(o.keep_on, 0, "long keep line starts above 100");

    tr_fxpnl_init(&s);
    feed(&s, &o, 20261008, 100, 100, 100, -1, 1);
    for (i = 0; i < 11; i++) {
        feed(&s, &o, 20261008, 100, 100, 100, -1, 1);
    }
    expect_i(o.mfe_w, 1, "short mfe width after 10 unchanged lows");
    expect_i(o.mfe_rgb, 0xFF0000, "short mfe default red");
}

static void test_avg_entry_and_partial_contracts(void) {
    tr_fxpnl_t s;
    tr_fxpnl_out_t o;
    tr_fxpnl_in_t in;
    tr_fxpnl_init(&s);
    memset(&in, 0, sizeof(in));
    in.date = 20261008;
    in.high = 100;
    in.low = 100;
    in.close = 100;
    in.price_scale = 1;
    in.market = 1;
    in.contracts = 2;
    in.avg_entry = 90;
    in.new_bar = 1;
    tr_fxpnl_eval(&s, &in, &o);
    expect_d(o.open_pts, 0, "entry bar pnl stays 0");
    in.high = 110;
    in.low = 110;
    in.close = 110;
    tr_fxpnl_eval(&s, &in, &o);
    expect_d(o.open_pts, 20, "open pnl uses I_AvgEntryPrice 90");
    expect_d(o.mfe_pts, 20, "mfe uses I_AvgEntryPrice");
    in.contracts = 1;
    tr_fxpnl_eval(&s, &in, &o);
    in.close = 130;
    in.high = 130;
    in.low = 130;
    tr_fxpnl_eval(&s, &in, &o);
    in.market = 0;
    in.contracts = 0;
    in.avg_entry = 0;
    in.qty_part = 1;
    tr_fxpnl_eval(&s, &in, &o);
    expect_d(o.closed_pts, 60, "partial books 20 then later open times 1");
}

static void test_shared_full_quantity(void) {
    tr_fxpnl_t s;
    tr_fxpnl_out_t o;
    tr_fxpnl_in_t in;
    tr_fxpnl_init(&s);
    feed(&s, &o, 20261008, 100, 100, 100, 1, 1);
    feed(&s, &o, 20261008, 110, 110, 110, 1, 1);
    memset(&in, 0, sizeof(in));
    in.date = 20261008;
    in.high = 80;
    in.low = 80;
    in.close = 80;
    in.price_scale = 1;
    in.market = 0;
    in.new_bar = 1;
    in.qty_full = 4;
    tr_fxpnl_eval(&s, &in, &o);
    expect_d(o.closed_pts, 40, "shared full quantity replaces 2");
}

int main(void) {
    test_orange_and_cyan_and_width();
    test_avg_entry_and_partial_contracts();
    test_shared_full_quantity();
    test_mfe_width_and_plot4();
    test_flip_once_and_close_uses_previous_open();
    test_four_flats_and_short_keep();
    if (fails) {
        return 1;
    }
    printf("ok\n");
    return 0;
}
