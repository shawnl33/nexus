#ifndef TR_STORE_H
#define TR_STORE_H

/* 저장소 어댑터 (계획서 §4, §20).
 *
 * - 거래·시장 기록의 쓰기 소유자는 C다. Node의 이력 조회는 관리 API 또는 읽기 전용 연결을 쓴다.
 * - SQLite WAL + synchronous=FULL (거래 기록 내구성 우선).
 * - 스키마는 버전 마이그레이션으로 관리한다 (schema_migrations).
 * - 체결·주문 기록은 일관된 트랜잭션으로 기록한다.
 * - 실패 시 부분 기록을 남기지 않기 위해 쓰기 묶음은 단일 트랜잭션을 사용한다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/market/candle.h"
#include "core/model/instrument.h"

typedef struct tr_store tr_store_t;

typedef enum {
    TR_STORE_OK = 0,
    TR_STORE_ERR,
    TR_STORE_NOT_FOUND,
    TR_STORE_DUPLICATE,  /* 유니크 제약 중복 (체결 등) */
    TR_STORE_CONFLICT    /* 같은 ID·다른 내용 (명령 등) */
} tr_store_rc_t;

/* ---------- 명령 중복 검사 ---------- */

typedef enum {
    TR_STORE_CMD_NEW = 0,   /* 새 명령: 기록됨 */
    TR_STORE_CMD_EXISTS,    /* 같은 ID·같은 내용: 기존 결과 재사용 */
    TR_STORE_CMD_CONFLICT   /* 같은 ID·다른 내용: 거절 */
} tr_store_cmd_check_t;

/* ---------- 주문/체결 기록 ---------- */

typedef struct {
    const char *order_id;       /* 낶부 주문 ID (제출 전 생성) */
    const char *run_id;         /* strategy_runs.run_id, 없으면 NULL */
    const char *account_id;
    uint64_t instrument_id;
    int side;                   /* 1=매수, -1=매도 */
    int type;                   /* 주문 종류 (정의는 order 계층) */
    int64_t quantity;           /* 스케일된 정수 */
    int64_t price;              /* 지정가. 시장가 등은 0 */
    int state;                  /* 주문 생명 상태 (계획서 §14) */
    int64_t created_at_us;
} tr_store_order_t;

typedef struct {
    const char *order_id;
    uint64_t engine_sequence;
    int event_type;             /* 정규화 이벤트 종류 */
    uint64_t source_id;
    int64_t event_time_us;
    int64_t received_time_us;
    const char *payload;        /* JSON 등 상세 (NULL 가능) */
} tr_store_order_event_t;

typedef struct {
    const char *order_id;
    const char *account_id;
    uint64_t instrument_id;
    int64_t quantity;
    int64_t price;
    int64_t fees;
    const char *source_execution_id; /* 브로커 체결 ID (중복 제거 근거) */
    int64_t executed_at_us;
} tr_store_fill_t;

/* ---------- 수명 ---------- */

/* path에 ":memory:" 를 주면 인메모리 DB (테스트용). */
tr_store_rc_t tr_store_open(const char *path, tr_store_t **out, char *errbuf, size_t errlen);
void tr_store_close(tr_store_t *s);

/* 마이그레이션 적용. 이미 최신이면 no-op. */
tr_store_rc_t tr_store_migrate(tr_store_t *s);

/* ---------- 종목 ---------- */

tr_store_rc_t tr_store_put_instrument(tr_store_t *s, const tr_instrument_t *ins);
tr_store_rc_t tr_store_get_instrument(tr_store_t *s, uint64_t instrument_id, tr_instrument_t *out);

/* ---------- 봉 ---------- */

/* 종목+주기+시작+출처를 고유키로, 더 큰 revision만 반영한다. */
tr_store_rc_t tr_store_put_candle(tr_store_t *s, const tr_candle_t *c);

/* from_us <= open_time < to_us, 시작 시각 오름차순, 최대 limit개. 반환값은 행 수, 실패 시 -1. */
typedef void (*tr_store_candle_fn)(void *ctx, const tr_candle_t *c);
int tr_store_query_candles(tr_store_t *s, uint64_t instrument_id, uint32_t timeframe,
                           int64_t from_us, int64_t to_us, size_t limit,
                           tr_store_candle_fn cb, void *ctx);

/* ---------- 명령 ---------- */

tr_store_cmd_check_t tr_store_command_check(tr_store_t *s, const char *command_id,
                                            const char *payload_hash, int command_type,
                                            int64_t created_at_us);
tr_store_rc_t tr_store_command_mark(tr_store_t *s, const char *command_id,
                                    int state, const char *result, int64_t applied_at_us);

/* ---------- 주문·체결 ---------- */

tr_store_rc_t tr_store_order_insert(tr_store_t *s, const tr_store_order_t *o);
tr_store_rc_t tr_store_order_add_event(tr_store_t *s, const tr_store_order_event_t *ev);

/* 체결 기록: fills + order_events + orders 갱신을 **하나의 트랜잭션**으로 기록한다.
 * source_execution_id가 이미 있으면 TR_STORE_DUPLICATE를 반환하고 아무것도 기록하지 않는다. */
tr_store_rc_t tr_store_record_fill(tr_store_t *s, const tr_store_fill_t *fill,
                                   int64_t new_cumulative_fill, int new_order_state,
                                   const tr_store_order_event_t *ev);

/* ---------- 조회 (CLI/관리용) ---------- */

typedef void (*tr_store_order_fn)(void *ctx, const tr_store_order_t *o, const char *broker_id,
                                  int64_t cumulative_fill, int64_t updated_at_us);
int tr_store_query_orders(tr_store_t *s, const char *account_id, size_t limit,
                          tr_store_order_fn cb, void *ctx);

#endif
