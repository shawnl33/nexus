#ifndef TR_LS_CHART_H
#define TR_LS_CHART_H

/* LS 과거 분봉 차트 (계획서 §15, docs/ls_api_mapping.md §3).
 *
 * - t8412(주식 N분) / t8465(선물 N분)를 조회해 tr_candle_t 배열로 정규화한다.
 * - 페이지/연속 조회(cts_date/cts_time)를 지원하고, 중복·누락·정렬을 확인한다.
 * - 가격 스케일: 주식은 정수(Number, scale 1), 선물은 소수 문자열(0.01틱, scale 100).
 * - 차트 TR은 1 TPS — 연속 조회 시 최소 간격을 강제한다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adapters/ls/ls_auth.h"
#include "core/market/candle.h"

typedef enum {
    LS_CHART_STOCK_MIN = 0, /* t8412 */
    LS_CHART_FUT_MIN = 1    /* t8465 */
} ls_chart_kind_t;

typedef struct {
    size_t count;          /* out에 기록된 봉 수 */
    bool has_more;         /* 연속 조회 가능 여부 */
    char cts_date[9];      /* 다음 페이지 키 */
    char cts_time[11];
    int64_t session_open_min;  /* OutBlock s_time (분) */
    int64_t session_close_min; /* OutBlock e_time (분) */
} ls_chart_page_t;

/* 한 페이지 조회. out에는 open_time 오름차순으로 기록된다.
 * 반환: LS_HTTP_OK 외에 LS_CHART_EMPTY(데이터 없음, 오류 아님) 가능. */
#define LS_CHART_EMPTY 100

int ls_chart_fetch_minute(ls_auth_t *auth, ls_chart_kind_t kind, const char *shcode,
                          int32_t ncnt, int32_t qrycnt, const char *edate,
                          const char *cts_date, const char *cts_time,
                          uint64_t instrument_id, uint64_t source_id,
                          tr_candle_t *out, size_t out_cap, ls_chart_page_t *page,
                          char *errbuf, size_t errlen);

/* 응답 JSON 본문 파싱 (테스트 가능하도록 분리). */
int ls_chart_parse_page(const char *body, size_t body_len, ls_chart_kind_t kind,
                        uint64_t instrument_id, uint64_t source_id, uint32_t timeframe_sec,
                        tr_candle_t *out, size_t out_cap, ls_chart_page_t *page,
                        char *errbuf, size_t errlen);

#endif
