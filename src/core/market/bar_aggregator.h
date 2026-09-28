#ifndef TR_BAR_AGGREGATOR_H
#define TR_BAR_AGGREGATOR_H

/* 하위 봉 → 상위 봉 집계기 (계획서 §8)
 *
 * - 확정된 하위 봉만 소비한다. 미확정 하위 봉·미래 데이터는 절대 반영되지 않는다.
 * - 정렬 기준은 UTC 나머지가 아니라 세션 개장 시각이다.
 * - 일봉(TR_TF_DAY)은 트레이딩 데이(세션 개장일) 단위로 묶는다. 야간장에서 자정이 바뀌어도 같은 일봉이다.
 * - 진행 중인 상위 봉은 OPEN 상태로 조회되며, 전략 사용 여부는 상위 계층 설정이다.
 * - 하위 봉은 시간 순서대로 전달되어야 한다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/market/candle.h"
#include "core/market/session.h"
#include "core/model/envelope.h"

typedef void (*tr_agg_event_fn)(void *ctx, const tr_event_envelope_t *env, const tr_candle_t *bar);

typedef struct {
    uint64_t instrument_id;
    uint32_t lower_tf_sec;
    uint32_t higher_tf_sec;   /* TR_TF_DAY 가능 */
    tr_session_policy_t session;
    uint64_t engine_instance_id;
    uint64_t source_id;
    tr_agg_event_fn on_event;
    void *on_event_ctx;
} tr_bar_aggregator_config_t;

typedef struct {
    tr_bar_aggregator_config_t cfg;
    bool has_open;
    tr_candle_t current;      /* 진행 중인 상위 봉 (OPEN) */
    uint64_t next_sequence;
    uint64_t n_closed;
} tr_bar_aggregator_t;

bool tr_bar_aggregator_init(tr_bar_aggregator_t *agg, const tr_bar_aggregator_config_t *cfg);

/* 확정된 하위 봉을 반영한다. state가 CLOSED가 아니면 false. */
bool tr_bar_aggregator_on_lower_closed(tr_bar_aggregator_t *agg, const tr_event_envelope_t *env, const tr_candle_t *lower);

/* 진행 중인 상위 봉(미확정) 조회. 없으면 NULL. */
const tr_candle_t *tr_bar_aggregator_current(const tr_bar_aggregator_t *agg);

#endif
