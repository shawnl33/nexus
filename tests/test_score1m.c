/* 통합 점수 테스트: 방향 0 → −2 분기, 세션 첫 봉 리셋, 비교 항, 구성 요소 합산 */

#include "test_util.h"

#include <string.h>

#include "core/indicators/score_1m.h"

#define CAP 4
static double storage[CAP];

static tr_score1m_input_t make_in(double h, double l, double c, double htf_dir, int64_t day) {
    tr_score1m_input_t in;
    memset(&in, 0, sizeof(in));
    in.high = h;
    in.low = l;
    in.close = c;
    in.htf_direction = htf_dir;
    in.trading_day = day;
    in.is_min_1 = true;
    in.reg_valid = true;
    in.reg_r2 = 0.50;
    in.reg_flat_line = 10.0;
    in.ob_score = 0.0;
    return in;
}

static void test_zero_direction_goes_negative(void) {
    tr_score1m_t s;
    TR_CHECK(tr_score1m_init(&s, 3, 0.40, storage, CAP));

    /* 첫 봉: 세션 첫 봉 → 미래방향 0 → 기본 항 −2 (중립이 아니다) */
    tr_score1m_input_t in = make_in(12.0, 10.0, 11.0, 5.0, 100);
    in.reg_r2 = 0.30; /* 회귀방향 0으로 두고 기본 항만 관찰 */
    in.reg_valid = false;
    in.close = 11.0;
    tr_score1m_on_bar(&s, &in);
    /* 마켓방향 0(단일 봉, 기울기 0), 회귀 0, 호가 0 → 점수 = −2 */
    TR_CHECK(s.active);
    TR_CHECK(s.future_dir == 0.0);
    TR_CHECK(s.score == -2);
}

static void test_full_sum(void) {
    tr_score1m_t s;
    tr_score1m_init(&s, 3, 0.40, storage, CAP);

    tr_score1m_input_t in = make_in(12.0, 10.0, 11.0, 5.0, 100);
    in.reg_valid = false;
    tr_score1m_on_bar(&s, &in); /* 첫 봉: future=0, prev=0 */

    /* 봉2: 미래방향 8 (>0 → +2, >prev 0 → +1), 상승 마켓 +1, 회귀 +1, 호가 +1 */
    in = make_in(14.0, 12.0, 13.5, 8.0, 100);
    in.reg_valid = true;
    in.reg_r2 = 0.50;
    in.reg_flat_line = 10.0;
    in.ob_score = 42.0;
    tr_score1m_on_bar(&s, &in);
    TR_CHECK(s.score == 2 + 1 + 1 + 1 + 1);
}

static void test_session_first_extra_penalty(void) {
    tr_score1m_t s;
    tr_score1m_init(&s, 3, 0.40, storage, CAP);

    tr_score1m_input_t in = make_in(12.0, 10.0, 11.0, 5.0, 100);
    in.reg_valid = false;
    tr_score1m_on_bar(&s, &in);
    in = make_in(13.0, 11.0, 12.0, 8.0, 100); /* 전일 마지막 방향 +8 */
    in.reg_valid = false;
    tr_score1m_on_bar(&s, &in);
    TR_CHECK(s.future_dir == 8.0);

    /* 세션 첫 봉(날짜 변경): 방향 0 → −2, 0 < prev 8 → 추가 −1.
       마켓 중심은 상승 유지(11,12,12 평균 상승)라 +1 상쇄 → 합계 −2 */
    in = make_in(13.0, 11.0, 12.0, 9.0, 101);
    in.reg_valid = false;
    in.ob_score = 0.0;
    tr_score1m_on_bar(&s, &in);
    TR_CHECK(s.future_dir == 0.0);
    TR_CHECK(s.market_dir == 1);
    TR_CHECK(s.score == -2 - 1 + 1 + 0 + 0);
}

static void test_not_min1_inactive(void) {
    tr_score1m_t s;
    tr_score1m_init(&s, 3, 0.40, storage, CAP);
    tr_score1m_input_t in = make_in(12.0, 10.0, 11.0, 5.0, 100);
    in.is_min_1 = false;
    tr_score1m_on_bar(&s, &in);
    TR_CHECK(!s.active);
    TR_CHECK(s.score == 0);
}

int main(void) {
    test_zero_direction_goes_negative();
    test_full_sum();
    test_session_first_extra_penalty();
    test_not_min1_inactive();
    TR_TEST_SUMMARY();
}
