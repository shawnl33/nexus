#ifndef TR_SESSION_H
#define TR_SESSION_H

/* 거래 세션 계약 (계획서 §8)
 *
 * - 봉 정렬 기준은 단순 UTC 나머지가 아니라 세션 시작점이다.
 * - 야간장(close_min <= open_min, 익일 폐장)·휴장·조기 종료·날짜 변경을 표현한다.
 * - '트레이딩 데이'는 세션 '개장'이 속한 현지 날짜다. 야간장에서 자정을 넘겨도 같은 트레이딩 데이다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/model/time_us.h"

/* days_mask 비트: bit0=일요일 .. bit6=토요일 (세션이 '개장'하는 요일) */
#define TR_SESSION_SUN ((uint8_t)(1u << 0))
#define TR_SESSION_MON ((uint8_t)(1u << 1))
#define TR_SESSION_TUE ((uint8_t)(1u << 2))
#define TR_SESSION_WED ((uint8_t)(1u << 3))
#define TR_SESSION_THU ((uint8_t)(1u << 4))
#define TR_SESSION_FRI ((uint8_t)(1u << 5))
#define TR_SESSION_SAT ((uint8_t)(1u << 6))
#define TR_SESSION_WEEKDAYS ((uint8_t)(TR_SESSION_MON | TR_SESSION_TUE | TR_SESSION_WED | TR_SESSION_THU | TR_SESSION_FRI))
#define TR_SESSION_EVERYDAY ((uint8_t)0x7F)

typedef struct {
    int32_t utc_offset_min; /* 거래소 현지 UTC 오프셋(분). 예: KST = 540 */
    uint32_t open_min;      /* 현지 자정 기준 개장 시각(분). 예: 09:00 = 540 */
    uint32_t close_min;     /* 폐장 시각(분). close_min <= open_min 이면 익일 폐장(야간장) */
    uint8_t days_mask;      /* 개장 요일 비트마스크 */
} tr_session_policy_t;

bool tr_session_policy_validate(const tr_session_policy_t *p);

/* t(UTC)가 세션 안이면 true를 반환하고, 그 세션의 개장·폐장 시각(UTC µs)을 돌려준다. */
bool tr_session_span(const tr_session_policy_t *p, tr_time_us_t t,
                     tr_time_us_t *out_open_us, tr_time_us_t *out_close_us);

/* t가 세션 안이면 true. */
bool tr_session_is_open(const tr_session_policy_t *p, tr_time_us_t t);

/* 트레이딩 데이(개장일 현지 날짜의 days since epoch) 반환. 세션 밖이면 false. */
bool tr_session_trading_day(const tr_session_policy_t *p, tr_time_us_t t, int64_t *out_open_day);

#endif
