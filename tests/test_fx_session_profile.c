#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/fx_session_profile_v2.h"

static tr_fxprof_input_t bar(int i, double h, double l, double c, double v, int reset) {
    tr_fxprof_input_t in;
    memset(&in, 0, sizeof(in));
    in.session_reset = reset;
    in.bar_open = (tr_time_us_t)i * 60000000;
    in.high = h;
    in.low = l;
    in.close = c;
    in.volume = v;
    in.price_scale = 1.0;
    in.value_mult = 1.5;
    in.min_bars = 2;
    in.period = 0;
    return in;
}

static void test_cumulative_floor_and_state(void) {
    tr_fxprof_t s;
    tr_fxprof_init(&s);
    tr_fxprof_input_t b1 = bar(1, 10, 8, 9, 0, 1);
    tr_fxprof_eval(&s, &b1);
    TR_CHECK(s.out.valid == 0);
    TR_CHECK(s.out.center == 9.0);
    TR_CHECK(s.out.hi1 == 10.5 && s.out.lo1 == 7.5);

    tr_fxprof_input_t b2 = bar(2, 12, 10, 11, 2, 0);
    tr_fxprof_eval(&s, &b2);
    TR_CHECK(s.out.valid == 1);
    TR_CHECK(fabs(s.out.center - (31.0 / 3.0)) < 1e-9);
    TR_CHECK(s.out.hi1 == s.out.center + 1.5);
    TR_CHECK(s.out.state5 == 0);

    /* 같은 봉 재평가: 누적이 한 번만 */
    tr_fxprof_eval(&s, &b2);
    TR_CHECK(fabs(s.out.center - (31.0 / 3.0)) < 1e-9);
    TR_CHECK(s.out.session_bars == 2);
}

static void test_window_overrides_center(void) {
    tr_fxprof_t s;
    tr_fxprof_init(&s);
    tr_fxprof_input_t b1 = bar(1, 0, 0, 0, 1, 1);
    b1.min_bars = 1;
    b1.period = 1;
    tr_fxprof_eval(&s, &b1);
    TR_CHECK(s.out.center == 0.0);
    tr_fxprof_input_t b2 = bar(2, 30, 30, 30, 1, 0);
    b2.min_bars = 1;
    b2.period = 1;
    tr_fxprof_eval(&s, &b2);
    /* 최근 1봉만: 중심 30. 세션 누적이면 15 */
    TR_CHECK(s.out.center == 30.0);
}

int main(void) {
    test_cumulative_floor_and_state();
    test_window_overrides_center();
    TR_TEST_SUMMARY();
}
