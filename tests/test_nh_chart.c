#include "test_util.h"

#include <string.h>

#include "adapters/nh/nh_chart.h"
#include "adapters/nh/nh_rt.h"

static const char SAMPLE[] =
    "{\"nxt_key\":\"20261008160500999999999999999999\",\"nrec\":\"0003\",\"sym\":\"\",\"exch_cd\":\"OCBO\","
    "\"Occurs1\":["
    "{\"occurs1_ref_dt\":\"20261008\",\"occurs1_open_pric\":\"22.50\",\"occurs1_high_pric\":\"22.50\","
    "\"occurs1_low_pric\":\"22.50\",\"occurs1_close_pric\":\"22.50\",\"occurs1_exch_tm\":\"163000\","
    "\"occurs1_cum_trd_qty\":\"5\"},"
    "{\"occurs1_ref_dt\":\"20261008\",\"occurs1_open_pric\":\"22.50\",\"occurs1_high_pric\":\"22.50\","
    "\"occurs1_low_pric\":\"22.50\",\"occurs1_close_pric\":\"22.50\",\"occurs1_exch_tm\":\"162900\","
    "\"occurs1_cum_trd_qty\":\"5\"},"
    "{\"occurs1_ref_dt\":\"20261008\",\"occurs1_open_pric\":\"23.20\",\"occurs1_high_pric\":\"23.20\","
    "\"occurs1_low_pric\":\"23.20\",\"occurs1_close_pric\":\"23.20\",\"occurs1_exch_tm\":\"162100\","
    "\"occurs1_cum_trd_qty\":\"2\"}"
    "]}";

static void test_parse(void) {
    tr_candle_t bars[8];
    size_t n = 0;
    char nxt[64];
    TR_CHECK(nh_chart_parse_minutes(SAMPLE, strlen(SAMPLE), 9, bars, 8, &n, nxt, sizeof(nxt)) == 0);
    TR_CHECK(n == 3);
    TR_CHECK(strcmp(nxt, "20261008160500999999999999999999") == 0);
    TR_CHECK(bars[0].open_time_us < bars[1].open_time_us);
    TR_CHECK(bars[1].open_time_us < bars[2].open_time_us);
    TR_CHECK(bars[0].close == 2320);
    TR_CHECK(bars[2].close == 2250);
    TR_CHECK(bars[0].volume == 2);
    TR_CHECK(bars[1].volume == 5);
    TR_CHECK(bars[2].volume == 5);
    TR_CHECK(bars[0].timeframe_sec == 60);
    TR_CHECK(strcmp(nh_exch_for_symbol("O_SPW2FV26-C7785.0"), "OCBO") == 0);
    TR_CHECK(strcmp(nh_exch_for_symbol("O_SPXV26-C1000.0"), "OCBO") == 0);
    TR_CHECK(strcmp(nh_exch_for_symbol("O_NDXV26-C29225.0"), "OCBO") == 0);
    TR_CHECK(strcmp(nh_exch_for_symbol("VXV26"), "FCBO") == 0);
    TR_CHECK(strcmp(nh_exch_for_symbol("VXMV26"), "FCBO") == 0);
    TR_CHECK(strcmp(nh_exch_for_symbol("ESZ26"), "FCME") == 0);
    TR_CHECK(strcmp(nh_exch_for_symbol("MESZ26"), "FCME") == 0);
    TR_CHECK(strcmp(nh_exch_for_symbol("6EZ26"), "FCME") == 0);
    TR_CHECK(nh_tick_raw("ESZ26") == 25.0);
    TR_CHECK(nh_tick_raw("VXV26") == 5.0);
    TR_CHECK(nh_exch_for_symbol("YMZ26") == 0);
    TR_CHECK(nh_exch_for_symbol("CLX26") == 0);
    TR_CHECK(nh_exch_for_symbol("ESZ26-ESH27") == 0);
    TR_CHECK(nh_exch_for_symbol("A016C000") == 0);
}

static void test_futures_bar_volume(void) {
    /* ESZ26, 2026-10-14 KST 23:00~23:02 API 실측. 일중 누적이 아닌 봉 거래량. */
    const char *json = "{\"Occurs1\":["
        "{\"occurs1_ref_dt\":\"20261014\",\"occurs1_exch_tm\":\"230200\","
        "\"occurs1_close_pric\":\"7810.75\",\"occurs1_cum_trd_qty\":\"6642\"},"
        "{\"occurs1_ref_dt\":\"20261014\",\"occurs1_exch_tm\":\"230100\","
        "\"occurs1_close_pric\":\"7810.25\",\"occurs1_cum_trd_qty\":7637},"
        "{\"occurs1_ref_dt\":\"20261014\",\"occurs1_exch_tm\":\"230000\","
        "\"occurs1_close_pric\":\"7809.75\",\"occurs1_cum_trd_qty\":\"12084\"}]}";
    tr_candle_t bars[3];
    size_t n = 0;
    TR_CHECK(nh_chart_parse_minutes(json, strlen(json), 9, bars, 3, &n, 0, 0) == 0);
    TR_CHECK(n == 3);
    TR_CHECK(bars[0].volume == 12084);
    TR_CHECK(bars[1].volume == 7637);
    TR_CHECK(bars[2].volume == 6642);
    /* 한 봉만 요청하거나 페이지 경계에 있어도 거래량이 유지되어야 한다. */
    TR_CHECK(nh_chart_parse_minutes(json, strlen(json), 9, bars, 1, &n, 0, 0) == 0);
    TR_CHECK(n == 1);
    TR_CHECK(bars[0].close == 781075);
    TR_CHECK(bars[0].volume == 6642);
}

static void test_take_pong(void) {
    const char *raw = "{ \"code\" : \"pingpong\" , \"msg\" : \"pingpong\" }";
    char out[128];
    memset(out, 0x5a, sizeof(out));
    const char *init = "{\"code\":\"INIT\",\"msg\":\"INIT_SUCCESS\"}";
    const char *other = "{\"code\":\"pingpong\",\"msg\":\"other\"}";
    TR_CHECK(nh_rt_take_pong(raw, strlen(raw), out, sizeof(out)));
    TR_CHECK(strcmp(out, raw) == 0);
    TR_CHECK(!nh_rt_take_pong(init, strlen(init), out, sizeof(out)));
    TR_CHECK(strcmp(out, raw) == 0);
    TR_CHECK(!nh_rt_take_pong(other, strlen(other), out, sizeof(out)));
}

int main(void) {
    test_parse();
    test_futures_bar_volume();
    test_take_pong();
    TR_TEST_SUMMARY();
}
