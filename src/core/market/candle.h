#ifndef TR_CANDLE_H
#define TR_CANDLE_H

/* 봉 계약 (계획서 §6, §7)
 *
 * - 봉은 OPEN(진행) 또는 CLOSED(확정) 상태를 가진다.
 * - 가격·거래량은 Instrument의 스케일을 따르는 정수다.
 * - 늦은 입력·정정은 revision과 품질 플래그로 기록한다.
 */

#include <stdint.h>

#include "core/model/envelope.h"
#include "core/model/time_us.h"
#include "core/model/units.h"

typedef enum {
    TR_CANDLE_OPEN = 0,   /* 진행 중. 새 입력으로 갱신될 수 있다 */
    TR_CANDLE_CLOSED = 1  /* 확정. 이후 변경은 revision 증가와 함께 정정으로 처리 */
} tr_candle_state_t;

/* 일봉 주기. 초 단위가 아니라 트레이딩 데이(세션 개장일) 정렬을 의미한다 (계획서 §8). */
#define TR_TF_DAY UINT32_C(0xFFFFFFFF)

typedef struct {
    uint64_t instrument_id;
    uint32_t timeframe_sec;       /* 주기(초). 60=1분봉 */
    tr_time_us_t open_time_us;    /* 봉 시작 시각 (UTC) */
    tr_time_us_t close_time_us;   /* 봉 종료 시각 (UTC) */
    tr_price_t open, high, low, close; /* instrument price_scale 적용 */
    tr_qty_t volume;              /* instrument qty_scale 적용 */
    tr_candle_state_t state;
    uint32_t revision;            /* 정정 횟수. 최초 0 */
    uint64_t source_id;           /* 입력 출처 */
    tr_quality_flags_t quality;
} tr_candle_t;

/* OHLC 관계와 시간 순서의 불변식을 검사한다. 유효하면 true. */
bool tr_candle_is_consistent(const tr_candle_t *c);

#endif
