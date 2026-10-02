/* WSF_FXTrendV1: 경계 세션 (고+저)/2 다섯 개로 기울기 2인 일봉 회귀 */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/fx_trend_v1.h"

static void feed(tr_fxtrend_t *s, int i, int64_t hhmmss, double px) {
    tr_fxtrend_input_t in;
    memset(&in, 0, sizeof(in));
    in.session_reset = i > 0;
    in.bar_index = i;
    in.time_hhmmss = hhmmss;
    in.bar_open = (tr_time_us_t)i * 60 * 1000000;
    in.high = px;
    in.low = px;
    in.close = px;
    in.price_scale = 1;
    in.min_r2 = 0.4;
    tr_fxtrend_eval(s, &in);
}

static void test_projected_line(void) {
    tr_fxtrend_t s;
    tr_fxtrend_config_t cfg = {5};
    tr_fxtrend_init(&s, &cfg);
    int64_t times[6] = {70000, 153000, 70000, 153000, 70000, 153000};
    double px[6] = {10, 12, 14, 16, 18, 20};
    for (int i = 0; i < 5; i++) {
        feed(&s, i, times[i], px[i]);
        TR_CHECK(s.valid == 0);
    }
    feed(&s, 5, times[5], px[5]);
    /* 완성 중간값 10,12,14,16,18 → 기울기 2, 한 봉 투영 회귀선 20 */
    TR_CHECK(s.link_valid == 1);
    TR_CHECK(fabs(s.slope - 2.0) < 1e-9);
    TR_CHECK(fabs(s.line - 20.0) < 1e-9);
    TR_CHECK(fabs(s.r2 - 1.0) < 1e-9);
    TR_CHECK(s.valid == 1);
    TR_CHECK(s.dir == 1);
    TR_CHECK(s.state == 2);
    TR_CHECK(fabs(s.strength - 100.0) < 1e-6);
}

int main(void) {
    test_projected_line();
    TR_TEST_SUMMARY();
}
