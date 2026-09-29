/* SMA 단위 테스트: 워밍업 valid 전이, 진행 봉 덮어쓰기(창 오염 없음), 창 밀어내기 */

#include "test_util.h"

#include <math.h>

#include "core/indicators/sma.h"

static void test_warmup(void) {
    tr_sma_t s;
    tr_sma_init(&s, 5);
    TR_CHECK(!s.valid && s.count == 0 && s.value == 0.0);

    /* period 미만에서는 valid=false */
    for (int i = 1; i <= 4; i++) {
        tr_sma_on_bar(&s, (double)i, true);
        TR_CHECK(!s.valid);
        TR_CHECK(s.count == i);
        TR_CHECK(s.value == 0.0);
    }

    /* period개 도달: 최근 5개 평균 */
    tr_sma_on_bar(&s, 5.0, true);
    TR_CHECK(s.valid);
    TR_CHECK(s.count == 5);
    TR_CHECK(fabs(s.value - 3.0) < 1e-9); /* (1+2+3+4+5)/5 */
}

static void test_progress_bar_overwrite(void) {
    tr_sma_t s;
    tr_sma_init(&s, 5);
    for (int i = 1; i <= 5; i++) {
        tr_sma_on_bar(&s, (double)i, true);
    }
    TR_CHECK(fabs(s.value - 3.0) < 1e-9);

    /* 진행 봉 재호출(is_new_bar=false): 현재 슬롯(5)만 덮어쓰고 창은 그대로 */
    tr_sma_on_bar(&s, 50.0, false);
    TR_CHECK(s.count == 5);
    TR_CHECK(fabs(s.value - 12.0) < 1e-9); /* (1+2+3+4+50)/5 */

    /* 재호출이 창을 오염시키지 않는다: 50은 새 슬롯이 아니라 같은 슬롯에서 7로 교체 */
    tr_sma_on_bar(&s, 7.0, false);
    TR_CHECK(s.count == 5);
    TR_CHECK(fabs(s.value - 3.4) < 1e-9); /* (1+2+3+4+7)/5 */

    /* 이후 새 봉 push는 오염 없이 가장 오래된 값(1)만 밀어낸다 */
    tr_sma_on_bar(&s, 6.0, true);
    TR_CHECK(s.count == 5);
    TR_CHECK(fabs(s.value - 4.4) < 1e-9); /* (2+3+4+7+6)/5 */
}

static void test_overwrite_during_warmup(void) {
    tr_sma_t s;
    tr_sma_init(&s, 5);

    /* 워밍업 중 진행 봉 재호출도 push가 아니라 덮어쓰기 */
    tr_sma_on_bar(&s, 1.0, true);
    tr_sma_on_bar(&s, 2.0, true);
    tr_sma_on_bar(&s, 3.0, true);
    tr_sma_on_bar(&s, 10.0, false); /* 슬롯 3을 10으로 교체 */
    TR_CHECK(s.count == 3);
    TR_CHECK(!s.valid);

    tr_sma_on_bar(&s, 4.0, true);
    tr_sma_on_bar(&s, 5.0, true);
    TR_CHECK(s.valid);
    TR_CHECK(fabs(s.value - 4.4) < 1e-9); /* (1+2+10+4+5)/5 */
}

static void test_window_eviction(void) {
    tr_sma_t s;
    tr_sma_init(&s, 5);

    /* period를 넘는 push: 가장 오래된 값(1,2,3)이 순서대로 빠진다 */
    for (int i = 1; i <= 8; i++) {
        tr_sma_on_bar(&s, (double)i, true);
    }
    TR_CHECK(s.count == 5);
    TR_CHECK(fabs(s.value - 6.0) < 1e-9); /* (4+5+6+7+8)/5 */

    tr_sma_on_bar(&s, 9.0, true);
    TR_CHECK(fabs(s.value - 7.0) < 1e-9); /* (5+6+7+8+9)/5 */
}

static void test_first_call_progress(void) {
    tr_sma_t s;
    tr_sma_init(&s, 60);

    /* 첫 호출이 is_new_bar=false여도 첫 슬롯을 만든다 (방어 경로) */
    tr_sma_on_bar(&s, 100.0, false);
    TR_CHECK(s.count == 1);
    TR_CHECK(!s.valid);
}

int main(void) {
    test_warmup();
    test_progress_bar_overwrite();
    test_overwrite_during_warmup();
    test_window_eviction();
    test_first_call_progress();
    TR_TEST_SUMMARY();
}
