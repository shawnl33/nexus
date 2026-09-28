#ifndef TR_UNITS_H
#define TR_UNITS_H

/* 공통 값 계약 (계획서 §6)
 *
 * - 가격·수량·현금은 스케일된 정수(int64)로 표현한다. 스케일은 Instrument가 소유한다.
 * - 값의 부재와 숫자 0은 tr_validity_t로 구분한다.
 * - 지표 계산은 double을 사용하되, 주문 값으로 변환할 때 스케일·범위·반올림을 검사한다.
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TR_VALIDITY_VALID = 0,      /* 사용 가능한 값 */
    TR_VALIDITY_MISSING,        /* 값이 없음 (0이 아님) */
    TR_VALIDITY_UNSUPPORTED,    /* 이 상품/기능 조합에서는 지원되지 않음 */
    TR_VALIDITY_STALE,          /* 값은 있으나 신선하지 않음 */
    TR_VALIDITY_MISSING_DEPENDENCY /* 원본 미제공 등 의존성 부재 (계획서 §10.2) */
} tr_validity_t;

/* 스케일된 정수 표현의 raw 값. 실제 값 = raw / scale.
 * scale은 Instrument.price_scale / qty_scale이 정한다. */
typedef int64_t tr_price_t;
typedef int64_t tr_qty_t;

/* scale은 10의 거듭제곱(1, 10, 100, ...)이어야 한다. */
bool tr_scale_is_valid(int64_t scale);

/* raw/scale을 double로 변환한다. scale이 유효하지 않으면 false. */
bool tr_price_to_double_checked(tr_price_t raw, int64_t scale, double *out);

#endif
