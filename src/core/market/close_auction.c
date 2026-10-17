#include "core/market/close_auction.h"

#include <string.h>

#include "core/model/civil_time.h"

#define TR_CLOSE_AUCTION_MIN (15u * 60u + 45u)

static const tr_close_auction_day_t *find_day(const tr_close_auction_day_t *days, size_t n, int64_t day) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (days[i].day == day) {
            return &days[i];
        }
    }
    return 0;
}

size_t tr_fut_close_auction_bars(const tr_candle_t *mins, size_t nmin, const tr_close_auction_day_t *days,
                                 size_t ndays, tr_time_us_t now_us, tr_candle_t *out, size_t out_cap) {
    size_t i = 0;
    size_t nout = 0;
    if (mins == 0 || days == 0 || out == 0 || out_cap == 0) {
        return 0;
    }
    while (i < nmin) {
        int64_t day = 0;
        uint32_t minute = 0;
        size_t j;
        int64_t vol = 0;
        int has_close = 0;
        const tr_close_auction_day_t *daily;
        tr_time_us_t midnight = 0;
        tr_time_us_t open_us;
        tr_candle_t bar;
        const tr_candle_t *src;
        tr_local_day_and_min(mins[i].open_time_us, 540, &day, &minute);
        j = i;
        while (j < nmin) {
            int64_t d = 0;
            uint32_t m = 0;
            tr_local_day_and_min(mins[j].open_time_us, 540, &d, &m);
            if (d != day) {
                break;
            }
            vol += mins[j].volume;
            if (m == TR_CLOSE_AUCTION_MIN) {
                has_close = 1;
            }
            j++;
        }
        src = &mins[j - 1];
        daily = find_day(days, ndays, day);
        tr_local_midnight(day, 540, &midnight);
        open_us = midnight + (tr_time_us_t)TR_CLOSE_AUCTION_MIN * TR_US_PER_MIN;
        if (!has_close && daily != 0 && daily->close > 0 && now_us >= open_us && nout < out_cap) {
            memset(&bar, 0, sizeof(bar));
            bar.instrument_id = src->instrument_id;
            bar.timeframe_sec = 60;
            bar.open_time_us = open_us;
            bar.close_time_us = open_us + TR_US_PER_MIN;
            bar.open = bar.high = bar.low = bar.close = daily->close;
            bar.volume = daily->volume > vol ? daily->volume - vol : 0;
            bar.state = TR_CANDLE_CLOSED;
            bar.source_id = src->source_id;
            out[nout++] = bar;
        }
        i = j;
    }
    return nout;
}
