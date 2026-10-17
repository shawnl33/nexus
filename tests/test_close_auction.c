#include <string.h>

#include "test_util.h"

#include "core/market/close_auction.h"
#include "core/model/civil_time.h"

static tr_time_us_t at(int hour, int min) {
    tr_civil_t c = {2026, 10, 16, (unsigned)hour, (unsigned)min, 0};
    tr_time_us_t t = 0;
    TR_CHECK(tr_time_us_from_civil(&c, 540, &t));
    return t;
}

static tr_candle_t minute(int hour, int min, int64_t volume) {
    tr_candle_t b;
    memset(&b, 0, sizeof(b));
    b.instrument_id = 7;
    b.timeframe_sec = 60;
    b.open_time_us = at(hour, min);
    b.close_time_us = b.open_time_us + TR_US_PER_MIN;
    b.open = b.high = b.low = b.close = 1000;
    b.volume = volume;
    b.state = TR_CANDLE_CLOSED;
    b.source_id = 2;
    return b;
}

static void test_builds_one_close_bar(void) {
    tr_candle_t mins[2];
    tr_close_auction_day_t day;
    tr_candle_t out[2];
    size_t n;
    mins[0] = minute(15, 34, 10);
    mins[1] = minute(15, 35, 20);
    day.day = tr_days_from_civil(2026, 10, 16);
    day.close = 109400;
    day.volume = 50;
    n = tr_fut_close_auction_bars(mins, 2, &day, 1, at(15, 45), out, 2);
    TR_CHECK(n == 1);
    TR_CHECK(out[0].open_time_us == at(15, 45));
    TR_CHECK(out[0].open == 109400 && out[0].high == 109400 && out[0].low == 109400 && out[0].close == 109400);
    TR_CHECK(out[0].volume == 20);
    TR_CHECK(out[0].instrument_id == 7);
    TR_CHECK(out[0].state == TR_CANDLE_CLOSED);
}

static void test_skips_when_present_or_too_early(void) {
    tr_candle_t mins[2];
    tr_close_auction_day_t day;
    tr_candle_t out[2];
    mins[0] = minute(15, 35, 20);
    mins[1] = minute(15, 45, 5);
    day.day = tr_days_from_civil(2026, 10, 16);
    day.close = 109400;
    day.volume = 50;
    TR_CHECK(tr_fut_close_auction_bars(mins, 2, &day, 1, at(16, 0), out, 2) == 0);
    mins[1] = minute(15, 35, 20);
    TR_CHECK(tr_fut_close_auction_bars(mins, 1, &day, 1, at(15, 40), out, 2) == 0);
    day.day = tr_days_from_civil(2026, 10, 15);
    TR_CHECK(tr_fut_close_auction_bars(mins, 1, &day, 1, at(16, 0), out, 2) == 0);
}

int main(void) {
    test_builds_one_close_bar();
    test_skips_when_present_or_too_early();
    TR_TEST_SUMMARY();
}
