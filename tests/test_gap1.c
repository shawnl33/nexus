/* WSF_GapRegimeV1 포팅 테스트: 세션 TR 평균, 갭비율·등급·비중, 경과분(자정), 유효 조건 */

#include "test_util.h"

#include <math.h>

#include "core/indicators/gap_regime_v1.h"

/* n=10개 완성 세션을 만든다: 전부 H−L=10, 종가 100 → 평균TR=10, 직전 종가 100 */
static void feed_11_sessions(tr_gap1_t *s, double day11_open) {
    tr_gap1_config_t cfg = {10, 0.35, 0.75};
    tr_gap1_init(s, &cfg);
    for (int i = 1; i <= 10; i++) {
        tr_gap1_on_bar(s, 100.0, 105.0, 95.0, 100.0, 540, true, i, true);
    }
    /* 11번째 세션 첫 봉: 이 시점에 10개 완성 세션이 확정된다 */
    tr_gap1_on_bar(s, day11_open, day11_open + 2.0, day11_open - 2.0, 100.0, 540, true, 11, true);
}

static void test_mid_gap(void) {
    tr_gap1_t s;
    feed_11_sessions(&s, 103.5); /* 갭비율 = 3.5/10 = 0.35 → 등급 1 */
    TR_CHECK(s.valid);
    TR_CHECK(fabs(s.gap_ratio - 0.35) < 1e-9);
    TR_CHECK(s.gap_grade == 1);
    TR_CHECK(s.daily_weight == 0.5);
    TR_CHECK(s.gap_dir == 1);
}

static void test_big_gap(void) {
    tr_gap1_t s;
    feed_11_sessions(&s, 107.5); /* 갭비율 = 0.75 → 등급 2 */
    TR_CHECK(fabs(s.gap_ratio - 0.75) < 1e-9);
    TR_CHECK(s.gap_grade == 2);
    TR_CHECK(s.daily_weight == 0.0);
}

static void test_normal(void) {
    tr_gap1_t s;
    feed_11_sessions(&s, 101.0); /* 갭비율 = 0.10 → 등급 0 */
    TR_CHECK(s.gap_grade == 0);
    TR_CHECK(s.daily_weight == 1.0);
}

static void test_warmup_not_valid(void) {
    tr_gap1_t s;
    tr_gap1_config_t cfg = {10, 0.35, 0.75};
    tr_gap1_init(&s, &cfg);
    for (int i = 1; i <= 5; i++) {
        tr_gap1_on_bar(&s, 100.0, 105.0, 95.0, 100.0, 540, true, i, true);
    }
    TR_CHECK(!s.valid); /* 완성 세션 4개 < 10 */
    TR_CHECK(s.gap_ratio == 0.0 && s.daily_weight == 0.0);
}

static void test_elapsed_min(void) {
    tr_gap1_t s;
    feed_11_sessions(&s, 100.0);
    TR_CHECK(s.elapsed_min == 0);
    tr_gap1_on_bar(&s, 100.0, 101.0, 99.0, 100.0, 555, false, 12, true);
    TR_CHECK(s.elapsed_min == 15);
}

static void test_midnight_elapsed(void) {
    tr_gap1_t s;
    tr_gap1_config_t cfg = {10, 0.35, 0.75};
    tr_gap1_init(&s, &cfg);
    tr_gap1_on_bar(&s, 100.0, 105.0, 95.0, 100.0, 1380, true, 1, true); /* 23:00 시작 */
    tr_gap1_on_bar(&s, 100.0, 101.0, 99.0, 100.0, 60, false, 2, true);  /* 익일 01:00 */
    TR_CHECK(s.elapsed_min == 120);
}

int main(void) {
    test_mid_gap();
    test_big_gap();
    test_normal();
    test_warmup_not_valid();
    test_elapsed_min();
    test_midnight_elapsed();
    TR_TEST_SUMMARY();
}
