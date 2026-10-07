#include "test_util.h"

#include <string.h>

#include "core/functions/fx_squeeze_v1.h"

static tr_fxsq_input_t bar(int i, int reset) {
    tr_fxsq_input_t in;
    memset(&in, 0, sizeof(in));
    in.session_reset = reset;
    in.bar_open = (tr_time_us_t)i * 60000000;
    in.high = 10;
    in.low = 10;
    in.close = 10;
    in.volume = 1;
    in.price_scale = 1;
    in.value_mult = 1;
    in.min_bars = 1;
    in.width_bars = 1;
    in.ratio_bars = 1;
    in.narrow_pct = 101; /* 창 1이면 비율 100. 101 미만이라 좁음 */
    in.stage1_bars = 2;
    in.confirm_bars = 3;
    return in;
}

static void test_hold_then_release(void) {
    tr_fxsq_t s;
    tr_fxsq_init(&s);
    tr_fxsq_input_t b1 = bar(1, 1);
    tr_fxsq_eval(&s, &b1);
    TR_CHECK(s.out.narrow == 1 && s.out.hold == 1 && s.out.release == 0);

    tr_fxsq_input_t b2 = bar(2, 0);
    tr_fxsq_eval(&s, &b2);
    TR_CHECK(s.out.hold == 2);

    tr_fxsq_input_t b3 = bar(3, 0);
    b3.narrow_pct = 50; /* 비율 100은 좁지 않다 */
    tr_fxsq_eval(&s, &b3);
    TR_CHECK(s.out.narrow == 0);
    TR_CHECK(s.out.release == 1);
    TR_CHECK(s.out.release_len == 2);
    TR_CHECK(s.out.hold == 0);

    tr_fxsq_eval(&s, &b3);
    TR_CHECK(s.out.release == 1 && s.out.release_len == 2);
}

int main(void) {
    test_hold_then_release();
    TR_TEST_SUMMARY();
}
