#ifndef TR_CSV_TICKS_H
#define TR_CSV_TICKS_H

/* CSV 틱 파일 로더 (replay 입력, 계획서 §21).
 *
 * 형식: 한 줄에 `epoch_us,price,qty` (정수). `#`으로 시작하는 줄과 빈 줄은 무시한다.
 * epoch_us는 UTC epoch 마이크로초. 가격·수량은 스케일된 정수(raw).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adapters/history/replay.h"

/* 파일 전체를 읽어 배열로 반환한다. 호출자가 tr_csv_ticks_free로 해제한다.
 * source_exec_id는 파일 내 행 번호(1부터)로 부여한다. 실패 시 0. */
tr_replay_tick_t *tr_csv_ticks_load(const char *path, uint64_t instrument_id, size_t *out_count);

void tr_csv_ticks_free(tr_replay_tick_t *ticks);

#endif
