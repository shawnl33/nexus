#ifndef TR_BAR_FILL_H
#define TR_BAR_FILL_H

/* 백테스트 구간의 1분봉을 저장소에 채운다.
 * 옵션은 Theta Terminal 의 1분 OHLC. 선물은 NH 실전 chart-minute.
 * 지수 1분봉은 받지 않는다. 키는 요청에 붙이지 않는다.
 */

#include <stddef.h>
#include <stdint.h>

#include "adapters/storage/store.h"

#define TR_BAR_GET 0
#define TR_BAR_POST 1

/* 0 이면 resp 에 본문을 담았다. -1 이면 err 에 이유. */
typedef int (*tr_bar_fetch_fn)(void *ctx, int method, const char *url, const char *body,
                               char *resp, size_t resp_cap, char *err, size_t err_cap);

typedef struct tr_bar_fill_req {
    tr_product_type_t product;
    uint64_t instrument_id;
    int64_t price_scale;
    int64_t qty_scale;
    /* 옵션은 SPXW:20261009:8400:put. 선물은 ESZ26. */
    const char *code;
} tr_bar_fill_req_t;

/* start_us 이상 end_us 미만. 성공 시 0. 실패 시 -1 이고 err 에 이유. */
int tr_bar_fill(tr_store_t *store, const tr_bar_fill_req_t *req, int64_t start_us, int64_t end_us,
                tr_bar_fetch_fn fetch, void *ctx, char *err, size_t err_cap);

#endif
