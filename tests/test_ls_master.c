/* LS 종목 마스터 파서 테스트: 실제 응답 캡처 기반 (네트워크 불필요) */

#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#include "adapters/ls/ls_master.h"
#include "adapters/ls/ls_ovsfut.h"

#define CAP 8
static ls_instrument_info_t g_items[CAP];

/* 실제 t8436 응답 캡처 (2026-09-28) */
static const char *STOCK_RESP =
    "{\"t8436OutBlock\":["
    "{\"hname\":\"동화약품\",\"shcode\":\"000020\",\"expcode\":\"KR7000020008\",\"gubun\":\"1\"},"
    "{\"hname\":\"삼성전자\",\"shcode\":\"005930\",\"expcode\":\"KR7005930003\",\"gubun\":\"1\"},"
    "{\"hname\":\"SK하이닉스\",\"shcode\":\"000660\",\"expcode\":\"KR7000660001\",\"gubun\":\"1\"},"
    "{\"hname\":\"메가엠디\",\"shcode\":\"133750\",\"expcode\":\"KR7133750004\",\"gubun\":\"2\"}]}";

/* 실제 t8467 응답 캡처 (2026-09-28) */
static const char *FUT_RESP =
    "{\"t8467OutBlock\":["
    "{\"hname\":\"F 2612\",\"shcode\":\"A016C000\",\"expcode\":\"KR4101C000\"},"
    "{\"hname\":\"F 2703\",\"shcode\":\"A0173000\",\"expcode\":\"KR41017300\"}]}";

static void test_parse_stock(void) {
    int n = ls_master_parse_stock(STOCK_RESP, strlen(STOCK_RESP), g_items, CAP);
    TR_CHECK(n == 4);
    TR_CHECK(strcmp(g_items[0].shcode, "000020") == 0);
    TR_CHECK(g_items[0].market == LS_MARKET_KOSPI);
    TR_CHECK(!g_items[0].is_futures);
    TR_CHECK(g_items[3].market == LS_MARKET_KOSDAQ);
    TR_CHECK(strcmp(g_items[1].name, "삼성전자") == 0);
}

/* 실제 t8433 응답 두 행 (2026-10-04). 이름에 위클리는 없고 C/P·만기·행사가다. */
static const char *OPT_RESP =
    "{\"t8433OutBlock\":["
    "{\"hname\":\"C 2610   745.0\",\"shcode\":\"B016A745\",\"expcode\":\"KR4B016A7454\"},"
    "{\"hname\":\"P 2610   745.0\",\"shcode\":\"C016A745\",\"expcode\":\"KR4C016A7452\"}]}";

static void test_parse_opt(void) {
    int n = ls_master_parse_opt(OPT_RESP, strlen(OPT_RESP), g_items, CAP);
    TR_CHECK(n == 2);
    TR_CHECK(strcmp(g_items[0].shcode, "B016A745") == 0);
    TR_CHECK(strcmp(g_items[0].name, "C 2610   745.0") == 0);
    TR_CHECK(strcmp(g_items[0].expcode, "KR4B016A7454") == 0);
    TR_CHECK(g_items[0].market == LS_MARKET_KP200_OPT);
    TR_CHECK(!g_items[0].is_futures);
    TR_CHECK(g_items[0].tick_raw == 1.0);
    TR_CHECK(g_items[1].market == LS_MARKET_KP200_OPT);
    TR_CHECK(strcmp(g_items[1].shcode, "C016A745") == 0);
    TR_CHECK(ls_master_parse_opt("{\"rsp_cd\":\"00000\"}", 18, g_items, CAP) == 0);
}

/* 실제 t8435 gubun WK 한 행 (2026-10-04). 월요일 위클리 콜, 행사가 1140. */
static const char *WEEKLY_RESP =
    "{\"t8435OutBlock\":["
    "{\"hname\":\"C 월 W1 1,140.0\",\"shcode\":\"BAFC0A57\",\"expcode\":\"KR4BAFC0A570\"}]}";

static void test_opt_name_and_expiries(void) {
    char cp = 0;
    char ex[24];
    double strike = 0;
    TR_CHECK(ls_opt_parse_name("C 2610   745.0", &cp, ex, sizeof(ex), &strike));
    TR_CHECK(cp == 'C' && strcmp(ex, "2610") == 0 && strike == 745.0);
    TR_CHECK(ls_opt_parse_name("P 2610   745.0", &cp, ex, sizeof(ex), &strike));
    TR_CHECK(cp == 'P' && strike == 745.0);
    TR_CHECK(ls_opt_parse_name("C 월 W1 1,140.0", &cp, ex, sizeof(ex), &strike));
    TR_CHECK(cp == 'C' && strcmp(ex, "월 W1") == 0 && strike == 1140.0);
    TR_CHECK(!ls_opt_parse_name("삼성전자", &cp, ex, sizeof(ex), &strike));
    TR_CHECK(!ls_opt_parse_name("F 2612", &cp, ex, sizeof(ex), &strike));

    ls_instrument_info_t items[4];
    memset(items, 0, sizeof(items));
    items[0].market = LS_MARKET_KP200_OPT;
    snprintf(items[0].name, sizeof(items[0].name), "C 2611   700.0");
    items[1].market = LS_MARKET_KP200_OPT;
    snprintf(items[1].name, sizeof(items[1].name), "C 월 W1 1,140.0");
    items[2].market = LS_MARKET_KP200_OPT;
    snprintf(items[2].name, sizeof(items[2].name), "P 2610   745.0");
    items[3].market = LS_MARKET_KOSPI;
    snprintf(items[3].name, sizeof(items[3].name), "삼성전자");
    tr_ls_master_t m = {.items = items, .count = 4};
    char keys[8][24];
    size_t n = ls_master_option_expiries(&m, &keys[0][0], 24, 8);
    TR_CHECK(n == 3);
    TR_CHECK(strcmp(keys[0], "월 W1") == 0);
    TR_CHECK(strcmp(keys[1], "2610") == 0);
    TR_CHECK(strcmp(keys[2], "2611") == 0);
    const ls_instrument_info_t *hits[4];
    size_t c = ls_master_collect(&m, 3, 0, "2610", hits, 4);
    TR_CHECK(c == 1 && strcmp(hits[0]->name, "P 2610   745.0") == 0);
}

static void test_parse_weekly_opt(void) {
    int n = ls_master_parse_opt(WEEKLY_RESP, strlen(WEEKLY_RESP), g_items, CAP);
    TR_CHECK(n == 1);
    TR_CHECK(strcmp(g_items[0].shcode, "BAFC0A57") == 0);
    TR_CHECK(strcmp(g_items[0].name, "C 월 W1 1,140.0") == 0);
    TR_CHECK(strcmp(g_items[0].expcode, "KR4BAFC0A570") == 0);
    TR_CHECK(g_items[0].market == LS_MARKET_KP200_OPT);
    TR_CHECK(!g_items[0].is_futures);
    TR_CHECK(g_items[0].tick_raw == 1.0);
}

static void test_parse_fut(void) {
    int n = ls_master_parse_fut(FUT_RESP, strlen(FUT_RESP), g_items, CAP);
    TR_CHECK(n == 2);
    TR_CHECK(g_items[0].is_futures);
    TR_CHECK(g_items[0].market == LS_MARKET_KP200_FUT);
    TR_CHECK(strcmp(g_items[0].shcode, "A016C000") == 0);
}

static void test_find_and_at(void) {
    tr_ls_master_t *m = (tr_ls_master_t *)calloc(1, sizeof(*m));
    m->items = g_items;
    int n = ls_master_parse_stock(STOCK_RESP, strlen(STOCK_RESP), g_items, CAP);
    m->count = (size_t)n;

    const ls_instrument_info_t *it = ls_master_find(m, "005930");
    TR_CHECK(it != 0 && strcmp(it->name, "삼성전자") == 0);
    TR_CHECK(ls_master_find(m, "999999") == 0);
    TR_CHECK(ls_master_at(m, 0) != 0);
    TR_CHECK(ls_master_at(m, (size_t)n) == 0);
    TR_CHECK(ls_master_count(m) == (size_t)n);
    free(m);
}

static void test_search(void) {
    tr_ls_master_t *m = (tr_ls_master_t *)calloc(1, sizeof(*m));
    m->items = g_items;
    int n = ls_master_parse_stock(STOCK_RESP, strlen(STOCK_RESP), g_items, CAP);
    m->count = (size_t)n;

    const ls_instrument_info_t *hits[CAP];

    /* 빈 검색어/NULL: 전체 */
    TR_CHECK(ls_master_search(m, "", hits, CAP) == 4);
    TR_CHECK(ls_master_search(m, 0, hits, CAP) == 4);

    /* 종목코드 접두사 */
    size_t k = ls_master_search(m, "005", hits, CAP);
    TR_CHECK(k == 1 && strcmp(hits[0]->shcode, "005930") == 0);
    TR_CHECK(ls_master_search(m, "000", hits, CAP) == 2); /* 000020, 000660 */

    /* 종목명 부분 문자열 */
    k = ls_master_search(m, "삼성", hits, CAP);
    TR_CHECK(k == 1 && strcmp(hits[0]->name, "삼성전자") == 0);
    k = ls_master_search(m, "sk", hits, CAP); /* 대소문자 무시 */
    TR_CHECK(k == 1 && strcmp(hits[0]->shcode, "000660") == 0);

    /* 종목코드 접두도 대소문자 무시 (해외선물 ESZ26) */
    memset(&g_items[4], 0, sizeof(g_items[4]));
    snprintf(g_items[4].shcode, sizeof(g_items[4].shcode), "ESZ26");
    snprintf(g_items[4].name, sizeof(g_items[4].name), "E-mini S&P 500");
    m->count = 5;
    k = ls_master_search(m, "esz", hits, CAP);
    TR_CHECK(k == 1 && strcmp(hits[0]->shcode, "ESZ26") == 0);
    k = ls_master_search(m, "Esz26", hits, CAP);
    TR_CHECK(k == 1 && strcmp(hits[0]->shcode, "ESZ26") == 0);

    /* 일치 없음, cap 제한 */
    TR_CHECK(ls_master_search(m, "없는종목", hits, CAP) == 0);
    TR_CHECK(ls_master_search(m, "", hits, 2) == 2);

    free(m);
}

/* 실제 o3101 응답 캡처 (2026-10-01 — 이 계정은 HKEX/LME만 온다. 필드는 실측 그대로,
 * 나머지 필드는 생략): CUSV26은 DotGb=4(소수 4자리), HSIV26은 DotGb=0 */
static const char *OVS_RESP =
    "{\"o3101OutBlock\":["
    "{\"Symbol\":\"CUSV26\",\"SymbolNm\":\"Renminbi_USD/CNH(2026.10\",\"ApplDate\":\"20261001\","
    "\"BscGdsCd\":\"CUS\",\"BscGdsNm\":\"Renminbi_USD/CNH\",\"ExchCd\":\"HKEX\",\"ExchNm\":\"홍콩거래소\","
    "\"CrncyCd\":\"CNY\",\"UntPrc\":\"0.000100000\",\"MnChgAmt\":\"10.000000000\",\"DotGb\":4},"
    "{\"Symbol\":\"HSIV26\",\"SymbolNm\":\"Hang Seng(2026.10\",\"ApplDate\":\"20261001\","
    "\"BscGdsCd\":\"HSI\",\"BscGdsNm\":\"Hang Seng\",\"ExchCd\":\"HKEX\",\"ExchNm\":\"홍콩거래소\","
    "\"CrncyCd\":\"HKD\",\"UntPrc\":\"1.000000000\",\"MnChgAmt\":\"1.000000000\",\"DotGb\":0}],"
    "\"rsp_cd\":\"00000\",\"rsp_msg\":\"정상적으로 조회가 완료되었습니다.\"}";

static void test_parse_ovs_master(void) {
    ls_ovsfut_master_row_t rows[CAP];
    int n = ls_ovsfut_parse_master(OVS_RESP, strlen(OVS_RESP), rows, CAP);
    TR_CHECK(n == 2);
    TR_CHECK(strcmp(rows[0].symbol, "CUSV26") == 0);
    TR_CHECK(strcmp(rows[0].exch_cd, "HKEX") == 0);
    TR_CHECK(rows[0].tick_raw > 0.009 && rows[0].tick_raw < 0.011); /* 0.0001 × 100 */
    TR_CHECK(rows[0].dot_gb == 4);
    TR_CHECK(rows[1].tick_raw > 99.9 && rows[1].tick_raw < 100.1);  /* 1.0 × 100 */
    TR_CHECK(rows[1].dot_gb == 0);
    /* OutBlock 없음(이 계정의 CME처럼)은 0행 — 오류 아님 */
    const char *empty = "{\"rsp_cd\":\"00000\",\"rsp_msg\":\"해당자료가 없습니다.\"}";
    TR_CHECK(ls_ovsfut_parse_master(empty, strlen(empty), rows, CAP) == 0);
}

static void test_ovs_precision_exclusion(void) {
    /* ×100 raw 정밀도 배제 (ls_master_fetch의 o3101 등록 필터와 같은 규칙):
     * 소수 3자리 이상(DotGb > 2)은 가격이 절단되므로 미등록 — CUSV26(4)은 배제,
     * HSIV26(0) 같은 정상 종목은 등록된다. DotGb 없음(-1)은 보수적 배제 */
    TR_CHECK(!ls_ovsfut_fits_raw100(4));
    TR_CHECK(!ls_ovsfut_fits_raw100(3));
    TR_CHECK(ls_ovsfut_fits_raw100(2));
    TR_CHECK(ls_ovsfut_fits_raw100(1));
    TR_CHECK(ls_ovsfut_fits_raw100(0));
    TR_CHECK(!ls_ovsfut_fits_raw100(-1));
    /* 실측 응답에 필터를 적용하면 CUSV26만 빠지고 HSI는 남는다 */
    ls_ovsfut_master_row_t rows[CAP];
    int n = ls_ovsfut_parse_master(OVS_RESP, strlen(OVS_RESP), rows, CAP);
    TR_CHECK(n == 2);
    int registered = 0;
    const char *last_sym = 0;
    for (int i = 0; i < n; i++) {
        if (ls_ovsfut_fits_raw100(rows[i].dot_gb)) {
            registered++;
            last_sym = rows[i].symbol;
        }
    }
    TR_CHECK(registered == 1);
    TR_CHECK(last_sym != 0 && strcmp(last_sym, "HSIV26") == 0);
}

static void test_ovsfut_static_table(void) {
    /* 월물 코드 정확 일치 + 접두 매치 (롤링된 신규 월물도 접두로 판별) */
    const ls_ovsfut_entry_t *es = ls_ovsfut_find("ESZ26");
    TR_CHECK(es != 0 && strcmp(es->prefix, "ES") == 0 && es->tick_raw == 25.0);
    TR_CHECK(ls_ovsfut_find("ESH27") == es);       /* 다음 월물도 ES */
    TR_CHECK(ls_ovsfut_find("NQZ26") != es);       /* NQ는 별 행 (ES 접두 오매치 방지) */
    const ls_ovsfut_entry_t *cl = ls_ovsfut_find("CLX26");
    TR_CHECK(cl != 0 && strcmp(cl->prefix, "CL") == 0 && cl->tick_raw == 1.0);
    /* 국내·HKEX 코드는 정적 표에 없다 (o3101 레지스트리가 담당) */
    TR_CHECK(ls_ovsfut_find("005930") == 0);
    TR_CHECK(ls_ovsfut_find("A016C000") == 0);
    TR_CHECK(ls_ovsfut_find("CUSV26") == 0);
    TR_CHECK(ls_ovsfut_find("ES") == 0); /* 월물 없는 접두만으로는 구독 불가 — 미매치 */
    TR_CHECK(ls_ovsfut_find(0) == 0);
    /* 세션: KST 07:00 → 익일 06:00 (close < open = 야간 넘김), 평일 */
    tr_session_policy_t s = ls_ovsfut_session();
    TR_CHECK(s.utc_offset_min == 540 && s.open_min == 420 && s.close_min == 360);
    TR_CHECK(s.days_mask == TR_SESSION_WEEKDAYS);
    TR_CHECK(tr_session_policy_validate(&s));
}

/* 코드가 정확히 같은 종목은, 그 코드가 이름에 들어 있는 앞선 종목보다 먼저 나온다.
 * 검색 상한이 20이라 지수옵션 코드가 주식 이름에 묻히면 종목 추가에서 고를 수 없다. */
static void test_search_exact_code_before_name(void) {
    ls_instrument_info_t items[3];
    memset(items, 0, sizeof(items));
    snprintf(items[0].shcode, sizeof(items[0].shcode), "000001");
    snprintf(items[0].name, sizeof(items[0].name), "B016A745 관련");
    snprintf(items[1].shcode, sizeof(items[1].shcode), "B016A745");
    snprintf(items[1].name, sizeof(items[1].name), "C 2610   745.0");
    snprintf(items[2].shcode, sizeof(items[2].shcode), "B016A750");
    snprintf(items[2].name, sizeof(items[2].name), "C 2610   750.0");
    tr_ls_master_t m = {.items = items, .count = 3};
    const ls_instrument_info_t *hits[2];

    size_t k = ls_master_search(&m, "B016A745", hits, 1);
    TR_CHECK(k == 1 && strcmp(hits[0]->shcode, "B016A745") == 0);

    k = ls_master_search(&m, "b016a", hits, 2);
    TR_CHECK(k == 2);
    TR_CHECK(strcmp(hits[0]->shcode, "B016A745") == 0);
    TR_CHECK(strcmp(hits[1]->shcode, "B016A750") == 0);
}

static void test_ovsfut_static_search(void) {
    const ls_ovsfut_entry_t *hits[CAP];
    /* 접두·월물 코드 접두사 */
    size_t k = ls_ovsfut_search("ES", hits, CAP);
    TR_CHECK(k == 1 && strcmp(hits[0]->prefix, "ES") == 0); /* 접두 매치만 (이름 오탐 없음) */
    k = ls_ovsfut_search("ESZ26", hits, CAP);
    TR_CHECK(k == 1 && strcmp(hits[0]->contract, "ESZ26") == 0);
    /* 이름 부분 문자열 (대소문자 무시) — 단어 경계에서 시작하는 매치만 인정 */
    k = ls_ovsfut_search("nasdaq", hits, CAP);
    TR_CHECK(k == 1 && strcmp(hits[0]->prefix, "NQ") == 0);
    k = ls_ovsfut_search("crude", hits, CAP);
    TR_CHECK(k == 1 && strcmp(hits[0]->prefix, "CL") == 0);
    k = ls_ovsfut_search("jones", hits, CAP); /* "Mini Dow Jones"의 단어 시작 */
    TR_CHECK(k == 1 && strcmp(hits[0]->prefix, "YM") == 0);
    /* 단어 중간 매치는 오탐으로 거부 ("es"가 "Jones"의 끝 두 글자에 걸리던 문제) */
    k = ls_ovsfut_search("es", hits, CAP); /* 코드 접두도 대소문자 무시 */
    TR_CHECK(k == 1 && strcmp(hits[0]->contract, "ESZ26") == 0);
    TR_CHECK(ls_ovsfut_search("ones", hits, CAP) == 0);
    /* 일치 없음·빈 검색어(전체) */
    TR_CHECK(ls_ovsfut_search("삼성전자", hits, CAP) == 0);
    TR_CHECK(ls_ovsfut_search("", hits, CAP) == ls_ovsfut_count());
}

int main(void) {
    test_parse_stock();
    test_parse_fut();
    test_parse_opt();
    test_opt_name_and_expiries();
    test_parse_weekly_opt();
    test_find_and_at();
    test_search();
    test_search_exact_code_before_name();
    test_parse_ovs_master();
    test_ovs_precision_exclusion();
    test_ovsfut_static_table();
    test_ovsfut_static_search();
    TR_TEST_SUMMARY();
}
