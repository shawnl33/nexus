#ifndef TR_TIME_US_H
#define TR_TIME_US_H

/* 시간 계약 (계획서 §6)
 *
 * - 기본 저장은 UTC epoch 마이크로초의 int64다.
 * - 타임아웃·경과 시간은 단조 시계(monotonic)로 측정하며, 이 타입과 혼용하지 않는다.
 * - 전략은 주입받은 논리적 시간만 사용하고 시스템 현재 시간을 직접 조회하지 않는다.
 * - 원본 시간 정밀도가 마이크로초보다 낮으면 그 사실을 품질 정보에 보존한다.
 */

#include <stdbool.h>
#include <stdint.h>

/* UTC epoch 마이크로초. */
typedef int64_t tr_time_us_t;

/* t + delta_us를 overflow 검사와 함께 계산한다. overflow/underflow 시 false. */
bool tr_time_us_add(tr_time_us_t t, int64_t delta_us, tr_time_us_t *out);

#endif
