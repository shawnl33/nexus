#ifndef TR_TICK_H
#define TR_TICK_H

/* 체결(Tick) 계약 (계획서 §6, §7)
 *
 * - 어댑터가 원본의 거래량 의미(개별 체결량/누적 거래량)를 구분해 volume_meaning에 기록한다.
 * - 원본에 안정적인 체결 ID가 없으면 source_exec_id=0으로 두고, 중복 제거 한계를 기록한다.
 */

#include <stdint.h>

#include "core/model/units.h"

typedef enum {
    TR_TICK_VOLUME_PER_TRADE = 0, /* 개별 체결량 */
    TR_TICK_VOLUME_CUMULATIVE = 1 /* 누적 거래량 (직전 값과의 차이가 체결량) */
} tr_tick_volume_meaning_t;

typedef struct {
    uint64_t instrument_id;
    tr_price_t price;              /* instrument price_scale 적용 */
    tr_qty_t qty;                  /* instrument qty_scale 적용 */
    uint64_t source_exec_id;       /* 원본 체결 식별 정보. 없으면 0 */
    tr_tick_volume_meaning_t volume_meaning;
} tr_tick_t;

#endif
