/* WSF_FXGapV1: 경계에서 시작한 세션 5개의 TR로 큰 갭을 판별 */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/fx_gap_v1.h"

static void feed(tr_fxgap_t *s, int i, int64_t hhmmss, double open) {
    tr_fxgap_input_t in;
    memset(&in, 0, sizeof(in));
    in.session_reset = i > 0;
    in.bar_index = i;
    in.time_hhmmss = hhmmss;
    in.bar_open = (tr_time_us_t)i * 60 * 1000000;
    in.open = open;
    in.high = open + 10;
    in.low = open - 10;
    in.close = 100;
    tr_fxgap_eval(s, &in);
}

static void test_big_gap_after_five_sessions(void) {
    tr_fxgap_t s;
    tr_fxgap_config_t cfg = {5, 0.35, 0.75};
    tr_fxgap_init(&s, &cfg);
    int64_t times[6] = {70000, 153000, 70000, 153000, 70000, 153000};
    for (int i = 0; i < 5; i++) {
        feed(&s, i, times[i], 100);
        TR_CHECK(!s.valid); /* 완성 TR이 5개 되기 전 */
    }
    feed(&s, 5, times[5], 120);
    TR_CHECK(s.valid);
    TR_CHECK(fabs(s.gap_ratio - 1.0) < 1e-9); /* |120-100| / 20 */
    TR_CHECK(s.gap_grade == 2);
    TR_CHECK(s.daily_weight == 0.0);
    TR_CHECK(s.gap_dir == 1);
    TR_CHECK(s.elapsed_min == 0);

    /* 같은 봉 재평가로 완성 세션이 늘지 않는다 */
    int32_t done = s.completed;
    feed(&s, 5, times[5], 120);
    TR_CHECK(s.completed == done);
    TR_CHECK(s.gap_grade == 2);
}

int main(void) {
    test_big_gap_after_five_sessions();
    TR_TEST_SUMMARY();
}
