#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/indicators/fx_sniper_co.h"

static tr_snco_input_t bar(int i, double a, double b, double c) {
    tr_snco_input_t in;
    memset(&in, 0, sizeof(in));
    in.date = 20241001;
    in.time = 90000 + (i - 1) * 100;
    in.bar_open = (tr_time_us_t)i * 60000000;
    in.is_new_bar = true;
    in.high = 110;
    in.low = 100;
    in.close = 105;
    in.volume = 10;
    in.price_scale = 1;
    in.fv_ok = 1;
    in.target[0] = a;
    in.target[1] = b;
    in.target[2] = c;
    in.mkt30 = 1;
    return in;
}

static int plot_on(const tr_snco_t *s, int id, double *v) {
    for (int i = 0; i < TR_SNCO_PLOTS; i++) {
        if (s->plots[i].on && s->plots[i].id == id) {
            *v = s->plots[i].value;
            return 1;
        }
    }
    return 0;
}

static void test_three_ratio_uses_session_peak(void) {
    tr_snco_t s;
    tr_snco_init(&s);
    tr_snco_input_t b1 = bar(1, 100, 110, 130);
    tr_snco_eval(&s, &b1);
    double v = 0;
    TR_CHECK(plot_on(&s, 10, &v));
    TR_CHECK(fabs(v - 100.0) < 1e-9);

    tr_snco_input_t b2 = bar(2, 100, 105, 110);
    tr_snco_eval(&s, &b2);
    TR_CHECK(plot_on(&s, 10, &v));
    TR_CHECK(fabs(v - (10.0 / 30.0) * 100.0) < 1e-9);

    tr_snco_eval(&s, &b2);
    TR_CHECK(plot_on(&s, 10, &v));
    TR_CHECK(fabs(v - (10.0 / 30.0) * 100.0) < 1e-9);
    TR_CHECK(s.scope_state <= 2 && s.scope_state >= -2);
}

int main(void) {
    test_three_ratio_uses_session_peak();
    TR_TEST_SUMMARY();
}
