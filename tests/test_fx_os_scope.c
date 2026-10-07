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

int main(void) {
    test_reset_is_flat_then_watch();
    TR_TEST_SUMMARY();
}
