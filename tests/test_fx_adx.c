/* WSF_FXADXV1: 세션 리셋형 Wilder ADX. 같은 봉 재평가는 한 봉만 반영 */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/fx_adx_v1.h"

static tr_fxadx_input_t bar(int n, double h, double l, double c, bool reset) {
    tr_fxadx_input_t in;
    memset(&in, 0, sizeof(in));
    in.session_reset = reset;
    in.high = h;
    in.low = l;
    in.close = c;
    in.bar_open = (tr_time_us_t)n * 60 * 1000000;
    return in;
}

static void test_seeds_like_wilder(void) {
    tr_fxadx_t s;
    TR_CHECK(tr_fxadx_init(&s, 2));
    tr_fxadx_input_t b1 = bar(1, 10, 8, 9, true);
    tr_fxadx_eval(&s, &b1);
    TR_CHECK(!s.valid);
    tr_fxadx_input_t b2 = bar(2, 12, 9, 11, false);
    tr_fxadx_eval(&s, &b2);
    tr_fxadx_input_t b3 = bar(3, 14, 11, 13, false);
    tr_fxadx_eval(&s, &b3);
    TR_CHECK(!s.valid);
    tr_fxadx_input_t b4 = bar(4, 16, 13, 15, false);
    tr_fxadx_eval(&s, &b4);
    TR_CHECK(s.valid);
    TR_CHECK(fabs(s.adx - 100.0) < 1e-9);
    TR_CHECK(fabs(s.plus_di - (3.5 / 5.75 * 100.0)) < 1e-6);

    /* 같은 봉을 다시 평가해도 ADX가 한 단계 더 나가지 않는다 */
    tr_fxadx_eval(&s, &b4);
    TR_CHECK(fabs(s.adx - 100.0) < 1e-9);
    TR_CHECK(fabs(s.plus_di - (3.5 / 5.75 * 100.0)) < 1e-6);
}

static void test_session_reset_clears(void) {
    tr_fxadx_t s;
    tr_fxadx_init(&s, 2);
    tr_fxadx_input_t b1 = bar(1, 10, 8, 9, true);
    tr_fxadx_eval(&s, &b1);
    tr_fxadx_input_t b2 = bar(2, 12, 9, 11, false);
    tr_fxadx_eval(&s, &b2);
    tr_fxadx_input_t b3 = bar(3, 14, 11, 13, false);
    tr_fxadx_eval(&s, &b3);
    tr_fxadx_input_t b4 = bar(4, 16, 13, 15, false);
    tr_fxadx_eval(&s, &b4);
    TR_CHECK(s.valid);
    tr_fxadx_input_t b5 = bar(5, 10, 9, 9.5, true);
    tr_fxadx_eval(&s, &b5);
    TR_CHECK(!s.valid);
    TR_CHECK(s.adx == 0.0);
}

int main(void) {
    test_seeds_like_wilder();
    test_session_reset_clears();
    TR_TEST_SUMMARY();
}
