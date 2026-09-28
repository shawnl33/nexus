/* LS 종목 마스터 파서 테스트: 실제 응답 캡처 기반 (네트워크 불필요) */

#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#include "adapters/ls/ls_master.h"

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

int main(void) {
    test_parse_stock();
    test_parse_fut();
    test_find_and_at();
    TR_TEST_SUMMARY();
}
