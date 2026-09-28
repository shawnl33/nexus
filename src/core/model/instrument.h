#ifndef TR_INSTRUMENT_H
#define TR_INSTRUMENT_H

/* 종목 계약 (계획서 §6)
 *
 * - 낶부 ID가 기본 식별자이며, 브로커 코드는 매핑 정보다.
 * - price_scale/qty_scale/multiplier를 통해 단위를 명시한다.
 * - 거래 불가 종목(지수 등)은 tradable=false로 명시하고 주문 경로에서 차단한다.
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TR_MARKET_UNKNOWN = 0,
    TR_MARKET_KRX,
    TR_MARKET_KOSDAQ,
    TR_MARKET_KRX_DERIVATIVES,
    TR_MARKET_INDEX
} tr_market_t;

typedef enum {
    TR_PRODUCT_UNKNOWN = 0,
    TR_PRODUCT_STOCK,
    TR_PRODUCT_INDEX,
    TR_PRODUCT_FUTURES,
    TR_PRODUCT_OPTIONS
} tr_product_type_t;

#define TR_BROKER_CODE_MAX 24
#define TR_CURRENCY_LEN 4 /* ISO 4217, 예: "KRW" */

typedef struct {
    uint64_t instrument_id;            /* 낶부 기본키, 0은 무효 */
    char broker_code[TR_BROKER_CODE_MAX]; /* LS 등 브로커 종목코드, NUL 종료 */
    tr_market_t market;
    tr_product_type_t product_type;
    bool tradable;                     /* 지수 등 거래 불가 상품은 false */
    int64_t price_scale;               /* raw price = 실제가격 × price_scale, 10^k */
    int64_t qty_scale;                 /* raw qty   = 실제수량 × qty_scale, 10^k */
    int64_t multiplier;                /* 계약 승수. 주식은 1 */
    char currency[TR_CURRENCY_LEN];
    uint32_t session_policy_id;        /* 세션 정책 참조 (계획서 §8) */
    uint64_t metadata_version;         /* 메타데이터 변경 버전 */
} tr_instrument_t;

/* 필수 필드와 단위의 유효성을 검사한다. 유효하면 true. */
bool tr_instrument_validate(const tr_instrument_t *ins);

#endif
