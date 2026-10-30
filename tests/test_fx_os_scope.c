#include "test_util.h"

#include <string.h>

#include "core/indicators/fx_os_scope.h"

static void test_reset_is_flat_then_watch(void) {
    tr_fxos_t s;
    TR_CHECK(tr_fxos_init(&s, 1.0));
    tr_fxos_input_t b1;
    memset(&b1, 0, sizeof(b1));
    b1.date = 20241001;
    b1.time = 90000;
    b1.bar_open = 60000000;
    b1.high = 101;
    b1.low = 100;
    b1.close = 100.5;
    b1.volume = 10;
    b1.is_new_bar = true;
    tr_fxos_eval(&s, &b1);
    TR_CHECK(s.out.judge == 0);
    TR_CHECK(s.out.fut == 0);

    tr_fxos_input_t b2 = b1;
    b2.time = 90100;
    b2.bar_open = 120000000;
    b2.high = 110;
    b2.low = 108;
    b2.close = 109;
    tr_fxos_eval(&s, &b2);
    TR_CHECK(s.out.fut == 1);
    TR_CHECK(s.out.judge == 50);
}

/* 7월 본장 시작은 223000, 1월은 233000. 전환이 없으면 turn_on 은 0. */
static void test_rth_follows_date_and_turn_stays_off(void) {
    tr_fxos_t s;
    tr_fxos_input_t bar;
    TR_CHECK(tr_fxos_init(&s, 1.0));
    memset(&bar, 0, sizeof(bar));
    bar.date = 20260715;
    bar.time = 100000;
    bar.bar_open = 60000000;
    bar.high = 101;
    bar.low = 100;
    bar.close = 100.5;
    bar.volume = 10;
    bar.is_new_bar = true;
    tr_fxos_eval(&s, &bar);
    TR_CHECK(s.out.rth_open == 223000);
    TR_CHECK(s.out.turn_on == 0);

    bar.date = 20260115;
    bar.time = 100100;
    bar.bar_open = 120000000;
    tr_fxos_eval(&s, &bar);
    TR_CHECK(s.out.rth_open == 233000);
    TR_CHECK(s.out.turn_on == 0);
}

/* 같은 봉을 다시 평가해도 봉 수와 짝 진행은 한 번이다. 다음 봉에서만 하나 늘어난다. */
static void test_replay_same_bar_does_not_advance(void) {
    tr_fxos_t s;
    tr_fxos_input_t bar;
    int bars;
    int steps;
    TR_CHECK(tr_fxos_init(&s, 1.0));
    memset(&bar, 0, sizeof(bar));
    bar.date = 20260715;
    bar.time = 220000;
    bar.bar_open = 60000000;
    bar.high = 101;
    bar.low = 100;
    bar.close = 100.5;
    bar.volume = 10;
    bar.is_new_bar = true;
    tr_fxos_eval(&s, &bar);
    bars = s.hist.bars;
    steps = s.zd_st.bar_n;
    bar.is_new_bar = false;
    bar.high = 103;
    bar.close = 102;
    tr_fxos_eval(&s, &bar);
    TR_CHECK(s.hist.bars == bars);
    TR_CHECK(s.zd_st.bar_n == steps);
    TR_CHECK(s.pair_st.bar_n == steps);
    bar.is_new_bar = true;
    bar.time = 220100;
    bar.bar_open = 120000000;
    tr_fxos_eval(&s, &bar);
    TR_CHECK(s.hist.bars == bars + 1);
    TR_CHECK(s.zd_st.bar_n == steps + 1);
}

static void test_format_appends_turn(void) {
    tr_fxos_out_t out;
    char buf[8192];
    const char *mark = ",223000,1,-190,25600,7,8,1,2";
    const char *hit;
    int idx = 0;
    const char *p;
    memset(&out, 0, sizeof(out));
    out.rth_open = 223000;
    out.turn_on = 1;
    out.turn_y = -190;
    out.turn_rgb = 0x006400;
    out.turn_w = 7;
    out.sig_grade = 8;
    out.sig_adj_kind = 1;
    out.sig_adj_grade = 2;
    TR_CHECK(tr_fxos_format(buf, sizeof(buf), &out) > 0);
    hit = strstr(buf, mark);
    TR_CHECK(hit != 0);
    if (hit != 0) {
        for (p = buf; p < hit; p++) {
            if (*p == ',') {
                idx++;
            }
        }
        /* 쉼표 개수는 값 직전이라 본장 시각 칸은 148, 전환은 149부터다. */
        TR_CHECK(idx == 147);
    }
}

/* 가격은 평탄선 아래, 현재 상승 기울기는 30봉 경계. 직전 상승 길이는 12봉이다. */
static void test_pair_uses_current_slope(void) {
    const struct { int previous_slope, want; } cases[] = {{29, 1}, {35, 0}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        tr_fxos_t s;
        tr_fxos_input_t bar;
        tr_fxfv_output_t fv;
        TR_CHECK(tr_fxos_init(&s, 0.25));
        memset(&bar, 0, sizeof(bar));
        memset(&fv, 0, sizeof(fv));
        bar.date = 20261014;
        bar.time = 234400;
        bar.bar_open = 60000000;
        bar.open = bar.close = 98;
        bar.high = 99;
        bar.low = 97;
        bar.volume = 100;
        bar.is_new_bar = true;
        tr_fxos_eval(&s, &bar);
        /* 다음 봉은 확정된 스윙 고점 99를 돌파한다. */
        s.entry.evals = 2;
        s.entry.cfg.brk_pos_on = 0;
        s.entry.cfg.brk_heat = 0;
        s.entry.st.up_hi = 99;
        s.entry.st.up_lo = 95;
        s.entry.st.up_382 = 98;
        s.entry.st.dn_hi = 97;
        s.entry.st.dn_lo = 90;
        s.entry.st.close1 = 98;
        s.sig_fv.cfg.price_scale = 0;
        fv.reg_flat = 110;
        bar.fv = &fv;
        s.prev_flat = 109;
        s.prev_close = 98;
        s.flat_prev.pos = -2;
        s.flat_prev.slope_dir = 1;
        s.flat_prev.slope = cases[i].previous_slope;
        s.flat_prev.prev_up_slope = 12;
        bar.time = 234500;
        bar.bar_open += 60000000;
        bar.open = bar.close = 100;
        bar.high = 101;
        bar.low = 99;
        tr_fxos_eval(&s, &bar);
        TR_CHECK(s.out.raw_dir == 1);
        TR_CHECK(s.out.flat_pos < 0);
        TR_CHECK(s.out.flat_slope == cases[i].previous_slope + 1);
        TR_CHECK(s.out.pair_dir == cases[i].want);
        TR_CHECK(s.out.zd_dir == cases[i].want);
    }
}

static void test_exit_quantities_reach_chart_rows(void) {
    tr_fxos_t s;
    tr_fxos_input_t bar = {0};
    tr_fxpair_in_t pin = {0};
    tr_fxpair_out_t pout;
    TR_CHECK(tr_fxos_init(&s, 1.0));
    bar.date = 20261014;
    bar.time = 220000;
    bar.bar_open = 60000000;
    bar.open = bar.high = bar.low = bar.close = 100;
    bar.volume = 10;
    bar.is_new_bar = true;
    tr_fxos_eval(&s, &bar);
    pin.time_hms = 223000;
    pin.rth_hms = 223000;
    pin.price_scale = 1;
    pin.high = pin.low = pin.close = 100;
    pin.dir = 1;
    pin.kind = 1;
    pin.grade = 3;
    tr_fxpair_on_bar(&s.pair_st, &pin, &pout);
    TR_CHECK(pout.entry_qty == 2);
    pin.dir = -1;
    s.zd_st.v68 = 1;
    tr_fxpair_on_bar(&s.zd_st, &pin, &pout);
    TR_CHECK(pout.entry_qty == 1);
    s.mkt = 1;
    s.contracts = 3;
    s.avg_entry = 100;
    bar.date = 20261015;
    bar.time = 20000;
    bar.bar_open += 60000000;
    bar.low = 85;
    bar.close = 90;
    tr_fxos_eval(&s, &bar);
    TR_CHECK(s.out.exit_qty == 3);
    TR_CHECK(s.out.pair_exit_qty == 2);
    TR_CHECK(s.out.pair_limit_qty == 0);
    TR_CHECK(s.out.zd_exit_qty == 0);
    TR_CHECK(s.out.zd_limit_qty == 1);
    char buf[8192];
    TR_CHECK(tr_fxos_format(buf, sizeof(buf), &s.out) > 0);
    const char *p = buf;
    for (int i = 0; i < 228 && p != 0; i++) {
        p = strchr(p, ',');
        if (p != 0) p++;
    }
    TR_CHECK(p != 0 && strncmp(p, "2,0,0,1", 7) == 0 && (p[7] == '\0' || p[7] == ','));
    snprintf(s.out.wr_name, sizeof(s.out.wr_name), "시가회복매수");
    TR_CHECK(tr_fxos_format(buf, sizeof(buf), &s.out) > 0);
    TR_CHECK(strstr(buf, "\"시가회복매수\"") != 0);
    bar.is_new_bar = false;
    tr_fxos_eval(&s, &bar);
    TR_CHECK(s.out.pair_exit_qty == 2);
    TR_CHECK(s.out.zd_limit_qty == 1);
}

int main(void) {
    test_exit_quantities_reach_chart_rows();
    test_pair_uses_current_slope();
    test_reset_is_flat_then_watch();
    test_rth_follows_date_and_turn_stays_off();
    test_replay_same_bar_does_not_advance();
    test_format_appends_turn();
    TR_TEST_SUMMARY();
}
