#include "test_util.h"

#include <string.h>

#include "core/functions/fx_decision_v2.h"

static tr_fxdec_input_t base(void) {
    tr_fxdec_input_t in;
    memset(&in, 0, sizeof(in));
    in.future_dir = 1;
    in.profile_valid = 1;
    in.center = 10;
    in.close = 11;
    in.mode = 1;
    in.adx = 25;
    in.plus_di = 30;
    in.minus_di = 10;
    in.adx_valid = 1;
    in.adx_trend = 20;
    in.adx_strong = 35;
    return in;
}

static void test_confirm_and_watch(void) {
    tr_fxdec_input_t in = base();
    tr_fxdec_out_t o = tr_fxdec_eval(&in);
    TR_CHECK(o.unified == 100);
    TR_CHECK(o.di == 1 && o.adx == 1);

    in.adx = 10;
    o = tr_fxdec_eval(&in);
    TR_CHECK(o.unified == 50 && o.adx == 0);

    in = base();
    in.future_dir = 0;
    o = tr_fxdec_eval(&in);
    TR_CHECK(o.unified == 0);

    in = base();
    in.future_dir = -1;
    in.close = 9;
    in.plus_di = 10;
    in.minus_di = 30;
    o = tr_fxdec_eval(&in);
    TR_CHECK(o.unified == -100);
}

static void test_mode_changes_profile_gate(void) {
    tr_fxdec_input_t in = base();
    in.mode = 2;
    in.state5 = 0;
    in.close = 11;
    tr_fxdec_out_t o = tr_fxdec_eval(&in);
    TR_CHECK(o.unified == 50);

    in.state5 = 1;
    o = tr_fxdec_eval(&in);
    TR_CHECK(o.unified == 100);
}

int main(void) {
    test_confirm_and_watch();
    test_mode_changes_profile_gate();
    TR_TEST_SUMMARY();
}
