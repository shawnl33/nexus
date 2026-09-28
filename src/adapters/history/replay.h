#ifndef TR_REPLAY_H
#define TR_REPLAY_H

/* 과거 입력 재생 (계획서 §4 adapters/history, §21 replay)
 *
 * - 기록된 틱을 기록된 논리 시간 순서대로 봉 빌더에 공급한다.
 * - 각 틱 전에 on_timer를 호출해 타이머 확정 경로도 동일하게 재생한다.
 * - 같은 입력은 항상 같은 출력을 만들어야 한다(테스트로 검증).
 * - 미래 데이터를 현재 값으로 노출하지 않는다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/market/bar_builder.h"
#include "core/market/tick.h"
#include "core/model/envelope.h"

typedef struct {
    tr_event_envelope_t env;
    tr_tick_t tick;
} tr_replay_tick_t;

typedef struct {
    size_t fed;              /* 공급된 틱 수 */
    size_t rejected;         /* 빌더가 거절한 틱 수 */
    size_t first_bad_index;  /* 순서 역전 등 첫 문제 인덱스. 없으면 (size_t)-1 */
} tr_replay_result_t;

/* events는 env.event_time_us 오름차순이어야 한다.
 * 마지막 틱 후 end_us로 on_timer를 한 번 호출해 잔여 봉을 확정한다. */
bool tr_replay_run(tr_bar_builder_t *bb, const tr_replay_tick_t *events, size_t n,
                   tr_time_us_t end_us, tr_replay_result_t *out);

#endif
