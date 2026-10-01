/* WSF_FXSwingV1 포팅 테스트: 대기→확정 상태 전이, 지난구간 메모리·유지/붕괴/확장,
 * 실효등급(얕은/중간/깊은/붕괴/없음), 대기 취소 흡수, 동일 봉 재평가 복원, 세션 리셋 */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/fx_swing_v1.h"

static void feed(tr_fxsw_t *s, bool reset, double dir, double h, double l, bool new_bar) {
    tr_fxsw_input_t in;
    memset(&in, 0, sizeof(in));
    in.session_reset = reset;
    in.future_dir = dir;
    in.min_hold_bars = 2;
    in.high = h;
    in.low = l;
    in.is_new_bar = new_bar;
    tr_fxsw_eval(s, &in);
}

/* 시나리오 A: 상승 대기→확정, 구간 연장, 하락 전환 시 지난상승 메모리 확정 */
static void test_up_confirm_and_extension(void) {
    tr_fxsw_t s;
    TR_CHECK(tr_fxsw_init(&s));
    feed(&s, true, 1.0, 110.0, 100.0, true);  /* 리셋 봉: 상태 기계 진행 없음 */
    feed(&s, false, 1.0, 110.0, 100.0, true); /* 양수전환 → 대기 시작 */
    TR_CHECK(s.st.wait_dir == 1 && s.st.wait_count == 1.0);
    feed(&s, false, 1.0, 112.0, 105.0, true); /* 대기 충족 → 상승 확정 (이전 구간 없음) */
    TR_CHECK(s.st.wait_dir == 0);
    TR_CHECK(s.st.up_high == 112.0 && s.st.up_low == 100.0 && s.st.up_count == 2.0);
    TR_CHECK(s.out_leg_dir == 1);
    TR_CHECK(s.out_lup_grade == -1 && s.out_ldn_grade == -1); /* 메모리 없음 */

    feed(&s, false, 1.0, 115.0, 108.0, true); /* 상승 구간 연장 */
    TR_CHECK(s.st.up_high == 115.0 && s.st.up_low == 100.0 && s.st.up_count == 3.0);

    feed(&s, false, -1.0, 113.0, 102.0, true); /* 음수전환 → 하락 대기 */
    TR_CHECK(s.st.wait_dir == -1);
    feed(&s, false, -1.0, 111.0, 95.0, true);  /* 하락 확정 → 지난상승 메모리 확정 */
    TR_CHECK(s.out_lup.high == 115.0 && s.out_lup.low == 100.0 && s.out_lup.count == 3.0);
    TR_CHECK(s.out_lup.size == 15.0);
    TR_CHECK(s.out_lup.lvl_382 == 100.0 + 15.0 * 0.382);
    TR_CHECK(s.out_lup.lvl_500 == 100.0 + 15.0 * 0.500);
    TR_CHECK(s.out_lup.lvl_618 == 100.0 + 15.0 * 0.618);
    TR_CHECK(s.out_lup.ext_100 == 0.0); /* 직전 하락이 없어 유지/확장 미계산 */
    TR_CHECK(s.out_lup.broken == 0 && s.out_lup.weak == 0);
    TR_CHECK(s.out_lup_grade == 2); /* 유지 플래그 없음·구조약화 아님 → 깊은목표 */
    TR_CHECK(s.out_leg_dir == -1);
    TR_CHECK(s.out_time_ratio == 2.0 / 3.0); /* 하락Count 2 / 지난상승Count 3 */
}

/* 시나리오 B (A에 이어): 깊은 되돌림 → 지난상승 붕괴·확장 계산·지난하락 구조약화 */
static void test_deep_retrace_broken_ext(void) {
    tr_fxsw_t s;
    tr_fxsw_init(&s);
    feed(&s, true, 1.0, 110.0, 100.0, true);
    feed(&s, false, 1.0, 110.0, 100.0, true);
    feed(&s, false, 1.0, 112.0, 105.0, true);
    feed(&s, false, 1.0, 115.0, 108.0, true);
    feed(&s, false, -1.0, 113.0, 102.0, true);
    feed(&s, false, -1.0, 111.0, 95.0, true);  /* 지난상승 (115,100,3) 확정 */
    feed(&s, false, -1.0, 110.0, 92.0, true);  /* 하락 연장 (113,92,3) */
    TR_CHECK(s.out_time_ratio == 3.0 / 3.0);
    feed(&s, false, 1.0, 108.0, 94.0, true);   /* 상승 대기 */
    feed(&s, false, 1.0, 109.0, 96.0, true);   /* 상승 확정 → 지난하락 (113,92,3) 확정 */

    /* 되돌림 저가 92가 지난상승 저가 100을 뚫었다 → 붕괴 */
    TR_CHECK(s.out_lup.broken == 1);
    TR_CHECK(s.out_lup_grade == -2);
    /* 확장은 Count>0이라 깊이와 무관하게 계산된다 (원본 주석) */
    TR_CHECK(s.out_lup.ext_100 == 115.0);
    TR_CHECK(s.out_lup.ext_1382 == 100.0 + 15.0 * 1.382);
    TR_CHECK(s.out_lup.ext_1618 == 100.0 + 15.0 * 1.618);
    TR_CHECK(s.out_lup.ext_200 == 130.0);
    TR_CHECK(s.out_lup.ext_2382 == 100.0 + 15.0 * 2.382);
    /* 지난하락 메모리 + 구조약화 (500유지 실패) */
    TR_CHECK(s.out_ldn.high == 113.0 && s.out_ldn.low == 92.0 && s.out_ldn.count == 3.0);
    TR_CHECK(s.out_ldn.lvl_382 == 92.0 + 21.0 * 0.382);
    TR_CHECK(s.out_ldn.weak == 1);
    TR_CHECK(s.out_ldn_grade == 1); /* 유지 없음·구조약화 → 한 단계 다운그레이드 */
    TR_CHECK(s.out_time_ratio == 2.0 / 3.0); /* 상승Count 2 / 지난하락Count 3 */
}

/* 시나리오 C: 얕은 되돌림 → 지난상승 유지 3개 모두 참 → 실효등급 0 (얕은목표) */
static void test_shallow_retrace_grade0(void) {
    tr_fxsw_t s;
    tr_fxsw_init(&s);
    feed(&s, true, 1.0, 100.0, 95.0, true);
    feed(&s, false, 1.0, 115.0, 100.0, true);
    feed(&s, false, 1.0, 120.0, 108.0, true);  /* 상승 (120,100,2) 확정 */
    feed(&s, false, -1.0, 119.0, 116.0, true);
    feed(&s, false, -1.0, 118.0, 115.0, true); /* 지난상승 (120,100,2) 메모리 확정 */
    feed(&s, false, 1.0, 117.0, 114.0, true);
    feed(&s, false, 1.0, 118.0, 114.0, true);  /* 상승 확정 → 지난하락 (118,115,2) 확정 */

    /* 되돌림 저가 115 > 618선(112.36) → 618/500/382 유지 모두 참 */
    TR_CHECK(s.out_lup.keep_618 == 1 && s.out_lup.keep_500 == 1 && s.out_lup.keep_382 == 1);
    TR_CHECK(s.out_lup.broken == 0);
    TR_CHECK(s.out_lup_grade == 0); /* 얕은목표 */
    TR_CHECK(s.out_lup.ext_100 == 120.0);
    TR_CHECK(s.out_lup.ext_1382 == 100.0 + 20.0 * 1.382);
    TR_CHECK(s.out_lup.ext_1618 == 100.0 + 20.0 * 1.618);
    TR_CHECK(s.out_lup.ext_200 == 140.0);
    TR_CHECK(s.out_lup.ext_2382 == 100.0 + 20.0 * 2.382);
    TR_CHECK(s.out_ldn.weak == 0); /* 500유지가 섰으므로 구조약화 아님 */
    TR_CHECK(s.out_ldn_grade == 2);
}

/* 시나리오 D: 지난하락 중간목표 — 반등이 38.2~61.8% 구간(618유지 참, 382유지 거짓) */
static void test_mid_band_rebound_grade1(void) {
    tr_fxsw_t s;
    tr_fxsw_init(&s);
    feed(&s, true, -1.0, 100.0, 95.0, true);
    feed(&s, false, -1.0, 105.0, 98.0, true);
    feed(&s, false, -1.0, 104.0, 90.0, true);  /* 하락 (105,90,2) 확정 */
    feed(&s, false, 1.0, 98.0, 93.0, true);
    feed(&s, false, 1.0, 99.0, 94.0, true);    /* 지난하락 (105,90,2) 메모리 확정 */
    feed(&s, false, -1.0, 98.0, 96.0, true);
    feed(&s, false, -1.0, 97.0, 95.0, true);   /* 하락 확정 → 지난상승 (99,93,2) 확정 */

    /* 반등 고가 99: 618선(99.27) 아래(유지) · 500선(97.5) 위 → 618유지만 참 */
    TR_CHECK(s.out_ldn.keep_618 == 1 && s.out_ldn.keep_500 == 0 && s.out_ldn.keep_382 == 0);
    TR_CHECK(s.out_ldn.broken == 0);
    TR_CHECK(s.out_ldn_grade == 1); /* 중간목표 */
    TR_CHECK(s.out_ldn.ext_100 == 90.0);
    TR_CHECK(s.out_ldn.ext_1618 == 105.0 - 15.0 * 1.618);
    TR_CHECK(s.out_ldn.ext_2382 == 105.0 - 15.0 * 2.382);
    TR_CHECK(s.out_lup.weak == 0);
    TR_CHECK(s.out_lup_grade == 2);
}

/* 시나리오 H: 지난상승 중간목표 — 되돌림이 38.2~61.8% 구간(618유지 거짓, 382유지 참) */
static void test_mid_band_retrace_grade1(void) {
    tr_fxsw_t s;
    tr_fxsw_init(&s);
    feed(&s, true, 1.0, 100.0, 95.0, true);
    feed(&s, false, 1.0, 118.0, 100.0, true);
    feed(&s, false, 1.0, 120.0, 110.0, true);  /* 상승 (120,100,2) 확정 */
    feed(&s, false, -1.0, 119.0, 112.0, true);
    feed(&s, false, -1.0, 118.0, 111.0, true); /* 지난상승 (120,100,2) 메모리 확정 */
    feed(&s, false, 1.0, 117.0, 113.0, true);
    feed(&s, false, 1.0, 118.0, 114.0, true);  /* 상승 확정 → 지난하락 (118,111,2) 확정 */

    /* 되돌림 저가 111: 618선(112.36) 아래 · 382선(107.64) 위 → 382유지 참 */
    TR_CHECK(s.out_lup.keep_618 == 0 && s.out_lup.keep_382 == 1);
    TR_CHECK(s.out_lup.broken == 0);
    TR_CHECK(s.out_lup_grade == 1); /* 중간목표 */
    TR_CHECK(s.out_ldn.weak == 0); /* 500유지가 섰으므로 구조약화 아님 */
}

/* 시나리오 E: 대기 취소 — 현재 구간에 대기를 흡수(merge) */
static void test_wait_cancel_merge(void) {
    tr_fxsw_t s;
    tr_fxsw_init(&s);
    feed(&s, true, 1.0, 100.0, 95.0, true);
    feed(&s, false, 1.0, 110.0, 100.0, true);
    feed(&s, false, 1.0, 112.0, 105.0, true);  /* 상승 (112,100,2) 확정 */
    feed(&s, false, -1.0, 111.0, 104.0, true); /* 하락 대기 */
    TR_CHECK(s.st.wait_dir == -1);
    feed(&s, false, 1.0, 113.0, 106.0, true);  /* 방향 반전 → 대기 흡수 */
    TR_CHECK(s.st.wait_dir == 0);
    TR_CHECK(s.st.up_high == 113.0); /* Max(112, Max(111,113)) */
    TR_CHECK(s.st.up_low == 100.0);  /* Min(100, Min(104,106)) */
    TR_CHECK(s.st.up_count == 4.0);  /* 2 + 대기Count 1 + 1 */
    TR_CHECK(s.out_leg_dir == 1);
}

/* 시나리오 F: 동일 봉 재평가 — 직전 봉 말 상태에서 1회분만 반영 (원본 101~155줄) */
static void test_same_bar_restore(void) {
    tr_fxsw_t s;
    tr_fxsw_init(&s);
    feed(&s, true, 1.0, 110.0, 100.0, true);
    feed(&s, false, 1.0, 110.0, 100.0, true);
    feed(&s, false, 1.0, 112.0, 105.0, true);
    feed(&s, false, 1.0, 115.0, 108.0, true);
    feed(&s, false, -1.0, 113.0, 102.0, true);
    feed(&s, false, -1.0, 111.0, 95.0, true);  /* 하락 확정 (113,95,2) */
    TR_CHECK(s.st.lup.count == 3.0);
    TR_CHECK(s.st.dn_low == 95.0);

    /* 같은 봉 재평가 (다른 H/L): 지난상승 Count가 중복 누적되지 않아야 한다 */
    feed(&s, false, -1.0, 112.0, 96.0, false);
    TR_CHECK(s.st.lup.count == 3.0);       /* 6이 아니다 — 복원 후 1회분만 반영 */
    TR_CHECK(s.st.up_high == 115.0);       /* 구간도 중복 확장되지 않는다 */
    TR_CHECK(s.st.dn_low == 96.0);         /* 이번 평가의 H/L만 반영 */

    /* 같은 봉 재평가 (원래 H/L): 첫 평가와 같은 결과로 돌아와야 한다 */
    feed(&s, false, -1.0, 111.0, 95.0, false);
    TR_CHECK(s.st.lup.count == 3.0);
    TR_CHECK(s.st.dn_low == 95.0);
    TR_CHECK(s.out_lup.count == 3.0 && s.out_lup.low == 100.0); /* 메모리도 1회분만 확정 */
}

/* 시나리오 G: 세션 리셋 — 전 상태 소거, 리셋 봉은 카운팅 없음, 이후 정상 재개 */
static void test_session_reset(void) {
    tr_fxsw_t s;
    tr_fxsw_init(&s);
    feed(&s, true, 1.0, 110.0, 100.0, true);
    feed(&s, false, 1.0, 110.0, 100.0, true);
    feed(&s, false, 1.0, 112.0, 105.0, true);
    TR_CHECK(s.st.up_count == 2.0);

    feed(&s, true, -1.0, 100.0, 90.0, true); /* 세션 리셋 */
    TR_CHECK(s.st.up_count == 0.0 && s.st.leg_dir == 0 && s.st.wait_dir == 0);
    TR_CHECK(s.out_lup.high == 0.0 && s.out_ldn.high == 0.0);
    TR_CHECK(s.out_lup_grade == -1 && s.out_ldn_grade == -1); /* 최고가==0 → 데이터 없음 */
    TR_CHECK(s.out_leg_dir == 0);
    TR_CHECK(s.out_time_ratio == 0.0);

    /* 리셋으로 직전미래방향도 0이라 다음 봉 전환 감지는 0 기준으로 재개된다 */
    feed(&s, false, 1.0, 100.0, 95.0, true);
    TR_CHECK(s.st.wait_dir == 1); /* 양수전환 → 대기 시작 */
}

int main(void) {
    test_up_confirm_and_extension();
    test_deep_retrace_broken_ext();
    test_shallow_retrace_grade0();
    test_mid_band_rebound_grade1();
    test_mid_band_retrace_grade1();
    test_wait_cancel_merge();
    test_same_bar_restore();
    test_session_reset();
    TR_TEST_SUMMARY();
}
