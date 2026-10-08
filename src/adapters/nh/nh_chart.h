#ifndef NH_CHART_H
#define NH_CHART_H

/* 해외 1분봉. POST /trade/v1/overseas/chart-minute (2026-10-08 실측). */

#include <stddef.h>
#include <stdint.h>

#include "core/market/candle.h"

#include "adapters/nh/nh_auth.h"

/* O_SPW/O_SPX/O_NDX 는 OCBO, CBOE 변동성 선물은 FCBO, 분봉이 열린 CME 품목은 FCME. */
const char *nh_exch_for_symbol(const char *sym);
/* 해외 틱. 포인트 ×100. 거래소가 없으면 0. */
double nh_tick_raw(const char *sym);

/* 응답 JSON을 시간 오름차순 봉으로 푼다. 시각은 KST. 수량은 누적값의 증가분이고
 * 페이지에서 가장 오래된 봉은 0이다. 0이면 성공. */
int nh_chart_parse_minutes(const char *json, size_t len, uint64_t instrument_id,
                           tr_candle_t *out, size_t cap, size_t *out_count, char *nxt_key,
                           size_t nxt_cap);

/* dcnt 1. req_qty는 서버 상한 9999 이하. nxt_in은 빈 문자열로 시작한다. */
int nh_chart_fetch_minutes(nh_auth_t *auth, const char *sym, const char *exch, int req_qty,
                           const char *nxt_in, uint64_t instrument_id, tr_candle_t *out,
                           size_t cap, size_t *out_count, char *nxt_out, size_t nxt_cap,
                           char *err, size_t err_cap);

#endif
