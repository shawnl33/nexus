/* WSF_AutoSessionADXV1 포팅 테스트: Wilder 평활 손계산, 시드·유효 시점, 세션 리셋 */

#include "test_util.h"

#include <math.h>

#include "core/functions/auto_session_adx_v1.h"

static void test_wilder_hand_computed(void) {
    tr_adx1_t s;
    TR_CHECK(tr_adx1_init(&s, 2));

    /* 강한 상승 4봉: 손계산 추적 (주석 참조) */
    tr_adx1_on_bar(&s, 10.0, 8.0, 9.0, true);   /* b1: 시드 TR=2, count=1 */
    TR_CHECK(!s.valid);

    tr_adx1_on_bar(&s, 12.0, 9.0, 11.0, false); /* b2: +DM=2, TR=3, count=2 누적 */
    TR_CHECK(!s.valid);

    tr_adx1_on_bar(&s, 14.0, 11.0, 13.0, false); /* b3: Wilder 평활, DX 누적 */
    TR_CHECK(!s.valid); /* count=3 < 4 */

    tr_adx1_on_bar(&s, 16.0, 13.0, 15.0, false); /* b4: count=4 → ADX 시드 */
    /* smTR=5.75, sm+DM=3.5 → +DI=60.87, −DI=0 → DX=100, ADX=(100+100)/2=100 */
    TR_CHECK(s.valid);
    TR_CHECK(fabs(s.adx - 100.0) < 1e-9);
    TR_CHECK(fabs(s.plus_di - (3.5 / 5.75 * 100.0)) < 1e-6);
    TR_CHECK(s.minus_di == 0.0);
}

static void test_session_reset(void) {
    tr_adx1_t s;
    tr_adx1_init(&s, 2);
    tr_adx1_on_bar(&s, 10.0, 8.0, 9.0, true);
    tr_adx1_on_bar(&s, 12.0, 9.0, 11.0, false);
    tr_adx1_on_bar(&s, 14.0, 11.0, 13.0, false);
    tr_adx1_on_bar(&s, 16.0, 13.0, 15.0, false);
    TR_CHECK(s.valid);

    /* 세션 첫 봉: 리셋 → 다시 워밍업 */
    tr_adx1_on_bar(&s, 20.0, 18.0, 19.0, true);
    TR_CHECK(!s.valid);
    TR_CHECK(s.bar_count == 1);
    TR_CHECK(s.adx == 0.0);
}

static void test_period_clamp(void) {
    tr_adx1_t s;
    TR_CHECK(tr_adx1_init(&s, 1)); /* 하한 2 */
    TR_CHECK(s.period == 2);
}

int main(void) {
    test_wilder_hand_computed();
    test_session_reset();
    test_period_clamp();
    TR_TEST_SUMMARY();
}
