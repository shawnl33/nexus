#ifndef TR_SERIES_H
#define TR_SERIES_H

/* yl_series — 예스랭귀지 변수 1:1 대응 시계열 타입 (docs/PORTING.md "암묵 시계열 원칙")
 *
 * 예스랭귀지는 모든 변수가 암묵적 시계열이다: 매 봉 평가될 때마다 값이 하나씩
 * 쌓이고, x[N]으로 N봉 전 값을 참조한다 ([0] = 현재 봉).
 *
 * 예스랭귀지 ↔ C 대응표:
 *   예스랭귀지                    | C (yl_series)
 *   -----------------------------+------------------------------------------------
 *   var x; (변수 선언)           | YL_SERIES_STORAGE(x, cap); yls_init(&x, x_buf, cap);
 *   x = expr; (대입, 매 봉 1회)  | yls_push(&x, expr);
 *   x = expr; (같은 봉 내 재대입)| yls_set_current(&x, expr);
 *   x[N] (참조, [0]=현재)        | yls_at(&x, N, &out)  — 범위 초과 시 false
 *
 * 구현은 검증된 tr_ring(src/core/market/ring.h)을 double 전용으로 감싼 얇은
 * 래퍼이며, ring의 불변식을 그대로 따른다 (새 불변식을 만들지 않는다):
 * - 인덱스는 최신 기준 상대값이다: [0] = 최신, [1] = 1봉 전, ...
 * - 가득 차면 가장 오래된 값을 덮어쓴다 (유한 이력).
 * - set_current는 최신 슬롯을 제자리에서 교체한다 — 같은 봉의 반복 대입으로
 *   과거 봉이 밀리지 않는다.
 * - 저장소는 호출자가 소유한다 (상태 구조체 안에 버퍼를 두는 기존 패턴).
 *
 * 값 복사 불안전: 저장소 버퍼를 상태 구조체 안에 두는 관계로 ring.storage는
 * 자기참조 포인터다. yl_series를 포함한 구조체를 통째로 값 복사(이식 등)하면
 * 사본의 ring.storage는 원본의 버퍼를 가리키는 채로 남는다. 통째 복사 후에는
 * 반드시 yls_relink로 사본 자신의 버퍼로 다시 연결해야 한다 (docs/PORTING.md
 * "암묵 시계열 원칙"의 값 복사 제약 참조).
 */

#include <stdbool.h>
#include <stddef.h>

#include "core/market/ring.h"

typedef struct {
    tr_ring ring;
} yl_series;

/* 변수 저장소 선언. YL_SERIES_STORAGE(x, cap)는 x_buf[cap]을 선언한다. */
#define YL_SERIES_STORAGE(name, cap) double name##_buf[(cap)]

/* storage/cap이 유효하지 않으면 false. 저장소는 호출자 소유. */
bool yls_init(yl_series *s, double *storage, size_t cap);

/* 대입문: x = expr (매 봉 1회). 가장 오래된 값이 밀려났으면 true. */
bool yls_push(yl_series *s, double v);

/* 봉 내 갱신: 현재([0]) 값을 제자리에서 교체한다. 비어 있으면 false. */
bool yls_set_current(yl_series *s, double v);

/* 참조: x[N], [0]=현재. 범위 초과 시 false. */
bool yls_at(const yl_series *s, size_t n, double *out);

/* 쌓인 봉 수 (<= cap). */
size_t yls_count(const yl_series *s);

/* 통째 값 복사(이식) 후 저장소 포인터만 new_storage로 다시 연결한다.
 * 봉 이력(count/head/total_pushed)은 값 복사로 이미 따라왔으므로 그대로 둔다.
 * s/new_storage가 유효하지 않으면 false. */
bool yls_relink(yl_series *s, double *new_storage);

void yls_clear(yl_series *s);

#endif
