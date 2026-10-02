/* 스나이퍼 점수: 직전 범위 아래면 +1. 압축은 목표폭이 줄어든 뒤 대기에 들어간다. */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/indicators/fx_sniper.h"

static void test_score_below_previous_range(void) {
    tr_fxsniper_t s;
    tr_fxsniper_config_t cfg;
    tr_fxsniper_default_config(&cfg, 1);
    tr_fxsniper_init(&s, &cfg);
    tr_fxsniper_input_t in;
    memset(&in, 0, sizeof(in));
    in.session_reset = true;
    in.bar_open = 60 * 1000000;
    in.high = 11;
    in.low = 10;
    in.close = 10.5;
    in.target[0] = 10;
    in.target[1] = 12;
    in.target[2] = 11;
    in.break_ready = 1;
    in.prev_range_valid = 1;
    in.pos = -1;
    in.price_ratio = 10;
    in.reg_ready = 1;
    in.reg_ratio = 10;
    tr_fxsniper_eval(&s, &in);
    TR_CHECK(s.out.score == 1);
    TR_CHECK(s.out.score_ex == 1);
    TR_CHECK(s.out.ratio_score == 2);
    TR_CHECK(s.out.rgb == 0x80A0FF);
}

static void test_compression_enters_wait(void) {
    tr_fxsniper_t s;
    tr_fxsniper_config_t cfg;
    tr_fxsniper_default_config(&cfg, 1);
    cfg.range_period = 2;
    cfg.atr_period = 2;
    cfg.hold_bars = 1;
    cfg.price_limit = 101;
    tr_fxsniper_init(&s, &cfg);
    for (int i = 1; i <= 2; i++) {
        tr_fxsniper_input_t in;
        memset(&in, 0, sizeof(in));
        in.session_reset = i == 1;
        in.bar_open = (tr_time_us_t)i * 60 * 1000000;
        in.high = 101;
        in.low = 100;
        in.close = 100.5;
        in.target[0] = 1;
        in.target[1] = 101;
        in.target[2] = 51;
        tr_fxsniper_eval(&s, &in);
    }
    tr_fxsniper_input_t tight;
    memset(&tight, 0, sizeof(tight));
    tight.bar_open = 3 * 60 * 1000000;
    tight.high = 51;
    tight.low = 50;
    tight.close = 50.5;
    tight.target[0] = 10;
    tight.target[1] = 12;
    tight.target[2] = 11;
    tr_fxsniper_eval(&s, &tight);
    TR_CHECK(s.out.calc_ready);
    TR_CHECK(s.out.compressed);
    TR_CHECK(s.out.stage == 2);
}

/* 가격압축이탈은 다음 봉에 나오고, 세션 첫 봉은 그 값을 숨긴다. */
static void test_price_exit_lags_one_bar(void) {
    tr_fxsniper_t s;
    tr_fxsniper_config_t cfg;
    tr_fxsniper_default_config(&cfg, 1);
    tr_fxsniper_init(&s, &cfg);
    tr_fxsniper_input_t b1;
    memset(&b1, 0, sizeof(b1));
    b1.session_reset = true;
    b1.bar_open = 60 * 1000000;
    b1.break_ready = 1;
    b1.prev_range_valid = 1;
    b1.pos = 1;
    b1.price_ratio = 10;
    b1.below = 0;
    b1.above = 1;
    tr_fxsniper_eval(&s, &b1);
    TR_CHECK(s.out.px_exit == 0);
    TR_CHECK(s.out.below == 0);
    TR_CHECK(s.out.above == 1);
    TR_CHECK(s.out.session_reset == 1);

    tr_fxsniper_input_t b2 = b1;
    b2.session_reset = false;
    b2.bar_open = 2 * 60 * 1000000;
    b2.pos = -1;
    tr_fxsniper_eval(&s, &b2);
    TR_CHECK(s.out.px_exit == 1);
    tr_fxsniper_eval(&s, &b2);
    TR_CHECK(s.out.px_exit == 1);

    tr_fxsniper_input_t b3 = b2;
    b3.session_reset = true;
    b3.bar_open = 3 * 60 * 1000000;
    tr_fxsniper_eval(&s, &b3);
    TR_CHECK(s.out.px_exit == 0);
}

int main(void) {
    test_score_below_previous_range();
    test_compression_enters_wait();
    test_price_exit_lags_one_bar();
    TR_TEST_SUMMARY();
}
