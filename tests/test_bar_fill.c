#include "test_util.h"

#include <string.h>

#include "adapters/storage/store.h"
#include "core/backtest/bar_fill.h"
#include "core/model/civil_time.h"

static tr_store_t *open_db(void) {
    tr_store_t *s = 0;
    char err[128] = {0};
    TR_CHECK(tr_store_open(":memory:", &s, err, sizeof(err)) == TR_STORE_OK);
    TR_CHECK(tr_store_migrate(s) == TR_STORE_OK);
    return s;
}

static int64_t us_of(int y, unsigned mo, unsigned d, unsigned h, unsigned mi, int offset_min) {
    tr_civil_t c;
    tr_time_us_t t = 0;
    memset(&c, 0, sizeof(c));
    c.year = y;
    c.month = mo;
    c.day = d;
    c.hour = h;
    c.min = mi;
    TR_CHECK(tr_time_us_from_civil(&c, offset_min, &t));
    return t;
}

typedef struct {
    int gets;
    int posts;
    int fail;
    const char *theta_body;
    const char *page0;
    const char *page1;
    char last_url[800];
    char last_body[800];
} script_t;

static int fetch(void *ctx, int method, const char *url, const char *body, char *resp, size_t cap,
                 char *err, size_t err_cap) {
    script_t *s = ctx;
    snprintf(s->last_url, sizeof(s->last_url), "%s", url != 0 ? url : "");
    snprintf(s->last_body, sizeof(s->last_body), "%s", body != 0 ? body : "");
    if (s->fail) {
        snprintf(err, err_cap, "연결할 수 없습니다");
        return -1;
    }
    if (method == TR_BAR_GET) {
        s->gets++;
        snprintf(resp, cap, "%s", s->theta_body);
        return 0;
    }
    s->posts++;
    if (strstr(s->last_body, "99999999999999") != 0) {
        snprintf(err, err_cap, "끝 키를 다시 보냈다");
        return -1;
    }
    if (strstr(s->last_body, "page2") != 0) {
        snprintf(resp, cap, "%s", s->page1);
    } else {
        snprintf(resp, cap, "%s", s->page0);
    }
    return 0;
}

static int count_range(tr_store_t *store, int64_t from, int64_t to) {
    return tr_store_query_candles(store, 7, 60, from, to, 100, 0, 0);
}

static void test_option_stores_once(void) {
    tr_store_t *store = open_db();
    script_t sc;
    tr_bar_fill_req_t req;
    char err[128] = {0};
    int64_t start = us_of(2026, 10, 9, 9, 30, -240);
    int64_t end = start + 2 * 60 * 1000000LL;
    memset(&sc, 0, sizeof(sc));
    sc.theta_body =
        "{\"response\":[{\"contract\":{\"symbol\":\"SPXW\",\"expiration\":\"2026-10-09\","
        "\"strike\":8400,\"right\":\"put\"},\"data\":["
        "{\"timestamp\":\"2026-10-09T09:30:00\",\"open\":1.5,\"high\":1.6,\"low\":1.4,\"close\":1.55,\"volume\":4},"
        "{\"timestamp\":\"2026-10-09T09:31:00\",\"open\":1.55,\"high\":1.7,\"low\":1.5,\"close\":1.6,\"volume\":1}"
        "]}]}";
    memset(&req, 0, sizeof(req));
    req.product = TR_PRODUCT_OPTIONS;
    req.instrument_id = 7;
    req.price_scale = 100;
    req.qty_scale = 1;
    req.code = "SPXW:20261009:8400:put";
    TR_CHECK(tr_bar_fill(store, &req, start, end, fetch, &sc, err, sizeof(err)) == 0);
    TR_CHECK(sc.gets == 1);
    TR_CHECK(sc.posts == 0);
    TR_CHECK(strstr(sc.last_url, "/v3/option/history/ohlc") != 0);
    TR_CHECK(strstr(sc.last_url, "symbol=SPXW") != 0);
    TR_CHECK(strstr(sc.last_url, "right=put") != 0);
    TR_CHECK(strstr(sc.last_url, "interval=1m") != 0);
    TR_CHECK(count_range(store, start, end) == 2);
    TR_CHECK(tr_bar_fill(store, &req, start, end, fetch, &sc, err, sizeof(err)) == 0);
    TR_CHECK(sc.gets == 1);
    tr_store_close(store);
}

static void test_futures_skip_theta_and_page(void) {
    tr_store_t *store = open_db();
    script_t sc;
    tr_bar_fill_req_t req;
    char err[160] = {0};
    int64_t start = us_of(2026, 10, 1, 0, 0, 540);
    int64_t end = us_of(2026, 10, 2, 0, 0, 540);
    memset(&sc, 0, sizeof(sc));
    sc.page0 = "{\"nxt_key\":\"page2\",\"Occurs1\":["
               "{\"occurs1_ref_dt\":\"20261013\",\"occurs1_exch_tm\":\"100000\","
               "\"occurs1_open_pric\":\"10\",\"occurs1_high_pric\":\"11\","
               "\"occurs1_low_pric\":\"9\",\"occurs1_close_pric\":\"10.5\",\"occurs1_trd_qty\":\"3\"},"
               "{\"occurs1_ref_dt\":\"20261013\",\"occurs1_exch_tm\":\"090000\","
               "\"occurs1_open_pric\":\"10\",\"occurs1_high_pric\":\"10\","
               "\"occurs1_low_pric\":\"10\",\"occurs1_close_pric\":\"10\",\"occurs1_trd_qty\":\"1\"}"
               "]}";
    sc.page1 = "{\"nxt_key\":\"99999999999999\",\"Occurs1\":["
               "{\"occurs1_ref_dt\":\"20261001\",\"occurs1_exch_tm\":\"000000\","
               "\"occurs1_open_pric\":\"8\",\"occurs1_high_pric\":\"8\","
               "\"occurs1_low_pric\":\"8\",\"occurs1_close_pric\":\"8\",\"occurs1_trd_qty\":\"2\"},"
               "{\"occurs1_ref_dt\":\"20260930\",\"occurs1_exch_tm\":\"235900\","
               "\"occurs1_open_pric\":\"7\",\"occurs1_high_pric\":\"7\","
               "\"occurs1_low_pric\":\"7\",\"occurs1_close_pric\":\"7\",\"occurs1_trd_qty\":\"1\"}"
               "]}";
    memset(&req, 0, sizeof(req));
    req.product = TR_PRODUCT_FUTURES;
    req.instrument_id = 7;
    req.price_scale = 100;
    req.qty_scale = 1;
    req.code = "ESZ26";
    TR_CHECK(tr_bar_fill(store, &req, start, end, fetch, &sc, err, sizeof(err)) == 0);
    TR_CHECK(sc.gets == 0);
    TR_CHECK(sc.posts == 2);
    TR_CHECK(strstr(sc.last_url, "thetadata") == 0);
    TR_CHECK(strstr(sc.last_body, "FCME") != 0);
    TR_CHECK(strstr(sc.last_body, "ESZ26") != 0);
    TR_CHECK(count_range(store, start, end) == 1);
    TR_CHECK(count_range(store, 0, start) == 1);
    TR_CHECK(tr_bar_fill(store, &req, start, end, fetch, &sc, err, sizeof(err)) == 0);
    TR_CHECK(sc.posts == 2);
    tr_store_close(store);
}

static void put_min(tr_store_t *store, int64_t open_us) {
    tr_candle_t c;
    memset(&c, 0, sizeof(c));
    c.instrument_id = 7;
    c.timeframe_sec = 60;
    c.open_time_us = open_us;
    c.close_time_us = open_us + 60000000LL;
    c.open = c.high = c.low = c.close = 100;
    c.state = TR_CANDLE_CLOSED;
    c.source_id = 1;
    TR_CHECK(tr_store_put_candle(store, &c) == TR_STORE_OK);
}

static void test_futures_hole_is_refetched(void) {
    tr_store_t *store = open_db();
    script_t sc;
    tr_bar_fill_req_t req;
    char err[160] = {0};
    int64_t start = us_of(2026, 10, 1, 0, 0, 540);
    int64_t end = us_of(2026, 10, 20, 0, 0, 540);
    memset(&sc, 0, sizeof(sc));
    sc.page0 = "{\"nxt_key\":\"page2\",\"Occurs1\":["
               "{\"occurs1_ref_dt\":\"20261013\",\"occurs1_exch_tm\":\"100000\","
               "\"occurs1_open_pric\":\"10\",\"occurs1_high_pric\":\"11\","
               "\"occurs1_low_pric\":\"9\",\"occurs1_close_pric\":\"10\",\"occurs1_trd_qty\":\"1\"},"
               "{\"occurs1_ref_dt\":\"20261002\",\"occurs1_exch_tm\":\"100000\","
               "\"occurs1_open_pric\":\"10\",\"occurs1_high_pric\":\"10\","
               "\"occurs1_low_pric\":\"10\",\"occurs1_close_pric\":\"10\",\"occurs1_trd_qty\":\"1\"}"
               "]}";
    sc.page1 = "{\"nxt_key\":\"99999999999999\",\"Occurs1\":["
               "{\"occurs1_ref_dt\":\"20260930\",\"occurs1_exch_tm\":\"230000\","
               "\"occurs1_open_pric\":\"7\",\"occurs1_high_pric\":\"7\","
               "\"occurs1_low_pric\":\"7\",\"occurs1_close_pric\":\"7\",\"occurs1_trd_qty\":\"1\"}"
               "]}";
    put_min(store, us_of(2026, 9, 30, 23, 0, 540));
    put_min(store, start);
    put_min(store, us_of(2026, 10, 15, 0, 0, 540));
    memset(&req, 0, sizeof(req));
    req.product = TR_PRODUCT_FUTURES;
    req.instrument_id = 7;
    req.price_scale = 100;
    req.qty_scale = 1;
    req.code = "ESZ26";
    TR_CHECK(tr_bar_fill(store, &req, start, end, fetch, &sc, err, sizeof(err)) == 0);
    TR_CHECK(sc.posts == 2);
    tr_store_close(store);
}

static void test_futures_before_history(void) {
    tr_store_t *store = open_db();
    script_t sc;
    tr_bar_fill_req_t req;
    char err[160] = {0};
    int64_t start = us_of(2020, 1, 1, 0, 0, 540);
    int64_t end = us_of(2020, 1, 2, 0, 0, 540);
    memset(&sc, 0, sizeof(sc));
    sc.page0 = "{\"nxt_key\":\"99999999999999\",\"Occurs1\":["
               "{\"occurs1_ref_dt\":\"20250911\",\"occurs1_exch_tm\":\"021000\","
               "\"occurs1_open_pric\":\"1\",\"occurs1_high_pric\":\"1\","
               "\"occurs1_low_pric\":\"1\",\"occurs1_close_pric\":\"1\",\"occurs1_trd_qty\":\"1\"}"
               "]}";
    sc.page1 = sc.page0;
    memset(&req, 0, sizeof(req));
    req.product = TR_PRODUCT_FUTURES;
    req.instrument_id = 7;
    req.price_scale = 100;
    req.qty_scale = 1;
    req.code = "ESZ26";
    TR_CHECK(tr_bar_fill(store, &req, start, end, fetch, &sc, err, sizeof(err)) == -1);
    TR_CHECK(sc.posts == 1);
    TR_CHECK(strstr(err, "2025-09-11") != 0 || strstr(err, "20250911") != 0);
    tr_store_close(store);
}

static void test_theta_down_and_index(void) {
    tr_store_t *store = open_db();
    script_t sc;
    tr_bar_fill_req_t req;
    char err[128] = {0};
    int64_t start = us_of(2026, 10, 9, 9, 30, -240);
    memset(&sc, 0, sizeof(sc));
    sc.fail = 1;
    memset(&req, 0, sizeof(req));
    req.product = TR_PRODUCT_OPTIONS;
    req.instrument_id = 7;
    req.price_scale = 100;
    req.qty_scale = 1;
    req.code = "SPXW:20261009:8400:call";
    TR_CHECK(tr_bar_fill(store, &req, start, start + 60000000LL, fetch, &sc, err, sizeof(err)) == -1);
    TR_CHECK(strstr(err, "연결") != 0);
    req.product = TR_PRODUCT_INDEX;
    req.code = "SPX";
    err[0] = 0;
    sc.gets = 0;
    sc.fail = 0;
    TR_CHECK(tr_bar_fill(store, &req, start, start + 60000000LL, fetch, &sc, err, sizeof(err)) == -1);
    TR_CHECK(sc.gets == 0);
    TR_CHECK(strstr(err, "지수") != 0);
    tr_store_close(store);
}

int main(void) {
    test_option_stores_once();
    test_futures_skip_theta_and_page();
    test_futures_hole_is_refetched();
    test_futures_before_history();
    test_theta_down_and_index();
    TR_TEST_SUMMARY();
}
