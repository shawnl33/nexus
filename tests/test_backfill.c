/* 백필 미래 스텁 봉 필터(tr_backfill_keep_bar) 단위 테스트.
 *
 * LS t8412(주식 분봉)는 프리마켓(08:00~09:00) 중 응답 마지막에 미래 시각의 스텁 봉
 * (당일 09:01, 거래량 0)을 싣는다 (2026-10-01 08:12/08:24 실측). 백필이 이를 CLOSED
 * 봉으로 주입하면 시리즈 꼬리가 한 칸 많아져 차트 시간축이 어긋난다.
 * 필터 규칙은 open > now + 1봉(timeframe)인 봉만 버리는 것 — 현재 진행 중 분의 봉
 * (open ≤ now)은 절대 걸리지 않아야 한다. */

#include "test_util.h"

#include <stdbool.h>
#include <stdint.h>

#include "adapters/ls/ls_chart.h"
#include "adapters/ls/ls_http.h"
#include "adapters/ls/ls_master.h"
#include "core/market/session.h"
#include "core/model/civil_time.h"

/* src/app/main.c 정의 — 테스트에서는 extern 선언으로 링크한다. */
bool tr_backfill_keep_bar(tr_time_us_t open_us, tr_time_us_t now_us, uint32_t timeframe_sec);
bool tr_backfill_marks_rt_only(int kind, int chart_rc);
bool tr_backfill_page_extends(tr_time_us_t page_oldest, bool have_bars, tr_time_us_t held_oldest);
tr_time_us_t tr_backfill_cover_until(const tr_time_us_t *opens, size_t n);
bool tr_nh_catchup_due(tr_time_us_t last_us, tr_time_us_t now_us);
tr_session_policy_t tr_ks_fut_session(void);

#define TF1M_SEC 60u
#define TF1M_US ((tr_time_us_t)TF1M_SEC * TR_US_PER_SEC)

static tr_time_us_t kst(int y, unsigned mo, unsigned d, unsigned h, unsigned mi) {
    tr_civil_t c = {y, mo, d, h, mi, 0};
    tr_time_us_t t = -1;
    TR_CHECK(tr_time_us_from_civil(&c, 540, &t));
    return t;
}

static void test_nh_catchup_due_after_two_minutes(void) {
    tr_time_us_t t = kst(2026, 10, 14, 1, 57);
    TR_CHECK(!tr_nh_catchup_due(0, t));
    TR_CHECK(!tr_nh_catchup_due(t, t + 120 * TR_US_PER_SEC));
    TR_CHECK(tr_nh_catchup_due(t, t + 121 * TR_US_PER_SEC));
}

static void test_cover_until_reaches_past_the_long_hole(void) {
    tr_time_us_t opens[5];
    opens[0] = kst(2026, 10, 14, 1, 56);
    opens[1] = kst(2026, 10, 14, 1, 57);
    opens[2] = kst(2026, 10, 14, 13, 12);
    opens[3] = kst(2026, 10, 14, 13, 13);
    opens[4] = kst(2026, 10, 14, 13, 15);
    TR_CHECK(tr_backfill_cover_until(opens, 5) == opens[1]);
}

static void test_cover_until_skips_halt_and_weekend(void) {
    tr_time_us_t halt[2];
    halt[0] = kst(2026, 10, 15, 5, 59);
    halt[1] = kst(2026, 10, 15, 7, 0);
    TR_CHECK(tr_backfill_cover_until(halt, 2) == 0);
    tr_time_us_t weekend[2];
    weekend[0] = kst(2026, 10, 9, 6, 0);
    weekend[1] = kst(2026, 10, 12, 7, 0);
    TR_CHECK(tr_backfill_cover_until(weekend, 2) == 0);
    TR_CHECK(tr_backfill_cover_until(halt, 1) == 0);
}

static tr_time_us_t utc(int y, unsigned mo, unsigned d, unsigned h, unsigned mi, unsigned s) {
    tr_civil_t c = {y, mo, d, h, mi, s};
    tr_time_us_t t = -1;
    TR_CHECK(tr_time_us_from_civil(&c, 0, &t));
    return t;
}

/* 2026-10-01 08:12:30 KST = 2026-09-30 23:12:30 UTC (실측 시점 재현) */
#define NOW (utc(2026, 9, 30, 23, 12, 30))

static void test_current_minute_bar_kept(void) {
    /* 현재 진행 중 분의 봉(open = now의 분 내림 ≤ now) — 절대 버리면 안 된다 */
    TR_CHECK(tr_backfill_keep_bar(utc(2026, 9, 30, 23, 12, 0), NOW, TF1M_SEC));
    /* open == now 경계도 keep */
    TR_CHECK(tr_backfill_keep_bar(NOW, NOW, TF1M_SEC));
}

static void test_past_bar_kept(void) {
    TR_CHECK(tr_backfill_keep_bar(utc(2026, 9, 30, 20, 0, 0), NOW, TF1M_SEC));
}

static void test_next_session_stub_dropped(void) {
    /* 실측 스텁: 08:12 시점 응답 꼬리의 당일 09:01 봉 (= 10-01 00:01 UTC) */
    TR_CHECK(!tr_backfill_keep_bar(utc(2026, 10, 1, 0, 1, 0), NOW, TF1M_SEC));
}

static void test_boundary_one_bar_slack(void) {
    /* 경계는 open > now + 1봉: 정확히 now+1봉이면 keep, 1µs라도 넘으면 drop */
    TR_CHECK(tr_backfill_keep_bar(NOW + TF1M_US, NOW, TF1M_SEC));
    TR_CHECK(!tr_backfill_keep_bar(NOW + TF1M_US + 1, NOW, TF1M_SEC));
}

static void test_timeframe_parameterized(void) {
    const uint32_t tf = 300; /* 5분 timeframe에도 같은 규칙 */
    const tr_time_us_t tf_us = (tr_time_us_t)tf * TR_US_PER_SEC;
    TR_CHECK(tr_backfill_keep_bar(NOW + tf_us, NOW, tf));
    TR_CHECK(!tr_backfill_keep_bar(NOW + tf_us + 1, NOW, tf));
}

/* RT-only 판정: 해외선물 백필의 LS_CHART_EMPTY("해당자료가 없습니다" — 계정 권한 차단,
 * 2026-10-01 ESZ26 실측)만 RT-only다. 국내의 빈 응답(주말·거래정지)이나 해외의
 * 전송 오류는 RT-only가 아니다 (60초 재백필 대상 유지) */
/* 같은 페이지 반복은 붙이지 않는다. 첫 페이지는 항상 받고, 이미 가진 가장 오래된
 * 봉보다 과거인 페이지만 이어 붙인다. */
static void test_page_extends_only_when_older(void) {
    const tr_time_us_t older = utc(2026, 10, 1, 4, 2, 0);
    const tr_time_us_t held = utc(2026, 10, 2, 2, 2, 0);
    const tr_time_us_t newer = utc(2026, 10, 2, 10, 0, 0);
    TR_CHECK(tr_backfill_page_extends(held, false, 0));
    TR_CHECK(tr_backfill_page_extends(older, true, held));
    TR_CHECK(!tr_backfill_page_extends(held, true, held));
    TR_CHECK(!tr_backfill_page_extends(newer, true, held));
}

static void test_ks_fut_session_is_day_only(void) {
    tr_session_policy_t s = tr_ks_fut_session();
    TR_CHECK(s.open_min == 8 * 60 + 45);
    TR_CHECK(s.close_min == 15 * 60 + 46);
    TR_CHECK(tr_session_span(&s, kst(2026, 10, 16, 9, 0), 0, 0));
    TR_CHECK(tr_session_span(&s, kst(2026, 10, 16, 15, 44), 0, 0));
    TR_CHECK(tr_session_span(&s, kst(2026, 10, 16, 15, 45), 0, 0));
    TR_CHECK(!tr_session_span(&s, kst(2026, 10, 16, 15, 46), 0, 0));
    TR_CHECK(!tr_session_span(&s, kst(2026, 10, 16, 18, 1), 0, 0));
    TR_CHECK(!tr_session_span(&s, kst(2026, 10, 17, 3, 0), 0, 0));
}

static void test_rt_only_classification(void) {
    TR_CHECK(tr_backfill_marks_rt_only(LS_MARKET_OVS_FUT, LS_CHART_EMPTY));
    TR_CHECK(!tr_backfill_marks_rt_only(LS_MARKET_OVS_FUT, LS_HTTP_OK));
    TR_CHECK(!tr_backfill_marks_rt_only(LS_MARKET_OVS_FUT, LS_HTTP_TRANSPORT_ERR));
    TR_CHECK(!tr_backfill_marks_rt_only(LS_MARKET_KP200_FUT, LS_CHART_EMPTY));
    TR_CHECK(!tr_backfill_marks_rt_only(LS_MARKET_KOSPI, LS_CHART_EMPTY));
    TR_CHECK(!tr_backfill_marks_rt_only(LS_MARKET_KOSDAQ, LS_CHART_EMPTY));
}

int main(void) {
    test_current_minute_bar_kept();
    test_past_bar_kept();
    test_next_session_stub_dropped();
    test_boundary_one_bar_slack();
    test_timeframe_parameterized();
    test_page_extends_only_when_older();
    test_ks_fut_session_is_day_only();
    test_rt_only_classification();
    test_nh_catchup_due_after_two_minutes();
    test_cover_until_reaches_past_the_long_hole();
    test_cover_until_skips_halt_and_weekend();
    TR_TEST_SUMMARY();
}
