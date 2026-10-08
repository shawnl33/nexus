#include "test_util.h"

#include <string.h>

#include "adapters/nh/nh_chart.h"

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
    TR_CHECK(bars[0].volume == 0);
    TR_CHECK(bars[1].volume == 3);
    TR_CHECK(bars[2].volume == 0);
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

int main(void) {
    test_parse();
    TR_TEST_SUMMARY();
}
