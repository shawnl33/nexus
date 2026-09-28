#ifndef TR_RING_H
#define TR_RING_H

/* 고정 용량 Ring Buffer (계획서 §7)
 *
 * - 최근 고정 개수의 요소를 유지한다. 가득 차면 가장 오래된 요소를 덮어쓴다.
 * - 인덱스는 최신 기준 상대값이다: 0 = 최신, 1 = 그 이전, ...
 *   (최신 확정 봉 / 현재 OPEN 봉 / 과거 확정 봉의 구분은 상위 계층이 이 인덱스로 표현)
 * - update_newest는 최신 슬롯을 제자리에서 교체한다.
 *   한 봉의 반복 갱신으로 과거 봉이 밀리지 않게 하기 위한 연산이다.
 * - 저장소는 호출자가 소유한다. 버퍼는 요소를 값으로 복사한다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *storage;         /* 호출자 소유, capacity × elem_size 바이트 */
    size_t elem_size;
    size_t capacity;       /* 0보다 커야 함 */
    size_t count;          /* 현재 유효 개수, <= capacity */
    size_t head;           /* 가장 오래된 요소의 물리 인덱스 */
    uint64_t total_pushed; /* 누적 push 횟수 (덮어쓰기 포함) */
} tr_ring;

/* storage/capacity가 유효하지 않으면 false. */
bool tr_ring_init(tr_ring *rb, void *storage, size_t elem_size, size_t capacity);

/* 요소를 최신 슬롯에 추가한다. 가장 오래된 요소가 밀려났으면 true. */
bool tr_ring_push(tr_ring *rb, const void *elem);

/* 최신 슬롯을 제자리에서 교체한다. 버퍼가 비어 있으면 false. */
bool tr_ring_update_newest(tr_ring *rb, const void *elem);

/* back_index(0=최신)의 요소를 out으로 복사한다. 범위 초과 시 false. */
bool tr_ring_at(const tr_ring *rb, size_t back_index, void *out);

/* back_index(0=최신)의 요소에 대한 가변 포인터. 범위 초과 시 NULL.
 * 버퍼를 다시 push/update하기 전까지만 유효하다. */
void *tr_ring_get_mut(tr_ring *rb, size_t back_index);

size_t tr_ring_count(const tr_ring *rb);
bool tr_ring_is_full(const tr_ring *rb);
void tr_ring_clear(tr_ring *rb);

#endif
