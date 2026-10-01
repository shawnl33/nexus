#ifndef TR_LS_REALTIME_H
#define TR_LS_REALTIME_H

/* LS 실시간 WebSocket 어댑터 (계획서 §15, docs/ls_api_mapping.md §4).
 *
 * - libwebsockets 클라이언트. 엔진 루프에서 tr_ls_rt_service로 구동한다 (콜백은 결과 이벤트만 큐에 넣는다).
 * - 구독: S3_/K3_(체결), H1_/HA_(호가잔량), FC9/FH9(코스피200선물 체결/호가).
 * - 재연결 시 재인증(토큰 갱신)·재구독한다. 단, 구독 복원만으로 데이터 연속성이 회복됐다고
 *   선언하지 않는다 — 공백 감지는 순번·시각과 별도 보충 조회(과거 데이터)로 처리한다.
 * - 연속 단기 세션(LS_RT_REAUTH_THRESHOLD회)은 서버 측 토큰 무효화로 보고 토큰을
 *   강제 재발급한다 (ls_auth_ensure는 명목 유효기간만 보기 때문). 발급 API 해머 방지로
 *   쿨다운(LS_RT_REAUTH_COOLDOWN_US)을 둔다.
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

/* 이 시간(ms) 이상 유지된 세션만 건강한 세션으로 본다. */
#define LS_RT_HEALTHY_MS 10000

/* 재접속 대기 시간 결정 (테스트 가능하도록 분리, 순수 함수).
 * survived_ms: 수립된 세션의 유지 시간(ms), 수립 없이 실패했으면 -1.
 * 건강한 세션(>= LS_RT_HEALTHY_MS)이면 min_ms로 리셋하고(빠른 복구),
 * 아니면 current_retry_ms를 유지한다 — 호출자가 지수 백오프로 증가시킨다. */
int ls_rt_next_retry_ms(int64_t survived_ms, int current_retry_ms, int min_ms, int max_ms);

/* 연속 단기 세션이 이 횟수에 도달하면 토큰 무효화 가능성으로 보고 강제 재발급을 시도한다.
 * (2026-10-01 사건: 서버 측 토큰 무효화 후 명목 유효기간이 남은 캐시 토큰으로
 *  28분간 1600회 이상 단기 세션이 반복됐다.) */
#define LS_RT_REAUTH_THRESHOLD 3

/* 강제 재발급 쿨다운(µs): 마지막 시도 후 이 시간이 지나기 전에는 다시 시도하지 않는다.
 * 서버 거절 원인이 토큰이 아닐 때 토큰 발급 API를 두드리는 루프를 막기 위함이다. */
#define LS_RT_REAUTH_COOLDOWN_US (60LL * 1000000LL)

/* 세션 종료 시 연속 단기 세션 카운터 갱신 (테스트 가능하도록 분리, 순수 로직).
 * healthy(건강한 세션 이후 단절)면 0으로 리셋, 아니면(단기 세션·수립 실패) +1. */
void ls_rt_note_session_end(bool healthy, int *consec_short);

/* 강제 재발급 시도 여부 판정 (테스트 가능하도록 분리, 순수 함수).
 * 카운터가 LS_RT_REAUTH_THRESHOLD에 도달했고 마지막 시도 이후 쿨다운이 지났으면 true.
 * last_reauth_us가 0이면 아직 시도한 적 없음(즉시 허용). */
bool ls_rt_should_reauth(int consec_short, int64_t last_reauth_us, int64_t now_us);

/* 구독 ACK의 거절 판정 (테스트 가능하도록 분리, 순수 함수).
 * header.rsp_cd가 문자열로 존재하고 "00000"이 아니면 true (정상 ACK는 false). */
bool ls_rt_sub_ack_rejected(const char *body, size_t len);

#endif
