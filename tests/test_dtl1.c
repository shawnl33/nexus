/* WSF_1m_DailyTrendLinkV1 포팅 테스트: 세션 집계 완성 일봉, 회귀 1봉 투영, 추세 판정 연계 */

#include "test_util.h"

#include <math.h>

#include "core/functions/daily_trend_link_v1.h"

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
    TR_CHECK(ylv_count(&s.day_mids) == 5);
    TR_CHECK(s.primed_count == 5);
    TR_CHECK(s.link_valid);
    TR_CHECK(fabs(s.reg_slope - 2.0) < 1e-9);
    TR_CHECK(fabs(s.reg_r2 - 1.0) < 1e-9);
    TR_CHECK(fabs(s.reg_line - 110.0) < 1e-9); /* 1봉 투영 */

    /* 프라임 후 첫 실세션 완성이 정상 이어짐: 세션6(mid 110)이 [0]에 push */
    tr_dtl1_on_bar(&s, 111.0, 109.0, 110.0, true, 1, true);  /* 세션6 시작 */
    TR_CHECK(ylv_count(&s.day_mids) == 5); /* 진행 중 세션은 이력을 바꾸지 않는다 */
    tr_dtl1_on_bar(&s, 113.0, 111.0, 112.0, true, 2, true);  /* 세션7 시작 → 세션6 완성 */
    TR_CHECK(ylv_count(&s.day_mids) == 6);
    double dm0 = 0.0, dm1 = 0.0;
    TR_CHECK(ylv_at(&s.day_mids, 0, &dm0) && fabs(dm0 - 110.0) < 1e-9);
    TR_CHECK(ylv_at(&s.day_mids, 1, &dm1) && fabs(dm1 - 108.0) < 1e-9); /* 프라임 최신값이 뒤로 밀림 */
    /* 회귀는 최신 5개(102..110): 기울기 2, 투영 112 */
    TR_CHECK(s.link_valid);
    TR_CHECK(fabs(s.reg_slope - 2.0) < 1e-9);
    TR_CHECK(fabs(s.reg_line - 112.0) < 1e-9);

    /* 프라임은 진행 중 세션 집계를 덮지 않는다: 세션7 진행 갱신 */
    tr_dtl1_on_bar(&s, 115.0, 113.0, 114.0, false, 3, true);
    TR_CHECK(ylv_count(&s.day_mids) == 6);
    TR_CHECK(fabs(s.sess_high - 115.0) < 1e-9);
}

/* 채워진 링 위의 재prime(k>0): 기존 신값 뒤(더 과거)에 이어 붙는다.
 * prime 5 → 실세션 완성 1개 → 재prime 3 → 최종 링 레이아웃 전수 대조 */
static void test_reprime_appends_behind(void) {
    tr_dtl1_t s;
    tr_dtl1_init(&s, &CFG);

    /* 프라임 5개: 오래된 순 100,102,104,106,108 → 링 [0]=108, [4]=100 */
    double mids[5] = {100.0, 102.0, 104.0, 106.0, 108.0};
    tr_dtl1_prime(&s, mids, 5);

    /* 실세션 1개 완성: 세션A(mid 110)가 [0]에 push */
    tr_dtl1_on_bar(&s, 111.0, 109.0, 110.0, true, 1, true); /* 세션A 시작 */
    tr_dtl1_on_bar(&s, 113.0, 111.0, 112.0, true, 2, true); /* 세션B 시작 → A 완성 */
    /* 링: [0]=110, [1]=108, ..., [5]=100, count=6 */

    /* 재prime 3개: 오래된 순 90,92,94 */
    double mids2[3] = {90.0, 92.0, 94.0};
    tr_dtl1_prime(&s, mids2, 3);

    /* 기대 레이아웃(원 코드 인덱스 규약): 기존 이력이 앞, 재prime이 뒤([6]=재prime 최신) */
    static const double expect[9] = {
        110.0, 108.0, 106.0, 104.0, 102.0, 100.0, 94.0, 92.0, 90.0
    };
    TR_CHECK(ylv_count(&s.day_mids) == 9);
    TR_CHECK(s.primed_count == 8);
    for (int i = 0; i < 9; i++) {
        double v = 0.0;
        TR_CHECK(ylv_at(&s.day_mids, (size_t)i, &v) && v == expect[i]);
    }
}

/* 재prime 절단: room 부족 시 입력 최구부터 버리고 기존 값은 하나도 밀리지 않는다 */
static void test_reprime_truncates_to_room(void) {
    tr_dtl1_t s;
    tr_dtl1_init(&s, &CFG);

    /* 프라임 98개(오래된 순 1..98) → count=98, room=2 */
    double mids[98];
    for (int i = 0; i < 98; i++) {
        mids[i] = (double)(i + 1);
    }
    tr_dtl1_prime(&s, mids, 98);
    TR_CHECK(ylv_count(&s.day_mids) == 98);

    /* 재prime 5개: 입력 {501,502,503}은 버려지고 {504,505}만 붙는다 */
    double mids2[5] = {501.0, 502.0, 503.0, 504.0, 505.0};
    tr_dtl1_prime(&s, mids2, 5);

    TR_CHECK(ylv_count(&s.day_mids) == 100);
    TR_CHECK(s.primed_count == 100);
    for (int i = 0; i < 98; i++) {
        double v = 0.0;
        TR_CHECK(ylv_at(&s.day_mids, (size_t)i, &v) && v == (double)(98 - i));
    }
    double v98 = 0.0, v99 = 0.0;
    TR_CHECK(ylv_at(&s.day_mids, 98, &v98) && v98 == 505.0); /* 재prime 최신 */
    TR_CHECK(ylv_at(&s.day_mids, 99, &v99) && v99 == 504.0);
    TR_CHECK(!ylv_at(&s.day_mids, 100, &(double){0.0})); /* cap 초과 참조 불가 */
}

int main(void) {
    test_regression_and_projection();
    test_trend_weak_side();
    test_partial_first_session_excluded();
    test_prime();
    test_reprime_appends_behind();
    test_reprime_truncates_to_room();
    TR_TEST_SUMMARY();
}
