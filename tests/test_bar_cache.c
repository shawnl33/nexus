#include "test_util.h"

#include <string.h>

#include "runtime/bar_cache.h"

static tr_candle_t bar(int64_t open, uint32_t rev, int64_t close) {
    tr_candle_t c;
    memset(&c, 0, sizeof(c));
    c.open_time_us = open;
    c.close_time_us = open + 60000000;
    c.close = close;
    c.revision = rev;
    c.state = TR_CANDLE_CLOSED;
    c.timeframe_sec = 60;
    return c;
}

static void test_cache_only_and_order(void) {
    tr_candle_t broker[1] = {bar(200, 0, 2)};
    tr_candle_t cache[2] = {bar(100, 0, 1), bar(300, 1, 3)};
    tr_candle_t out[4];
    size_t n = tr_bar_cache_merge(broker, 1, cache, 2, out, 4);
    TR_CHECK(n == 3);
    TR_CHECK(out[0].open_time_us == 100 && out[0].close == 1);
    TR_CHECK(out[1].open_time_us == 200 && out[1].close == 2);
    TR_CHECK(out[2].open_time_us == 300 && out[2].close == 3);
}

static void test_higher_revision_wins(void) {
    tr_candle_t broker[1] = {bar(100, 0, 10)};
    tr_candle_t cache[1] = {bar(100, 2, 20)};
    tr_candle_t out[1];
    size_t n = tr_bar_cache_merge(broker, 1, cache, 1, out, 1);
    TR_CHECK(n == 1);
    TR_CHECK(out[0].close == 20 && out[0].revision == 2);
}

static void test_equal_revision_prefers_broker(void) {
    tr_candle_t broker[1] = {bar(100, 1, 10)};
    tr_candle_t cache[1] = {bar(100, 1, 20)};
    tr_candle_t out[1];
    TR_CHECK(tr_bar_cache_merge(broker, 1, cache, 1, out, 1) == 1);
    TR_CHECK(out[0].close == 10);
}

static void test_keeps_newest_when_capped(void) {
    tr_candle_t cache[3] = {bar(100, 0, 1), bar(200, 0, 2), bar(300, 0, 3)};
    tr_candle_t out[2];
    size_t n = tr_bar_cache_merge(0, 0, cache, 3, out, 2);
    TR_CHECK(n == 2);
    TR_CHECK(out[0].open_time_us == 200 && out[1].open_time_us == 300);
}

int main(void) {
    test_cache_only_and_order();
    test_higher_revision_wins();
    test_equal_revision_prefers_broker();
    test_keeps_newest_when_capped();
    TR_TEST_SUMMARY();
}
