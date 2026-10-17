#ifndef TR_CLOSE_AUCTION_H
#define TR_CLOSE_AUCTION_H

/* 코스피200 선물·옵션 마감 단일가. 접속매매 분봉은 15:35에서 끝나고,
 * 15:45 체결은 분봉 TR에 없다. 일봉 종가로 그 시각 봉 하나만 만든다.
 */

#include <stddef.h>
#include <stdint.h>

#include "core/market/candle.h"

typedef struct {
    int64_t day; /* KST days since epoch */
    int64_t close; /* raw, 분봉과 같은 스케일 */
    int64_t volume;
} tr_close_auction_day_t;

/* mins는 시각 오름차순. 이미 15:45 봉이 있거나, 일봉이 없거나,
 * 그 날 15:45가 아직 안 됐으면 만들지 않는다.
 * 거래량은 일봉 거래량에서 그날 분봉 합을 뺀 값이다. 음수면 0.
 * 반환은 out에 쓴 봉 수. */
size_t tr_fut_close_auction_bars(const tr_candle_t *mins, size_t nmin,
                                 const tr_close_auction_day_t *days, size_t ndays,
                                 tr_time_us_t now_us, tr_candle_t *out, size_t out_cap);

#endif
