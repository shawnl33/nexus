/* LS 차트 파서 단위 테스트: 실제 API 응답 캡처 기반 (네트워크 불필요) */

#include "test_util.h"

#include <string.h>

#include "adapters/ls/ls_chart.h"
#include "adapters/ls/ls_http.h"
#include "core/model/civil_time.h"

#define CAP 16
static tr_candle_t g_bars[CAP];

/* 2026-09-28 실제 t8412 응답 캡처 (005930) */
static const char *STOCK_RESP =
    "{\"t8412OutBlock\":{\"shcode\":\"005930\",\"cts_date\":\"20260928\",\"cts_time\":\"124400\","
    "\"s_time\":\"090000\",\"e_time\":\"153000\",\"rec_count\":3},"
    "\"t8412OutBlock1\":["
    "{\"date\":\"20260928\",\"time\":\"124500\",\"open\":271750,\"high\":272000,\"low\":271500,\"close\":272000,\"jdiff_vol\":19563,\"value\":5316,\"jongchk\":0,\"rate\":\"0\",\"sign\":\"5\"},"
    "{\"date\":\"20260928\",\"time\":\"124600\",\"open\":271750,\"high\":272500,\"low\":271500,\"close\":272250,\"jdiff_vol\":72715,\"value\":19777,\"jongchk\":0,\"rate\":\"0\",\"sign\":\"5\"},"
    "{\"date\":\"20260928\",\"time\":\"124700\",\"open\":272250,\"high\":272500,\"low\":272000,\"close\":272250,\"jdiff_vol\":4269,\"value\":1162,\"jongchk\":0,\"rate\":\"0\",\"sign\":\"5\"}],"
    "\"rsp_cd\":\"00000\",\"rsp_msg\":\"정상적으로 조회가 완료되었습니다.\"}";

/* 실제 t8465 응답 캡처 (A016C000, 가격이 문자열) */
static const char *FUT_RESP =
    "{\"t8465OutBlock\":{\"shcode\":\"A016C000\",\"cts_date\":\"20260928\",\"cts_time\":\"124600\","
    "\"s_time\":\"084500\",\"e_time\":\"154500\",\"rec_count\":2},"
    "\"t8465OutBlock1\":["
    "{\"date\":\"20260928\",\"time\":\"124700\",\"open\":\"1093.90\",\"high\":\"1094.35\",\"low\":\"1093.65\",\"close\":\"1094.00\",\"jdiff_vol\":137,\"value\":37471,\"openyak\":136315},"
    "{\"date\":\"20260928\",\"time\":\"124800\",\"open\":\"1094.05\",\"high\":\"1095.20\",\"low\":\"1093.75\",\"close\":\"1095.05\",\"jdiff_vol\":279,\"value\":76360,\"openyak\":136295}],"
    "\"rsp_cd\":\"00000\",\"rsp_msg\":\"정상적으로 조회가 완료되었습니다.\"}";

static const char *EMPTY_RESP = "{\"rsp_cd\":\"00000\",\"rsp_msg\":\"해당자료가 없습니다.\"}";

static void test_stock_parse(void) {
    ls_chart_page_t page;
    char err[128] = {0};
    int rc = ls_chart_parse_page(STOCK_RESP, strlen(STOCK_RESP), LS_CHART_STOCK_MIN,
                                 1, 7, 60, g_bars, CAP, &page, err, sizeof(err));
    TR_CHECK(rc == LS_HTTP_OK);
    TR_CHECK(page.count == 3);
    TR_CHECK(page.has_more);
    TR_CHECK(strcmp(page.cts_date, "20260928") == 0);
    TR_CHECK(strcmp(page.cts_time, "124400") == 0);
    TR_CHECK(page.session_open_min == 540 && page.session_close_min == 930);

    /* 오름차순 정렬 확인 */
    TR_CHECK(g_bars[0].open_time_us < g_bars[1].open_time_us);
    TR_CHECK(g_bars[1].open_time_us < g_bars[2].open_time_us);

    /* 2026-09-28 12:45 KST = 03:45 UTC */
    tr_civil_t c = {2026, 9, 28, 3, 45, 0};
    tr_time_us_t expect;
    tr_time_us_from_civil(&c, 0, &expect);
    TR_CHECK(g_bars[0].open_time_us == expect);
    TR_CHECK(g_bars[0].open == 27175000); /* ×100 스케일 */
    TR_CHECK(g_bars[0].close == 27200000);
    TR_CHECK(g_bars[0].volume == 19563);
    TR_CHECK(g_bars[0].state == TR_CANDLE_CLOSED);
    TR_CHECK(g_bars[0].source_id == 7);
}

static void test_futures_string_prices(void) {
    ls_chart_page_t page;
    char err[128] = {0};
    int rc = ls_chart_parse_page(FUT_RESP, strlen(FUT_RESP), LS_CHART_FUT_MIN,
                                 2, 7, 60, g_bars, CAP, &page, err, sizeof(err));
    TR_CHECK(rc == LS_HTTP_OK);
    TR_CHECK(page.count == 2);
    /* 선물 가격 문자열 "1093.90" → 109390 (×100) */
    TR_CHECK(g_bars[0].open == 109390);
    TR_CHECK(g_bars[0].high == 109435);
    TR_CHECK(g_bars[0].low == 109365);
    TR_CHECK(g_bars[0].close == 109400);
    TR_CHECK(page.session_open_min == 8 * 60 + 45);
    TR_CHECK(page.session_close_min == 15 * 60 + 45);
}

static void test_empty_is_not_error(void) {
    ls_chart_page_t page;
    char err[128] = {0};
    int rc = ls_chart_parse_page(EMPTY_RESP, strlen(EMPTY_RESP), LS_CHART_STOCK_MIN,
                                 1, 7, 60, g_bars, CAP, &page, err, sizeof(err));
    TR_CHECK(rc == LS_CHART_EMPTY);
    TR_CHECK(page.count == 0);
}

static void test_duplicate_detected(void) {
    const char *dup =
        "{\"t8412OutBlock\":{\"cts_date\":\"\",\"cts_time\":\"\",\"s_time\":\"090000\",\"e_time\":\"153000\"},"
        "\"t8412OutBlock1\":["
        "{\"date\":\"20260928\",\"time\":\"124500\",\"open\":1,\"high\":1,\"low\":1,\"close\":1,\"jdiff_vol\":1},"
        "{\"date\":\"20260928\",\"time\":\"124500\",\"open\":1,\"high\":1,\"low\":1,\"close\":1,\"jdiff_vol\":1}]}";
    ls_chart_page_t page;
    char err[128] = {0};
    int rc = ls_chart_parse_page(dup, strlen(dup), LS_CHART_STOCK_MIN,
                                 1, 7, 60, g_bars, CAP, &page, err, sizeof(err));
    TR_CHECK(rc == LS_HTTP_PARSE_ERR);
}

/* 실제 t8461 응답 형식 (2026-09-28 캡처 기반): 내림차순, chetime만 있고 날짜 없음 */
static const char *NIGHT_RESP =
    "{\"t8461OutBlock1\":["
    "{\"chetime\":\"213800\",\"price\":\"1089.70\",\"open\":\"1089.90\",\"high\":\"1090.10\",\"low\":\"1089.70\",\"cvolume\":10},"
    "{\"chetime\":\"180500\",\"price\":\"1087.15\",\"open\":\"1087.00\",\"high\":\"1087.30\",\"low\":\"1086.90\",\"cvolume\":32},"
    "{\"chetime\":\"051000\",\"price\":\"1090.00\",\"open\":\"1090.10\",\"high\":\"1090.20\",\"low\":\"1089.90\",\"cvolume\":5},"
    "{\"chetime\":\"003000\",\"price\":\"1091.00\",\"open\":\"1090.90\",\"high\":\"1091.10\",\"low\":\"1090.80\",\"cvolume\":7},"
    "{\"chetime\":\"235000\",\"price\":\"1092.00\",\"open\":\"1091.90\",\"high\":\"1092.10\",\"low\":\"1091.80\",\"cvolume\":12},"
    "{\"chetime\":\"181000\",\"price\":\"1093.00\",\"open\":\"1092.90\",\"high\":\"1093.10\",\"low\":\"1092.80\",\"cvolume\":20}],"
    "\"rsp_cd\":\"00000\",\"rsp_msg\":\"정상적으로 조회가 완료되었습니다.\"}";

static int64_t kst_us(int y, unsigned mo, unsigned d, unsigned h, unsigned mi, unsigned s) {
    tr_civil_t c = {y, mo, d, h, mi, s};
    tr_time_us_t t = 0;
    tr_time_us_from_civil(&c, 540, &t);
    return t;
}

static void test_fut_night_session_day(void) {
    /* 월 21:40 → 월 세션 / 화 03:00 → 월 세션 / 월 10:00(주간) → 금 세션 / 토 10:00 → 금 세션 */
    TR_CHECK(ls_fut_night_session_day(kst_us(2026, 9, 28, 21, 40, 0)) == tr_days_from_civil(2026, 9, 28));
    TR_CHECK(ls_fut_night_session_day(kst_us(2026, 9, 29, 3, 0, 0)) == tr_days_from_civil(2026, 9, 28));
    TR_CHECK(ls_fut_night_session_day(kst_us(2026, 9, 28, 10, 0, 0)) == tr_days_from_civil(2026, 9, 25));
    TR_CHECK(ls_fut_night_session_day(kst_us(2026, 9, 26, 10, 0, 0)) == tr_days_from_civil(2026, 9, 25));
}

static void test_fut_night_parse(void) {
    char err[128] = {0};
    /* 09-24/25는 추석 연휴(무세션)라는 시나리오: 거래일은 09-23, 09-28 뿐 */
    int64_t tdays[] = {tr_days_from_civil(2026, 9, 23), tr_days_from_civil(2026, 9, 28)};
    int n = ls_chart_parse_fut_night(NIGHT_RESP, strlen(NIGHT_RESP),
                                     tdays, 2,
                                     tr_days_from_civil(2026, 9, 28), /* 앵커(최신 세션) */
                                     9, 7, g_bars, CAP, err, sizeof(err));
    TR_CHECK(n == 6);
    /* 오름차순: 09-23 18:10, 09-23 23:50, 09-24 00:30, 09-24 05:10, 09-28 18:05, 09-28 21:38
     * (연휴가 껴 있으면 09-23 세션의 아침 봉 날짜는 09-24다 — 평일 추정이면 09-26으로 오판) */
    TR_CHECK(g_bars[0].open_time_us == kst_us(2026, 9, 23, 18, 10, 0));
    TR_CHECK(g_bars[1].open_time_us == kst_us(2026, 9, 23, 23, 50, 0));
    TR_CHECK(g_bars[2].open_time_us == kst_us(2026, 9, 24, 0, 30, 0));
    TR_CHECK(g_bars[3].open_time_us == kst_us(2026, 9, 24, 5, 10, 0));
    TR_CHECK(g_bars[4].open_time_us == kst_us(2026, 9, 28, 18, 5, 0));
    TR_CHECK(g_bars[5].open_time_us == kst_us(2026, 9, 28, 21, 38, 0));
    /* 가격(price=종가) ×100 스케일과 봉 속성 */
    TR_CHECK(g_bars[5].close == 108970);
    TR_CHECK(g_bars[5].open == 108990 && g_bars[5].high == 109010 && g_bars[5].low == 108970);
    TR_CHECK(g_bars[5].volume == 10);
    TR_CHECK(g_bars[5].state == TR_CANDLE_CLOSED && g_bars[5].timeframe_sec == 60);
}

/* ---------- 일봉 (t8466 2026-09-29 / t8410 2026-09-30 실측 완료 — 파서 단위 테스트) ---------- */

/* t8410 명세 형식: 주식 가격은 Number. 행 순서는 명세에 없어 최신→과거로 섞어 둔다 */
static const char *STOCK_DAY_RESP =
    "{\"t8410OutBlock\":{\"shcode\":\"005930\",\"cts_date\":\"20260910\",\"s_time\":\"090000\",\"e_time\":\"153000\",\"rec_count\":3},"
    "\"t8410OutBlock1\":["
    "{\"date\":\"20260928\",\"open\":271000,\"high\":272000,\"low\":270500,\"close\":271800,\"jdiff_vol\":12345678},"
    "{\"date\":\"20260925\",\"open\":270000,\"high\":271500,\"low\":269500,\"close\":271000,\"jdiff_vol\":11345678},"
    "{\"date\":\"20260924\",\"open\":269000,\"high\":270500,\"low\":268500,\"close\":270000,\"jdiff_vol\":10345678}],"
    "\"rsp_cd\":\"00000\",\"rsp_msg\":\"정상적으로 조회가 완료되었습니다.\"}";

/* t8466 실측 형식 (2026-09-29): 선물 가격은 소수 문자열 (분봉 t8465와 같은 규칙으로 확인),
 * 행은 오름차순(과거→최신)이며 당일 진행 중 일봉도 포함된다 */
static const char *FUT_DAY_RESP =
    "{\"t8466OutBlock\":{\"shcode\":\"A016C000\",\"cts_date\":\"20260910\",\"s_time\":\"084500\",\"e_time\":\"154500\",\"rec_count\":2},"
    "\"t8466OutBlock1\":["
    "{\"date\":\"20260925\",\"open\":\"1092.00\",\"high\":\"1093.00\",\"low\":\"1091.50\",\"close\":\"1092.50\",\"jdiff_vol\":120000,\"openyak\":136000},"
    "{\"date\":\"20260928\",\"open\":\"1093.90\",\"high\":\"1094.35\",\"low\":\"1093.65\",\"close\":\"1094.00\",\"jdiff_vol\":137000,\"openyak\":136315}],"
    "\"rsp_cd\":\"00000\",\"rsp_msg\":\"정상적으로 조회가 완료되었습니다.\"}";

static void test_stock_daily_parse(void) {
    ls_daily_bar_t bars[8];
    size_t n = 0;
    char err[128] = {0};
    int rc = ls_chart_parse_daily(STOCK_DAY_RESP, strlen(STOCK_DAY_RESP), LS_CHART_STOCK_DAY,
                                  bars, 8, &n, err, sizeof(err));
    TR_CHECK(rc == LS_HTTP_OK);
    TR_CHECK(n == 3);
    /* 입력은 최신→과거였지만 거래일 오름차순으로 정렬된다 */
    TR_CHECK(bars[0].day == tr_days_from_civil(2026, 9, 24));
    TR_CHECK(bars[1].day == tr_days_from_civil(2026, 9, 25));
    TR_CHECK(bars[2].day == tr_days_from_civil(2026, 9, 28));
    /* 주식 정수 가격 ×100 스케일 (분봉과 동일 규칙) */
    TR_CHECK(bars[0].high == 27050000);
    TR_CHECK(bars[0].low == 26850000);
    TR_CHECK(bars[2].high == 27200000);
    TR_CHECK(bars[2].low == 27050000);
}

static void test_fut_daily_parse(void) {
    ls_daily_bar_t bars[8];
    size_t n = 0;
    char err[128] = {0};
    int rc = ls_chart_parse_daily(FUT_DAY_RESP, strlen(FUT_DAY_RESP), LS_CHART_FUT_DAY,
                                  bars, 8, &n, err, sizeof(err));
    TR_CHECK(rc == LS_HTTP_OK);
    TR_CHECK(n == 2);
    TR_CHECK(bars[0].day == tr_days_from_civil(2026, 9, 25));
    TR_CHECK(bars[1].day == tr_days_from_civil(2026, 9, 28));
    /* 선물 문자열 가격 "1093.00" → 109300 (×100) */
    TR_CHECK(bars[0].high == 109300);
    TR_CHECK(bars[0].low == 109150);
    TR_CHECK(bars[1].high == 109435);
    TR_CHECK(bars[1].low == 109365);
}

static void test_daily_empty_is_not_error(void) {
    ls_daily_bar_t bars[8];
    size_t n = 99;
    char err[128] = {0};
    int rc = ls_chart_parse_daily(EMPTY_RESP, strlen(EMPTY_RESP), LS_CHART_STOCK_DAY,
                                  bars, 8, &n, err, sizeof(err));
    TR_CHECK(rc == LS_CHART_EMPTY);
    TR_CHECK(n == 0);
}

static void test_daily_duplicate_detected(void) {
    const char *dup =
        "{\"t8410OutBlock\":{\"cts_date\":\" \"},"
        "\"t8410OutBlock1\":["
        "{\"date\":\"20260925\",\"open\":100,\"high\":110,\"low\":90,\"close\":105},"
        "{\"date\":\"20260925\",\"open\":100,\"high\":111,\"low\":91,\"close\":106}]}";
    ls_daily_bar_t bars[8];
    size_t n = 0;
    char err[128] = {0};
    int rc = ls_chart_parse_daily(dup, strlen(dup), LS_CHART_STOCK_DAY,
                                  bars, 8, &n, err, sizeof(err));
    TR_CHECK(rc == LS_HTTP_PARSE_ERR);
}

static void test_daily_kind_guard(void) {
    /* 분봉/일봉 파서는 서로의 kind를 거부한다 */
    ls_daily_bar_t bars[8];
    size_t n = 0;
    char err[128] = {0};
    TR_CHECK(ls_chart_parse_daily(FUT_DAY_RESP, strlen(FUT_DAY_RESP), LS_CHART_FUT_MIN,
                                  bars, 8, &n, err, sizeof(err)) == LS_HTTP_PARSE_ERR);
    ls_chart_page_t page;
    TR_CHECK(ls_chart_parse_page(FUT_DAY_RESP, strlen(FUT_DAY_RESP), LS_CHART_FUT_DAY,
                                 2, 7, 60, g_bars, CAP, &page, err, sizeof(err)) == LS_HTTP_PARSE_ERR);
}

/* 재시도 분류 고정: 전송/HTTP 상태 오류만 재시도한다 (백필 페이지 유실 방지 정책) */
static void test_retryable_classification(void) {
    TR_CHECK(ls_chart_retryable(LS_HTTP_TRANSPORT_ERR)); /* 타임아웃 등 서버 미응답 */
    TR_CHECK(ls_chart_retryable(LS_HTTP_STATUS_ERR));    /* HTTP 5xx 등 */
    TR_CHECK(!ls_chart_retryable(LS_HTTP_PARSE_ERR));    /* 서버가 답함 — 재시도 무의미 */
    TR_CHECK(!ls_chart_retryable(LS_HTTP_API_ERR));
    TR_CHECK(!ls_chart_retryable(LS_CHART_EMPTY));       /* 데이터 없음은 오류가 아니다 */
    TR_CHECK(!ls_chart_retryable(LS_HTTP_OK));
}

int main(void) {
    test_stock_parse();
    test_futures_string_prices();
    test_empty_is_not_error();
    test_duplicate_detected();
    test_fut_night_session_day();
    test_fut_night_parse();
    test_stock_daily_parse();
    test_fut_daily_parse();
    test_daily_empty_is_not_error();
    test_daily_duplicate_detected();
    test_daily_kind_guard();
    test_retryable_classification();
    TR_TEST_SUMMARY();
}
