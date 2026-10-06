/* WSF_KSSession1mV1: 첫 봉은 1, BDate·DayIndex 경계에서만 증가, 같은 봉은 유지. */

#include "test_util.h"

#include "core/functions/ks_session_v1.h"

static void test_session_boundaries(void) {
    tr_ks_session_t s;
    tr_ks_session_init(&s);

    TR_CHECK(tr_ks_session_eval(&s, true, 1, 20261005, 0) == 1);
    /* 같은 봉 재평가 */
    TR_CHECK(tr_ks_session_eval(&s, false, 1, 20261005, 0) == 1);
    /* 같은 장, DayIndex만 증가 */
    TR_CHECK(tr_ks_session_eval(&s, true, 2, 20261005, 1) == 1);
    TR_CHECK(tr_ks_session_eval(&s, true, 3, 20261005, 40) == 1);

    /* 시각이 아니라 날짜가 바뀌면 새 장 */
    TR_CHECK(tr_ks_session_eval(&s, true, 4, 20261006, 0) == 2);
    /* DayIndex가 되돌아가면 새 장 */
    TR_CHECK(tr_ks_session_eval(&s, true, 5, 20261006, 10) == 2);
    TR_CHECK(tr_ks_session_eval(&s, true, 6, 20261006, 0) == 3);
    /* DayIndex 0이 연속이면 올리지 않는다 */
    TR_CHECK(tr_ks_session_eval(&s, true, 7, 20261006, 0) == 3);
}

int main(void) {
    test_session_boundaries();
    TR_TEST_SUMMARY();
}
