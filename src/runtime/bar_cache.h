#ifndef TR_BAR_CACHE_H
#define TR_BAR_CACHE_H

/* 증권사 조회 봉과 로컬 캐시 봉을 한 줄로 합친다.
 * 둘 다 open_time_us 오름차순이어야 한다.
 * 같은 시각은 revision이 큰 쪽, 같으면 증권사 봉.
 * 결과가 cap을 넘으면 최근 cap개만 남긴다. */

#include <stddef.h>

#include "core/market/candle.h"

size_t tr_bar_cache_merge(const tr_candle_t *broker, size_t nbroker,
                          const tr_candle_t *cache, size_t ncache,
                          tr_candle_t *out, size_t cap);

#endif
