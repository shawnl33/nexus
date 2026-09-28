#ifndef TR_BAR_BUILDER_H
#define TR_BAR_BUILDER_H

/* 틱 → 봉 생성기 (계획서 §7)
 *
 * - 같은 봉의 새 입력은 OPEN 봉을 갱신하고, 경계를 넘으면 이전 봉을 한 번 확정한다.
 * - 확정 시점: 다음 봉 구간의 입력이 오거나, 논리적 타이머(on_timer)가 봉 종료/세션 폐장을 넘길 때.
 * - 중복 체결(source_exec_id 기준)은 두 번 반영하지 않는다. ID가 없는 입력(0)은 중복 제거가 불가함을 기록한다.
 * - 이미 확정된 봉에 대한 늦은 입력은 revision 증가 + CORRECTED 표시로 정정한다.
 * - 무거래 봉 채움 여부는 정책이다. 수신 누락(GAP)과 무거래(FILLED_EMPTY)를 구분한다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/market/candle.h"
#include "core/market/ring.h"
#include "core/market/session.h"
#include "core/market/tick.h"
#include "core/model/envelope.h"

typedef enum {
    TR_BB_ACCEPTED = 0,          /* 현재 봉에 반영 */
    TR_BB_ACCEPTED_NEW_BAR,      /* 이전 봉 확정 후 새 봉에 반영 */
    TR_BB_DUPLICATE,             /* 중복 체결로 무시 */
    TR_BB_LATE_CORRECTED,        /* 확정된 과거 봉을 정정 */
    TR_BB_LATE_DROPPED,          /* 너무 오래된 입력(버퍼 밖)이라 폐기 */
    TR_BB_REJECTED_OUT_OF_SESSION, /* 세션 밖 입력 */
    TR_BB_ERROR                  /* 잘못된 인자/종목 불일치 */
} tr_bb_status_t;

typedef enum {
    TR_NO_TRADE_SKIP = 0, /* 무거래 봉을 만들지 않음 */
    TR_NO_TRADE_FILL = 1  /* 전 종가·0 거래량 봉으로 채움 (TR_QUALITY_FILLED_EMPTY) */
} tr_no_trade_policy_t;

/* 봉 이벤트 콜백. kind는 TR_EVENT_CANDLE_UPDATE 또는 TR_EVENT_CANDLE_CLOSED. */
typedef void (*tr_bb_event_fn)(void *ctx, const tr_event_envelope_t *env, const tr_candle_t *bar);

typedef struct {
    uint64_t instrument_id;
    uint32_t timeframe_sec;
    tr_session_policy_t session;
    tr_no_trade_policy_t no_trade;
    tr_candle_t *ring_storage; /* 호출자 소유 */
    size_t ring_capacity;
    uint64_t engine_instance_id;
    uint64_t source_id;
    tr_bb_event_fn on_event;
    void *on_event_ctx;
} tr_bar_builder_config_t;

typedef struct {
    tr_bar_builder_config_t cfg;
    tr_ring bars;              /* tr_candle_t 링. 최신이 현재 또는 마지막 봉 */
    bool has_open;             /* 최신 봉이 OPEN 상태인가 */
    tr_price_t last_close;     /* 마지막 확정 종가 (FILL 정책용) */
    bool has_last_close;
    uint64_t last_source_exec_id;
    uint64_t next_sequence;
    /* 통계/관측 */
    uint64_t n_duplicates;
    uint64_t n_late_corrected;
    uint64_t n_late_dropped;
    uint64_t n_out_of_session;
    uint64_t n_filled_empty;
} tr_bar_builder_t;

bool tr_bar_builder_init(tr_bar_builder_t *bb, const tr_bar_builder_config_t *cfg);

tr_bb_status_t tr_bar_builder_on_tick(tr_bar_builder_t *bb, const tr_event_envelope_t *env, const tr_tick_t *tick);

/* 논리적 타이머. now가 봉 종료·세션 폐장을 넘기면 확정하고, 정책에 따라 무거래 봉을 채운다. */
void tr_bar_builder_on_timer(tr_bar_builder_t *bb, tr_time_us_t now_us);

/* 현재 OPEN 봉 조회. 없으면 NULL. */
const tr_candle_t *tr_bar_builder_current(const tr_bar_builder_t *bb);

/* 과거 확정 봉 직접 주입 (백필 경로). 실제 OHLC를 가진 봉을 시간 오름차순으로 넣는다.
 * - 종가 단일 틱 근사 재생 대신 봉 자체를 넣어 백필 캔들이 납작(O=H=L=C)해지는 것을 막는다.
 * - OPEN 봉이 있는 상태, 최신 봉보다 과거·중복 시각이면 거부한다.
 * - 주입 봉은 CLOSED로 기록하고 last_close를 갱신한다 (이후 FILL 정책과 호환). */
bool tr_bar_builder_inject_bar(tr_bar_builder_t *bb, tr_time_us_t event_time, const tr_candle_t *bar);

/* 과거 봉 조회. back_index 0 = 최신 봉(OPEN일 수 있음). */
bool tr_bar_builder_at(const tr_bar_builder_t *bb, size_t back_index, tr_candle_t *out);

#endif
