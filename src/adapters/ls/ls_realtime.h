#ifndef TR_LS_REALTIME_H
#define TR_LS_REALTIME_H

/* LS 실시간 WebSocket 어댑터 (계획서 §15, docs/ls_api_mapping.md §4).
 *
 * - libwebsockets 클라이언트. 엔진 루프에서 tr_ls_rt_service로 구동한다 (콜백은 결과 이벤트만 큐에 넣는다).
 * - 구독: S3_/K3_(체결), H1_/HA_(호가잔량), FC9/FH9(코스피200선물 체결/호가).
 * - 재연결 시 재인증(토큰 갱신)·재구독한다. 단, 구독 복원만으로 데이터 연속성이 회복됐다고
 *   선언하지 않는다 — 공백 감지는 순번·시각과 별도 보충 조회(과거 데이터)로 처리한다.
 * - 입력 큐에는 상한이 있고 포화 횟수를 노출한다 (계획서 §5). 조용히 버리지 않는다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adapters/ls/ls_auth.h"
#include "core/market/tick.h"
#include "core/model/envelope.h"

typedef enum {
    LS_RT_CONNECTING = 0,
    LS_RT_READY,
    LS_RT_RECONNECTING,
    LS_RT_FAILED
} ls_rt_state_t;

typedef enum {
    LS_RT_TICK = 0,
    LS_RT_ORDERBOOK
} ls_rt_kind_t;

typedef struct {
    tr_price_t price;
    int64_t qty;
} ls_rt_level_t;

typedef struct {
    ls_rt_kind_t kind;
    char tr_cd[8];
    uint64_t instrument_id;
    int64_t event_time_us;   /* 메시지 체결 시각. 없으면 수신 시각 */
    int64_t recv_time_us;
    tr_tick_volume_meaning_t volume_meaning;
    /* TICK */
    tr_price_t price;
    tr_qty_t qty;
    /* ORDERBOOK (호가 총잔량, Bids/Asks 대응) */
    int64_t bid_total;
    int64_t ask_total;
    ls_rt_level_t levels[10]; /* 우선 호가: [0..4]=매수, [5..9]=매도 (없을 수 있음) */
    int level_count;
} ls_rt_event_t;

typedef struct {
    const char *url;               /* NULL이면 기본 wss://openapi.ls-sec.co.kr:9443/websocket */
    ls_auth_t *auth;
    int64_t (*now_us_fn)(void);    /* NULL이면 시스템 시계 */
    size_t queue_capacity;
    int reconnect_min_ms;
    int reconnect_max_ms;
} ls_rt_config_t;

typedef struct tr_ls_rt tr_ls_rt_t;

tr_ls_rt_t *tr_ls_rt_open(const ls_rt_config_t *cfg, char *errbuf, size_t errlen);
void tr_ls_rt_close(tr_ls_rt_t *rt);

/* 구독 등록. instrument_id는 이후 이벤트에 태깅된다. 재연결 시 자동 재구독된다. */
bool tr_ls_rt_subscribe(tr_ls_rt_t *rt, const char *tr_cd, const char *tr_key, uint64_t instrument_id);

/* 구독 해지 (tr_type "4" = 실시간 시세 해제, 공식 명세). 재연결 시에도 복원되지 않는다. */
bool tr_ls_rt_unsubscribe(tr_ls_rt_t *rt, const char *tr_cd, const char *tr_key);

/* 이벤트 루프 구동. timeout_ms 동안 대기할 수 있다. */
int tr_ls_rt_service(tr_ls_rt_t *rt, int timeout_ms);

/* 큐에서 다음 이벤트를 꺼낸다. 없으면 false. */
bool tr_ls_rt_next(tr_ls_rt_t *rt, ls_rt_event_t *out);

ls_rt_state_t tr_ls_rt_state(tr_ls_rt_t *rt);
uint64_t tr_ls_rt_queue_dropped(tr_ls_rt_t *rt);
uint64_t tr_ls_rt_reconnect_count(tr_ls_rt_t *rt);

/* 실시간 메시지 파싱 (테스트 가능하도록 분리). instrument_id는 구독 맵 조회 결과. */
bool tr_ls_rt_parse_message(const char *body, size_t len, uint64_t instrument_id,
                            int64_t recv_time_us, ls_rt_event_t *out);

#endif
