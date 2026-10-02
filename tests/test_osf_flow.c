/* CLV 고가 종가 = +100. 흐름은 기간이 찬 뒤 유효. 같은 방향 결합은 1.2배 후 100 제한. */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/osf_clv_pressure_v1.h"
#include "core/functions/osf_clv_vol_flow_v1.h"
#include "core/functions/osf_vol_flow_v1.h"

static void test_clv_at_high_is_strong(void) {
    tr_osf_clv_t s;
    tr_osf_clv_init(&s, 20, 40, 0);
    tr_osf_clv_input_t in = {.high = 10, .low = 0, .close = 10, .bar_open = 60 * 1000000};
    tr_osf_clv_eval(&s, &in);
    TR_CHECK(fabs(s.score - 100.0) < 1e-9);
    TR_CHECK(s.state == 2);
    TR_CHECK(s.valid == 1);
    TR_CHECK(s.slope3 == 0.0);
}

static void test_flow_needs_period(void) {
    tr_osf_flow_t s;
    tr_osf_flow_init(&s, 20, 40, 0, 2);
    tr_osf_flow_input_t in = {.high = 10, .low = 0, .close = 10, .volume = 5, .bar_open = 60 * 1000000};
    tr_osf_flow_eval(&s, &in);
    TR_CHECK(!s.valid);
    TR_CHECK(fabs(s.score - 100.0) < 1e-9);
    in.bar_open *= 2;
    tr_osf_flow_eval(&s, &in);
    TR_CHECK(s.valid);
    TR_CHECK(s.state == 2);
}

static void test_combo_caps_at_100(void) {
    tr_osf_combo_t s;
    tr_osf_combo_init(&s, 20, 40, 0, 2);
    tr_osf_flow_input_t in = {.high = 10, .low = 0, .close = 10, .volume = 4, .bar_open = 60 * 1000000};
    tr_osf_combo_eval(&s, &in);
    in.bar_open *= 2;
    tr_osf_combo_eval(&s, &in);
    TR_CHECK(s.valid);
    TR_CHECK(fabs(s.score - 100.0) < 1e-9); /* 100 * 1.2 → 100 */
    TR_CHECK(s.state == 2);
}

int main(void) {
    test_clv_at_high_is_strong();
    test_flow_needs_period();
    test_combo_caps_at_100();
    TR_TEST_SUMMARY();
}
