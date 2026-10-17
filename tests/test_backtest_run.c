#include "test_util.h"

#include <string.h>

#include "adapters/storage/live_bars.h"
#include "app/backtest_cmd.h"
#include "core/market/candle.h"
#include "core/model/civil_time.h"

static uint64_t id_of(const char *code) {
    uint64_t h = 1469598103934665603ULL;
    while (*code) {
        h ^= (unsigned char)*code++;
        h *= 1099511628211ULL;
    }
    return h != 0 ? h : 1;
}

static int64_t kst_us(int hour, int min) {
    tr_civil_t c;
    tr_time_us_t t = 0;
    memset(&c, 0, sizeof(c));
    c.year = 2024;
    c.month = 10;
    c.day = 1;
    c.hour = (unsigned)hour;
    c.min = (unsigned)min;
    tr_time_us_from_civil(&c, 540, &t);
    return t;
}

static void put(const char *code, int64_t open_us, int price) {
    tr_candle_t c;
    memset(&c, 0, sizeof(c));
    c.instrument_id = id_of(code);
    c.timeframe_sec = 60;
    c.open_time_us = open_us;
    c.close_time_us = open_us + 60000000LL;
    c.open = c.close = price;
    c.high = price + 100;
    c.low = price - 100;
    c.state = TR_CANDLE_CLOSED;
    tr_live_bars_put(&c);
}

int main(void) {
    char err[128] = {0};
    /* 봉 가격은 포인트. ES 1틱은 0.25, 표를 모르면 선물 자동 0.05. */
    TR_CHECK((int)(tr_bt_price_scale("ESZ26") * 100.0 + 0.5) == 25);
    TR_CHECK((int)(tr_bt_price_scale("NQZ26") * 100.0 + 0.5) == 25);
    TR_CHECK((int)(tr_bt_price_scale("RTYZ26") * 100.0 + 0.5) == 10);
    TR_CHECK((int)(tr_bt_price_scale("UNKNOWN") * 100.0 + 0.5) == 5);
    char out[8192];
    const char *payload =
        "{\"type\":\"backtest.run\",\"data\":{\"signal\":\"pair-short\",\"start\":\"2024-10-01\","
        "\"end\":\"2024-10-01\",\"qty\":1,\"base\":\"AAA\",\"opp\":\"BBB\",\"fut\":\"CCC\"}}";
    TR_CHECK(tr_live_bars_open(":memory:", err, sizeof(err)) == 0);
    put("AAA", kst_us(9, 0), 1000);
    put("BBB", kst_us(9, 0), 1000);
    put("CCC", kst_us(9, 0), 1000);
    put("AAA", kst_us(9, 1), 1000);
    put("BBB", kst_us(9, 1), 1000);
    put("CCC", kst_us(9, 1), 1000);
    put("AAA", kst_us(9, 2), 800);
    put("BBB", kst_us(9, 2), 800);
    put("CCC", kst_us(9, 2), 800);
    TR_CHECK(tr_backtest_run(payload, out, sizeof(out)) == 0);
    TR_CHECK(strstr(out, "\"bars\"") != 0);
    TR_CHECK(strstr(out, "양매도진입") != 0);
    TR_CHECK(strstr(out, "\"trades\"") != 0);
    {
        char big[70 * 1024];
        memset(big, 'a', sizeof(big) - 1);
        big[0] = '{';
        big[sizeof(big) - 2] = '}';
        big[sizeof(big) - 1] = '\0';
        TR_CHECK(tr_backtest_fit_ipc(big, sizeof(big)) == 0);
        TR_CHECK(strcmp(big, "{\"result_file\":true}") == 0);
    }
    tr_live_bars_close();
    TR_TEST_SUMMARY();
}
