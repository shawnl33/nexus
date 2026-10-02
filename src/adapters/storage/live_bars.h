#ifndef TR_LIVE_BARS_H
#define TR_LIVE_BARS_H

/* 라이브 엔진이 확정 1분봉을 남기고 다시 읽는 입구.
 * store.h를 포함하지 않는다. 그 헤더는 종목 시장 열거와 지표 마켓 구조체 이름이 겹친다. */

#include <stddef.h>
#include <stdint.h>

#include "core/market/candle.h"

/* 0이면 열림. 실패해도 이후 put/load는 아무것도 하지 않는다. */
int tr_live_bars_open(const char *path, char *err, size_t errlen);
void tr_live_bars_close(void);
void tr_live_bars_put(const tr_candle_t *bar);
size_t tr_live_bars_load(uint64_t instrument_id, uint32_t timeframe, tr_candle_t *out, size_t cap);

#endif
