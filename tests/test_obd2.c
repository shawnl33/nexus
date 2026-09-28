/* WSF_OrderBookDirectionV2 포팅 테스트: 점수식·상태·일자 리셋·무효 처리·부호 규칙 */

#include "test_util.h"

#include <math.h>

#include "core/indicators/orderbook_dir_v2.h"

static void test_futures_scores_and_state(void) {
    tr_obd2_t s;
    TR_CHECK(tr_obd2_init(&s, 10.0, 35.0, false, true));

    /* 첫 유효 평가: diff=400, total=avg=1000 → 점수 40 → 강세 상태 2 */
    tr_obd2_eval(&s, 700.0, 300.0, 100);
    TR_CHECK(s.validity == TR_VALIDITY_VALID);
    TR_CHECK(fabs(s.score - 40.0) < 1e-9);
    TR_CHECK(s.state == 2);
    TR_CHECK(s.slope3 == 0.0);

    tr_obd2_eval(&s, 700.0, 300.0, 100);
    TR_CHECK(fabs(s.score - 40.0) < 1e-9);

    /* 반대 불균형: 핵심점수는 −40이지만 이력 평활로 방향점수는 완만해진다.
       ma3 = (−40+40+40)/3 = 13.33, dir = (−40*60 + 13.33*25 + 13.33*15)/100 ≈ −18.67 → 상태 −1 */
    tr_obd2_eval(&s, 300.0, 700.0, 100);
    double expect = (-40.0 * 60.0 + (40.0 / 3.0) * 25.0 + (40.0 / 3.0) * 15.0) / 100.0;
    TR_CHECK(fabs(s.score - expect) < 1e-9);
    TR_CHECK(s.state == -1);
}

static void test_daily_reset(void) {
    tr_obd2_t s;
    tr_obd2_init(&s, 10.0, 35.0, false, true);
    tr_obd2_eval(&s, 700.0, 300.0, 100);
    tr_obd2_eval(&s, 700.0, 300.0, 100);

    /* 일자 변경 → 상태 리셋: 첫 평가처럼 동작 */
    tr_obd2_eval(&s, 600.0, 400.0, 101);
    TR_CHECK(fabs(s.score - 20.0) < 1e-9);
    TR_CHECK(s.state == 1);
    TR_CHECK(s.slope3 == 0.0); /* 이력도 리셋 */
}

static void test_invalid_when_no_book(void) {
    tr_obd2_t s;
    tr_obd2_init(&s, 10.0, 35.0, false, true);
    tr_obd2_eval(&s, 0.0, 0.0, 100);
    TR_CHECK(s.validity == TR_VALIDITY_MISSING);
    TR_CHECK(s.score == 0.0 && s.slope3 == 0.0 && s.state == 0);

    /* 무효 평가는 시계열을 갱신하지 않는다: 이후 첫 유효 평가가 첫 평가처럼 동작 */
    tr_obd2_eval(&s, 700.0, 300.0, 100);
    TR_CHECK(fabs(s.score - 40.0) < 1e-9);
    TR_CHECK(s.quote_no == 0);
}

static void test_stock_sign(void) {
    tr_obd2_t s;
    tr_obd2_init(&s, 10.0, 35.0, false, false); /* 주식: 매도 우세가 양수 */
    tr_obd2_eval(&s, 300.0, 700.0, 100);
    TR_CHECK(fabs(s.score - 40.0) < 1e-9);
}

static void test_sign_reverse(void) {
    tr_obd2_t s;
    tr_obd2_init(&s, 10.0, 35.0, true, true); /* 부호 반전 */
    tr_obd2_eval(&s, 700.0, 300.0, 100);
    TR_CHECK(fabs(s.score - (-40.0)) < 1e-9);
}

static void test_slope3_series(void) {
    tr_obd2_t s;
    tr_obd2_init(&s, 10.0, 35.0, false, true);
    /* 총잔량 1000 유지, 점수 60 → 40 → 28 → 8 의 계열 */
    tr_obd2_eval(&s, 800.0, 200.0, 100); /* dir = 60 */
    tr_obd2_eval(&s, 700.0, 300.0, 100); /* dir = 40 */
    tr_obd2_eval(&s, 600.0, 400.0, 100); /* dir = 28 */
    tr_obd2_eval(&s, 500.0, 500.0, 100); /* dir = 8, slope3 = 8−60 = −52 */
    TR_CHECK(fabs(s.score - 8.0) < 1e-9);
    TR_CHECK(fabs(s.slope3 - (-52.0)) < 1e-9);
}

static void test_avg_total_running(void) {
    tr_obd2_t s;
    tr_obd2_init(&s, 1.0, 35.0, false, true);
    /* 잔량이 변하면 러닝 평균으로 잔량차이점수를 정규화한다 */
    tr_obd2_eval(&s, 700.0, 300.0, 100);  /* avg=1000, core 40 */
    tr_obd2_eval(&s, 1400.0, 600.0, 100); /* avg=(1000+2000)/2=1500, diff=800 */
    /* 잔량차이점수 = 800/1500*100 ≈ 53.33, 비율 = 800/2000*100 = 40 */
    double expect_core = (800.0 / 1500.0 * 100.0 * 60.0 + 40.0 * 40.0) / 100.0;
    double ma3 = (40.0 + expect_core) / 2.0; /* 유효 2회라 ma3=핵심점수 */
    (void)ma3;
    /* dir = (core*60 + ma3(core)*25 + ma5(core)*15)/100 = core */
    TR_CHECK(fabs(s.score - expect_core) < 1e-9);
}

int main(void) {
    test_futures_scores_and_state();
    test_daily_reset();
    test_invalid_when_no_book();
    test_stock_sign();
    test_sign_reverse();
    test_slope3_series();
    test_avg_total_running();
    TR_TEST_SUMMARY();
}
