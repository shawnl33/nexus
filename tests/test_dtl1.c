/* WSF_1m_DailyTrendLinkV1 포팅 테스트: 세션 집계 완성 일봉, 회귀 1봉 투영, 추세 판정 연계 */

#include "test_util.h"

#include <math.h>

#include "core/indicators/daily_trend_link_v1.h"

static tr_dtl1_config_t CFG = {5, 0.40, 1.0};

/* 세션 첫 봉을 공급. 중간값이 mid가 되도록 H/L을 준다. */
static void feed_session_first(tr_dtl1_t *s, double mid, int64_t bar_index) {
    tr_dtl1_on_bar(s, mid + 1.0, mid - 1.0, mid, true, bar_index, true);
}

static void test_regression_and_projection(void) {
    tr_dtl1_t s;
    tr_dtl1_init(&s, &CFG);

    /* 완성 일봉 중간값: 100, 102, 104, 106, 108 (완전 직선) */
    double mids[5] = {100, 102, 104, 106, 108};
    for (int i = 0; i < 5; i++) {
        feed_session_first(&s, mids[i], i + 1);
        TR_CHECK(!s.link_valid); /* i+1개 < 5 */
    }
    /* 6번째 세션 첫 봉에서 5개 완성 → 회귀 유효 */
    tr_dtl1_on_bar(&s, 112.0, 110.0, 112.0, true, 6, true);
    TR_CHECK(s.link_valid);
    TR_CHECK(fabs(s.reg_slope - 2.0) < 1e-9);
    TR_CHECK(fabs(s.reg_r2 - 1.0) < 1e-9);
    /* 마지막 회귀값 = 2*4+100 = 108, 1봉 투영 회귀선 = 110 */
    TR_CHECK(fabs(s.reg_line - 110.0) < 1e-9);

    /* 추세 판정: 가격 112 >= 110, 기울기 양수 → 방향 1, 상태 2, 강도 100 */
    TR_CHECK(s.trend.valid);
    TR_CHECK(s.trend.dir == 1);
    TR_CHECK(s.trend.state == 2);
    TR_CHECK(fabs(s.trend.strength - 100.0) < 1e-9);
}

static void test_trend_weak_side(void) {
    tr_dtl1_t s;
    tr_dtl1_init(&s, &CFG);
    double mids[5] = {100, 102, 104, 106, 108};
    for (int i = 0; i < 5; i++) {
        feed_session_first(&s, mids[i], i + 1);
    }
    tr_dtl1_on_bar(&s, 106.0, 104.0, 105.0, true, 6, true); /* 가격 105 < 회귀선 110 */
    TR_CHECK(s.trend.dir == 1);
    TR_CHECK(s.trend.state == 1);
    TR_CHECK(fabs(s.trend.strength - 65.0) < 1e-9);
}

static void test_partial_first_session_excluded(void) {
    tr_dtl1_t s;
    tr_dtl1_init(&s, &CFG);
    /* 첫 로딩 세션이 중간 시작(session_first=false) → 완성으로 저장하지 않음 */
    tr_dtl1_on_bar(&s, 50.0, 40.0, 45.0, false, 1, true);
    double mids[5] = {100, 102, 104, 106, 108};
    for (int i = 0; i < 5; i++) {
        feed_session_first(&s, mids[i], i + 2);
    }
    /* 45 중간값 세션이 제외되어 5개 완성은 전부 100..108 계열 */
    tr_dtl1_on_bar(&s, 112.0, 110.0, 112.0, true, 7, true);
    TR_CHECK(s.link_valid);
    TR_CHECK(fabs(s.reg_slope - 2.0) < 1e-9);
}

static void test_prime(void) {
    tr_dtl1_t s;
    tr_dtl1_init(&s, &CFG);

    /* 프라임: 오래된 순으로 100, 102, 104, 106, 108 → 즉시 유효 조건 만족 */
    double mids[5] = {100, 102, 104, 106, 108};
    tr_dtl1_prime(&s, mids, 5);
    TR_CHECK(s.day_count == 5);
    TR_CHECK(s.primed_count == 5);
    TR_CHECK(s.link_valid);
    TR_CHECK(fabs(s.reg_slope - 2.0) < 1e-9);
    TR_CHECK(fabs(s.reg_r2 - 1.0) < 1e-9);
    TR_CHECK(fabs(s.reg_line - 110.0) < 1e-9); /* 1봉 투영 */

    /* 프라임 후 첫 실세션 완성이 정상 이어짐: 세션6(mid 110)이 [0]에 push */
    tr_dtl1_on_bar(&s, 111.0, 109.0, 110.0, true, 1, true);  /* 세션6 시작 */
    TR_CHECK(s.day_count == 5); /* 진행 중 세션은 이력을 바꾸지 않는다 */
    tr_dtl1_on_bar(&s, 113.0, 111.0, 112.0, true, 2, true);  /* 세션7 시작 → 세션6 완성 */
    TR_CHECK(s.day_count == 6);
    TR_CHECK(fabs(s.day_mids[0] - 110.0) < 1e-9);
    TR_CHECK(fabs(s.day_mids[1] - 108.0) < 1e-9); /* 프라임 최신값이 뒤로 밀림 */
    /* 회귀는 최신 5개(102..110): 기울기 2, 투영 112 */
    TR_CHECK(s.link_valid);
    TR_CHECK(fabs(s.reg_slope - 2.0) < 1e-9);
    TR_CHECK(fabs(s.reg_line - 112.0) < 1e-9);

    /* 프라임은 진행 중 세션 집계를 덮지 않는다: 세션7 진행 갱신 */
    tr_dtl1_on_bar(&s, 115.0, 113.0, 114.0, false, 3, true);
    TR_CHECK(s.day_count == 6);
    TR_CHECK(fabs(s.sess_high - 115.0) < 1e-9);
}

int main(void) {
    test_regression_and_projection();
    test_trend_weak_side();
    test_partial_first_session_excluded();
    test_prime();
    TR_TEST_SUMMARY();
}
