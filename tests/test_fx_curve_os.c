#include "test_util.h"

#include <string.h>

#include "core/indicators/fx_curve_os.h"

static void test_score_plot_on_rising_bar(void) {
    tr_fxcu_t s;
    TR_CHECK(tr_fxcu_init(&s, 1.0));
    tr_fxmirae_input_t in;
    memset(&in, 0, sizeof(in));
    in.bar.date = 20241001;
    in.bar.time = 90000;
    in.bar.bar_open = 60000000;
    in.bar.high = 101;
    in.bar.low = 100;
    in.bar.close = 100.5;
    in.bar.volume = 10;
    tr_fxcu_eval(&s, &in, 0, 0, 0);
    TR_CHECK(s.plots[0].on);
    TR_CHECK(s.plots[0].value == 0.0);
    TR_CHECK(!s.plots[18].on);
}

int main(void) {
    test_score_plot_on_rising_bar();
    TR_TEST_SUMMARY();
}
