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

#include "core/model/civil_time.h"

/* src/app/main.c 정의 — 테스트에서는 extern 선언으로 링크한다. */
bool tr_backfill_keep_bar(tr_time_us_t open_us, tr_time_us_t now_us, uint32_t timeframe_sec);

#define TF1M_SEC 60u
#define TF1M_US ((tr_time_us_t)TF1M_SEC * TR_US_PER_SEC)

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

int main(void) {
    test_current_minute_bar_kept();
    test_past_bar_kept();
    test_next_session_stub_dropped();
    test_boundary_one_bar_slack();
    test_timeframe_parameterized();
    TR_TEST_SUMMARY();
}
