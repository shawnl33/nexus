#include "test_util.h"

#include <string.h>

#include "core/indicators/fx_os_hist.h"
#include "core/indicators/fx_os_scope.h"
#include "core/functions/fx_synthetic_lines_v1.h"

static int near(double a, double b) {
    return a > b - 1e-6 && a < b + 1e-6;
}

static void bar_clear(tr_fxos_hist_in_t *in) {
    memset(in, 0, sizeof(*in));
    in->two_window = 3;
    in->two_min_bars = 3;
    in->flat_window = 0;
}

/* 1. 목표 폭 4 다음 2는 50. 목표 0은 비율만 0. 세션이 바뀌면 최대가 지워진다. */
static void test_three_ratio(void) {
    tr_fxos_hist_t h;
    tr_fxos_hist_in_t in;
    tr_fxos_hist_init(&h);
    bar_clear(&in);
    in.t1 = 10;
    in.t2 = 12;
    in.t3 = 14;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.three_ratio, 100));
    in.t1 = 10;
    in.t2 = 11;
    in.t3 = 12;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.three_ratio, 50));
    in.t2 = 0;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.three_ratio, 0));
    in.t1 = 10;
    in.t2 = 11;
    in.t3 = 12;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.three_ratio, 50));
    in.session_first = 1;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.three_ratio, 100));
}

/* 2. 3봉이 지난 뒤 삼선 폭이 바뀌면 직전 구간을 닫는다. 창은 3봉. */
static void test_two_zone_ratio(void) {
    tr_fxos_hist_t h;
    tr_fxos_hist_in_t in;
    int i;
    tr_fxos_hist_init(&h);
    bar_clear(&in);
    in.t1 = 10;
    in.t2 = 10;
    in.t3 = 20;
    in.high = 10;
    in.low = 0;
    for (i = 0; i < 3; i++) {
        tr_fxos_hist_bar(&h, &in);
    }
    TR_CHECK(near(h.two_ratio, 100));
    in.t3 = 21;
    in.high = 5;
    in.low = 4;
    for (i = 0; i < 3; i++) {
        tr_fxos_hist_bar(&h, &in);
    }
    TR_CHECK(near(h.two_ratio, 100));
    in.t3 = 22;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.two_ratio, 10));
}

/* 3. 네 회귀선이 모두 있을 때만 폭을 낸다. */
static void test_flat_spread_ratio(void) {
    tr_fxos_hist_t h;
    tr_fxos_hist_in_t in;
    tr_fxos_hist_init(&h);
    bar_clear(&in);
    in.flat_ready = 1;
    in.flat_30 = 4;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.flat_ratio, 100));
    in.flat_30 = 2;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.flat_ratio, 50));
    in.flat_ready = 0;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.flat_ratio, 0));
    in.flat_ready = 1;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.flat_ratio, 50));
    in.session_first = 1;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.flat_ratio, 100));
}

/* 4. 네 극값이 있을 때 세션 최대 대비 폭이다. */
static void test_swing_ratio(void) {
    tr_fxos_hist_t h;
    tr_fxos_hist_in_t in;
    tr_fxos_hist_init(&h);
    bar_clear(&in);
    in.fv_ok = 1;
    in.rise_hi = 20;
    in.rise_lo = 10;
    in.fall_hi = 15;
    in.fall_lo = 10;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.swing, 100));
    in.rise_hi = 19;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.swing, 90));
    in.fall_lo = 0;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.swing, 0));
    in.fall_lo = 10;
    in.session_first = 1;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.swing, 100));
}

/* 5. 직전 다리의 고저는 이번 봉을 빼는 최소·최대다. */
static void test_prior_leg(void) {
    tr_fxos_hist_t h;
    tr_fxos_hist_in_t in;
    tr_fxos_hist_init(&h);
    bar_clear(&in);
    in.low = 5;
    tr_fxos_hist_bar(&h, &in);
    in.low = 4;
    tr_fxos_hist_bar(&h, &in);
    in.low = 3;
    tr_fxos_hist_bar(&h, &in);
    in.low = 9;
    in.prev_dn_len = 3;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.prior_low, 3));
    in.prev_dn_len = 0;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.prior_low, 0));

    tr_fxos_hist_init(&h);
    bar_clear(&in);
    in.high = 1;
    tr_fxos_hist_bar(&h, &in);
    in.high = 8;
    tr_fxos_hist_bar(&h, &in);
    in.high = 2;
    tr_fxos_hist_bar(&h, &in);
    in.high = 0;
    in.prev_up_len = 3;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(near(h.prior_high, 8));
}

/* 6. 기울기 -21 구간에 폭비율 100이 있으면 경험이 있다. */
static void test_width_seen(void) {
    tr_fxos_hist_t h;
    tr_fxos_hist_in_t in;
    int i;
    tr_fxos_hist_init(&h);
    bar_clear(&in);
    in.slope = -21;
    for (i = 0; i < 20; i++) {
        tr_fxos_hist_bar(&h, &in);
    }
    TR_CHECK(h.width_seen == 0);
    in.sq_ratio = 100;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(h.width_seen == 1);
    in.slope = -20;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(h.width_seen == 0);
}

/* 7. 23봉이 차기 전에는 장대봉이 없다. */
static void test_tall_ring(void) {
    tr_fxos_hist_t h;
    tr_fxos_hist_in_t in;
    int i;
    tr_fxos_hist_init(&h);
    bar_clear(&in);
    in.open = 10;
    in.high = 10.5;
    in.low = 9.5;
    in.close = 10;
    for (i = 0; i < 22; i++) {
        tr_fxos_hist_bar(&h, &in);
    }
    TR_CHECK(h.tall_buy == 0);
    TR_CHECK(h.tall_sell == 0);
    in.high = 13;
    in.low = 10;
    in.open = 10.2;
    in.close = 10.1;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(h.tall_buy == 1);
    TR_CHECK(h.tall_sell == 0);
}

/* 8. 전환 과열, 빨강 수, 경과 봉, 직전 통합상태. */
static void test_turn_memory(void) {
    tr_fxos_hist_t h;
    tr_fxos_hist_in_t in;
    int i;
    tr_fxos_hist_init(&h);
    bar_clear(&in);
    in.session_first = 1;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(h.bars_since_buy == 999);
    in.session_first = 0;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(h.bars_since_buy == 1000);
    in.turn_buy = 1;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(h.bars_since_buy == 0);
    in.turn_buy = 0;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(h.bars_since_buy == 1);

    tr_fxos_hist_init(&h);
    bar_clear(&in);
    in.prev_up_slope = 30;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(h.hot_up == 0);
    in.prev_up_slope = 31;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(h.hot_up == 1);
    in.prev_up_slope = 0;
    in.prev_dn_slope = 31;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(h.hot_dn == 1);

    tr_fxos_hist_init(&h);
    bar_clear(&in);
    in.sq_hold = 1;
    in.sq_color = 1;
    for (i = 0; i < 6; i++) {
        tr_fxos_hist_bar(&h, &in);
    }
    TR_CHECK(h.red_n == 6);

    tr_fxos_hist_init(&h);
    bar_clear(&in);
    in.unified = -100;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(h.had_m100 == 0);
    in.unified = 0;
    for (i = 0; i < 10; i++) {
        tr_fxos_hist_bar(&h, &in);
    }
    TR_CHECK(h.had_m100 == 1);
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(h.had_m100 == 0);

    tr_fxos_hist_init(&h);
    bar_clear(&in);
    in.unified = 100;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(h.had_p100 == 0);
    in.unified = 0;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(h.had_p100 == 1);
}

/* 압축 박스 상단 1/3은 빨강, 하단 1/3은 파랑, 가운데는 회색. */
static void test_box_color(void) {
    TR_CHECK(tr_fxos_box_color(100, 100, 90, 1) == 1);
    TR_CHECK(tr_fxos_box_color(90, 100, 90, 1) == -1);
    TR_CHECK(tr_fxos_box_color(95, 100, 90, 1) == 0);
    TR_CHECK(tr_fxos_box_color(90.0 + 10.0 * (2.0 / 3.0), 100, 90, 1) == 1);
    TR_CHECK(tr_fxos_box_color(90.0 + 10.0 * (1.0 / 3.0), 100, 90, 1) == -1);
    TR_CHECK(tr_fxos_box_color(90.0 + 10.0 * (2.0 / 3.0) - 0.01, 100, 90, 1) == 0);
    TR_CHECK(tr_fxos_box_color(100, 100, 90, 0) == 0);
    TR_CHECK(tr_fxos_box_color(100, 90, 100, 1) == 0);
}

/* 삼선이 준비되고 신호합계·복합압축·비율점수가 0이면 회색이다. */
static void test_three_gray(void) {
    tr_fxos_hist_t h;
    tr_fxos_hist_in_t in;
    tr_fxos_hist_init(&h);
    bar_clear(&in);
    in.fv_ok = 1;
    in.t1 = 10;
    in.t2 = 12;
    in.t3 = 14;
    in.flat_ready = 1;
    in.flat_30 = 10;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(h.gray == 1);
    in.flat_30 = 2;
    tr_fxos_hist_bar(&h, &in);
    TR_CHECK(h.gray == 0);
}

/* 합성선 출력은 스코프 입력의 회귀·마켓중심으로 들어간다. */
static void test_synth_prices_fill_input(void) {
    tr_fxos_input_t in;
    tr_fxsyn_t s5, s15, s30;
    memset(&in, 0, sizeof(in));
    memset(&s5, 0, sizeof(s5));
    memset(&s15, 0, sizeof(s15));
    memset(&s30, 0, sizeof(s30));
    s5.out_reg_valid = 1;
    s5.out_reg = 10;
    s5.out_mkt_valid = 1;
    s5.out_mkt = 11;
    s15.out_reg_valid = 1;
    s15.out_reg = 20;
    s30.out_mkt_valid = 1;
    s30.out_mkt = 31;
    tr_fxos_set_synth(&in, &s5, &s15, &s30);
    TR_CHECK(in.reg5_ok == 1 && in.reg5 == 10);
    TR_CHECK(in.mkt5_ok == 1 && in.mkt5 == 11);
    TR_CHECK(in.reg15_ok == 1 && in.reg15 == 20);
    TR_CHECK(in.mkt15_ok == 0);
    TR_CHECK(in.reg30_ok == 0);
    TR_CHECK(in.mkt30_ok == 1 && in.mkt30 == 31);
}

/* 9. 스코프가 장대봉 고리와 미래값·압축·진폭을 필터 입력에 넣는다. */
static void test_scope_feeds_hist(void) {
    tr_fxos_t s;
    tr_fxos_input_t bar;
    int i;
    TR_CHECK(tr_fxos_init(&s, 1.0));
    memset(&bar, 0, sizeof(bar));
    bar.date = 20260715;
    bar.volume = 10;
    bar.is_new_bar = true;
    bar.open = 10;
    bar.high = 10.5;
    bar.low = 9.5;
    bar.close = 10;
    for (i = 0; i < 22; i++) {
        bar.time = 100000 + i * 100;
        bar.bar_open = (int64_t)(i + 1) * 60000000;
        tr_fxos_eval(&s, &bar);
    }
    TR_CHECK(s.applied.tall_buy == 0);
    bar.time = 100000 + 22 * 100;
    bar.bar_open = 23 * 60000000;
    bar.high = 13;
    bar.low = 10;
    bar.open = 10.2;
    bar.close = 10.1;
    tr_fxos_eval(&s, &bar);
    TR_CHECK(s.applied.tall_buy == 1);
    TR_CHECK(s.applied.tall_sell == 0);
    TR_CHECK(s.applied.target5 == s.sig_fv.out.persist_target[4]);
    TR_CHECK(s.applied.rise_hi == s.sig_fv.out.lup_high);
    TR_CHECK(s.applied.rise_lo == s.sig_fv.out.lup_low);
    TR_CHECK(s.applied.fall_hi == s.sig_fv.out.ldn_high);
    TR_CHECK(s.applied.fall_lo == s.sig_fv.out.ldn_low);
    TR_CHECK(s.applied.sq_bars == s.squeeze.out.hold);
    TR_CHECK(s.applied.amp == s.out.flat_amp);
}

/* 스코프가 5·15·30분 회귀선과 압축 박스 색을 이력에 넣는다. */
static void test_scope_flat_lines_and_box_color(void) {
    tr_fxos_t s;
    tr_fxos_input_t bar;
    int i;
    int color;
    TR_CHECK(tr_fxos_init(&s, 1.0));
    memset(&bar, 0, sizeof(bar));
    bar.date = 20260715;
    bar.volume = 10;
    bar.is_new_bar = true;
    bar.reg5_ok = 1;
    bar.reg15_ok = 1;
    bar.reg30_ok = 1;
    bar.reg5 = 50;
    bar.reg15 = 50;
    bar.reg30 = 50;
    for (i = 0; i < 8; i++) {
        bar.time = 100000 + i * 100;
        bar.bar_open = (int64_t)(i + 1) * 60000000;
        bar.open = 100;
        bar.high = 101;
        bar.low = 95;
        bar.close = 100;
        tr_fxos_eval(&s, &bar);
    }
    TR_CHECK(s.sig_fv.out.reg_flat != 0);
    TR_CHECK(s.applied.flat_ratio > 0);
    color = tr_fxos_box_color(bar.close, s.squeeze.out.band_hi, s.squeeze.out.band_lo,
                              s.squeeze.out.profile_valid);
    TR_CHECK(color == 1);
    TR_CHECK(s.hist.color_r[0] == color);
}

int main(void) {
    test_three_ratio();
    test_two_zone_ratio();
    test_flat_spread_ratio();
    test_swing_ratio();
    test_prior_leg();
    test_width_seen();
    test_tall_ring();
    test_turn_memory();
    test_box_color();
    test_three_gray();
    test_synth_prices_fill_input();
    test_scope_feeds_hist();
    test_scope_flat_lines_and_box_color();
    TR_TEST_SUMMARY();
}
