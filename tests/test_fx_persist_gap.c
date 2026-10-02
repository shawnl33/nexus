/* 지속목표차: 3선 폭, 세션이 바뀌면 직전 최고폭, 같은 봉은 봉수를 한 번만 */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/indicators/fx_persist_gap.h"

static tr_fxpgap_input_t feed(int n, bool reset, double a, double b, double c, double hi, double lo) {
    tr_fxpgap_input_t in;
    memset(&in, 0, sizeof(in));
    in.session_reset = reset;
    in.bar_open = (tr_time_us_t)n * 60 * 1000000;
    in.high = hi;
    in.low = lo;
    in.target[0] = a;
    in.target[1] = b;
    in.target[2] = c;
    return in;
}

static void test_three_line_gap_and_session(void) {
    tr_fxpgap_t s;
    tr_fxpgap_init(&s, 3, 25);
    tr_fxpgap_input_t b1 = feed(1, true, 10, 14, 12, 15, 9);
    tr_fxpgap_eval(&s, &b1);
    TR_CHECK(s.out.ready);
    TR_CHECK(fabs(s.out.gap - 4.0) < 1e-9); /* 14-10 */
    TR_CHECK(fabs(s.out.peak - 4.0) < 1e-9);
    TR_CHECK(fabs(s.out.ratio - 100.0) < 1e-9);
    TR_CHECK(!s.out.plot3); /* 첫 봉은 직전 구간이 없다 */
    TR_CHECK(fabs(s.out.cnt - 1.0) < 1e-9);

    tr_fxpgap_eval(&s, &b1);
    TR_CHECK(fabs(s.out.cnt - 1.0) < 1e-9);

    tr_fxpgap_input_t b2 = feed(2, false, 10, 14, 12, 15, 8);
    tr_fxpgap_eval(&s, &b2);
    TR_CHECK(fabs(s.out.cnt - 2.0) < 1e-9);
    TR_CHECK(fabs(s.out.gap - 4.0) < 1e-9);

    tr_fxpgap_input_t b3 = feed(3, true, 10, 20, 12, 21, 11);
    tr_fxpgap_eval(&s, &b3);
    TR_CHECK(fabs(s.out.gap - 10.0) < 1e-9);
    TR_CHECK(fabs(s.out.prev_peak - 4.0) < 1e-9);
    TR_CHECK(fabs(s.out.prev_ratio - 250.0) < 1e-9);
    TR_CHECK(s.out.plot3 && s.out.plot5);
    TR_CHECK(fabs(s.out.cnt - 1.0) < 1e-9);
}

static void test_five_lines_need_all_targets(void) {
    tr_fxpgap_t s;
    tr_fxpgap_init(&s, 5, 25);
    tr_fxpgap_input_t b = feed(1, true, 10, 14, 12, 15, 9);
    tr_fxpgap_eval(&s, &b);
    TR_CHECK(!s.out.ready); /* 4·5번 목표가 0 */
    b.target[3] = 11;
    b.target[4] = 18;
    tr_fxpgap_eval(&s, &b);
    TR_CHECK(s.out.ready);
    TR_CHECK(fabs(s.out.gap - 8.0) < 1e-9); /* 18-10 */
}

static void test_four_line_flat_reg_gap(void) {
    tr_fxpgap_t s;
    tr_fxpgap_init(&s, 4, 25);
    tr_fxpgap_input_t in = feed(1, true, 100, 110, 90, 130, 112);
    in.target[3] = 105; /* 30분선. Plot5 색 기준 */
    tr_fxpgap_eval(&s, &in);
    TR_CHECK(s.out.ready);
    TR_CHECK(fabs(s.out.gap - 20.0) < 1e-9); /* 110−90 */
    TR_CHECK(s.out.rgb5 == 0xFF0000);        /* 구간최저 112 > 30분선, L > 최고 110 */
}

static void test_union_thick_when_price_outside(void) {
    tr_fxpgap_t s;
    tr_fxpgap_init(&s, 3, 25);
    tr_fxpgap_input_t wide = feed(1, true, 1, 101, 51, 100, 0);
    wide.close = 40;
    tr_fxpgap_eval(&s, &wide); /* 폭 100, 비율 100 */
    tr_fxpgap_input_t tight = feed(2, false, 10, 12, 11, 9, 8);
    tight.close = 8.5; /* 봉 전체가 최저 10 아래, 폭 2 / 최고 100 = 2% */
    tr_fxpgap_eval(&s, &tight);
    tr_fxunion_paint(&s, 1, 1, 10, 1, 10, tight.high, tight.low);
    TR_CHECK(s.out.union_w == 3);
    TR_CHECK(s.out.union_rgb == 0x000096);
}

int main(void) {
    test_three_line_gap_and_session();
    test_five_lines_need_all_targets();
    test_four_line_flat_reg_gap();
    test_union_thick_when_price_outside();
    TR_TEST_SUMMARY();
}
