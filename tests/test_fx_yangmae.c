/* 삼선 폭이 바뀌면 직전 고저를 고정하고, 그 위를 종가가 넘으면 첫 이탈 */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/indicators/fx_yangmae.h"

static tr_fxymae_input_t bar(int n, bool reset, double t1, double t2, double t3, double h, double l, double c) {
    tr_fxymae_input_t in;
    memset(&in, 0, sizeof(in));
    in.session_reset = reset;
    in.targets_ready = 1;
    in.bar_open = (tr_time_us_t)n * 60 * 1000000;
    in.t1 = t1;
    in.t2 = t2;
    in.t3 = t3;
    in.high = h;
    in.low = l;
    in.close = c;
    in.volume = 10;
    return in;
}

static void test_width_change_breaks_previous_range(void) {
    tr_fxymae_t s;
    tr_fxymae_config_t cfg;
    tr_fxymae_default_config(&cfg, 1);
    tr_fxymae_init(&s, &cfg);
    tr_fxymae_input_t b1 = bar(1, true, 10, 12, 14, 11, 10, 10.5);
    tr_fxymae_eval(&s, &b1);
    TR_CHECK(s.out.pos == 0);
    TR_CHECK(!s.out.prev_valid);
    TR_CHECK(fabs(s.out.cnt - 1) < 1e-9);

    tr_fxymae_eval(&s, &b1);
    TR_CHECK(fabs(s.out.cnt - 1) < 1e-9);

    tr_fxymae_input_t b2 = bar(2, false, 10, 12, 14, 15, 9, 14);
    tr_fxymae_eval(&s, &b2);
    TR_CHECK(fabs(s.out.cnt - 2) < 1e-9);

    tr_fxymae_input_t b3 = bar(3, false, 10, 20, 12, 16, 9, 16);
    tr_fxymae_eval(&s, &b3);
    TR_CHECK(s.out.prev_valid);
    TR_CHECK(fabs(s.out.prev_hi - 15) < 1e-9);
    TR_CHECK(fabs(s.out.prev_lo - 9) < 1e-9);
    TR_CHECK(s.out.pos == 1);
    TR_CHECK(s.out.first_break == 1);
    TR_CHECK(s.out.show_first == 0);
    TR_CHECK(fabs(s.out.cnt - 1) < 1e-9);

    /* 첫이탈 표시는 다음 봉이다. 같은 폭에서 다시 벗어나도 다시 켜지지 않는다. */
    tr_fxymae_input_t b4 = bar(4, false, 10, 20, 12, 17, 10, 16.5);
    tr_fxymae_eval(&s, &b4);
    TR_CHECK(s.out.pos == 1);
    TR_CHECK(s.out.first_break == 0);
    TR_CHECK(s.out.show_first == 1);
    tr_fxymae_eval(&s, &b4);
    TR_CHECK(s.out.show_first == 1);
    TR_CHECK(s.out.first_break == 0);

    tr_fxymae_input_t b5 = bar(5, false, 10, 20, 12, 13, 11, 12);
    tr_fxymae_eval(&s, &b5);
    TR_CHECK(s.out.pos == 0);
    TR_CHECK(s.out.show_first == 0);

    tr_fxymae_input_t b6 = bar(6, false, 10, 20, 12, 20, 12, 20);
    tr_fxymae_eval(&s, &b6);
    TR_CHECK(s.out.pos == 1);
    TR_CHECK(s.out.first_break == 0);
    TR_CHECK(s.out.show_first == 0);

    tr_fxymae_input_t b7 = bar(7, false, 10, 30, 12, 40, 10, 40);
    tr_fxymae_eval(&s, &b7);
    TR_CHECK(s.out.first_break == 1);
    TR_CHECK(s.out.show_first == 0);

    tr_fxymae_input_t b8 = bar(8, false, 10, 30, 12, 41, 10, 41);
    tr_fxymae_eval(&s, &b8);
    TR_CHECK(s.out.first_break == 0);
    TR_CHECK(s.out.show_first == 1);

    tr_fxymae_input_t b9 = bar(9, true, 10, 30, 12, 41, 10, 41);
    tr_fxymae_eval(&s, &b9);
    TR_CHECK(s.out.show_first == 0);
}

/* 가격거래량압축 표시는 직전 봉 값이다.
 * 넓은 구간으로 최대 범위를 만든 뒤, 좁은 구간 둘을 이어 비율을 떨어뜨린다.
 * 세션 첫 봉은 그리지 않고, 같은 봉을 다시 계산해도 표시는 한 번만 늦다. */
static void test_compression_display_lags_one_bar(void) {
    tr_fxymae_t s;
    tr_fxymae_config_t cfg;
    tr_fxymae_default_config(&cfg, 1);
    tr_fxymae_init(&s, &cfg);

    tr_fxymae_input_t b1 = bar(1, true, 0, 2, 4, 100, 0, 50);
    b1.volume = 100;
    tr_fxymae_eval(&s, &b1);
    TR_CHECK(s.out.show_price == 0);
    TR_CHECK(s.out.show_vol == 0);
    TR_CHECK(s.out.show_both == 0);

    tr_fxymae_input_t b2 = bar(2, false, 0, 10, 5, 10, 9, 9.5);
    b2.volume = 40;
    tr_fxymae_eval(&s, &b2);
    TR_CHECK(s.out.show_price == 1);
    TR_CHECK(fabs(s.out.show_price_ratio - 100) < 1e-9);
    TR_CHECK(s.out.show_vol == 0);
    TR_CHECK(s.out.show_both == 0);
    tr_fxymae_eval(&s, &b2);
    TR_CHECK(s.out.show_price == 1);
    TR_CHECK(fabs(s.out.show_price_ratio - 100) < 1e-9);

    tr_fxymae_input_t b3 = bar(3, false, 0, 20, 8, 10, 9, 9.5);
    b3.volume = 10;
    tr_fxymae_eval(&s, &b3);
    TR_CHECK(s.out.show_price == 1);
    TR_CHECK(fabs(s.out.show_price_ratio - 100) < 1e-9);
    TR_CHECK(s.out.show_vol == 1);
    TR_CHECK(fabs(s.out.show_vol_ratio - 40) < 1e-9);
    TR_CHECK(s.out.show_both == 0);

    tr_fxymae_input_t b4 = bar(4, false, 0, 20, 8, 10, 9.5, 9.7);
    b4.volume = 10;
    tr_fxymae_eval(&s, &b4);
    TR_CHECK(s.out.show_price == 1);
    TR_CHECK(fabs(s.out.show_price_ratio - 1) < 1e-9);
    TR_CHECK(s.out.show_vol == 1);
    TR_CHECK(fabs(s.out.show_vol_ratio - 25) < 1e-9);
    TR_CHECK(s.out.show_both == 1);

    tr_fxymae_input_t b5 = bar(5, true, 0, 20, 8, 10, 9, 9.5);
    b5.volume = 10;
    tr_fxymae_eval(&s, &b5);
    TR_CHECK(s.out.show_price == 0);
    TR_CHECK(s.out.show_vol == 0);
    TR_CHECK(s.out.show_both == 0);
    tr_fxymae_eval(&s, &b5);
    TR_CHECK(s.out.show_price == 0);
    TR_CHECK(s.out.show_both == 0);
}

static void test_show_first_down_lags_one_bar(void) {
    tr_fxymae_t s;
    tr_fxymae_config_t cfg;
    tr_fxymae_default_config(&cfg, 1);
    tr_fxymae_init(&s, &cfg);
    tr_fxymae_input_t b1 = bar(1, true, 10, 12, 14, 20, 10, 15);
    tr_fxymae_eval(&s, &b1);
    tr_fxymae_input_t b2 = bar(2, false, 30, 12, 14, 8, 5, 8);
    tr_fxymae_eval(&s, &b2);
    TR_CHECK(s.out.pos == -1);
    TR_CHECK(s.out.first_break == -1);
    TR_CHECK(s.out.show_first == 0);
    tr_fxymae_input_t b3 = bar(3, false, 30, 12, 14, 8, 5, 7);
    tr_fxymae_eval(&s, &b3);
    TR_CHECK(s.out.first_break == 0);
    TR_CHECK(s.out.show_first == -1);
    tr_fxymae_input_t hidden = bar(4, false, 30, 12, 14, 8, 5, 7);
    hidden.targets_ready = 0;
    tr_fxymae_eval(&s, &hidden);
    TR_CHECK(s.out.show_first == 0);
}

int main(void) {
    test_width_change_breaks_previous_range();
    test_show_first_down_lags_one_bar();
    test_compression_display_lags_one_bar();
    TR_TEST_SUMMARY();
}
