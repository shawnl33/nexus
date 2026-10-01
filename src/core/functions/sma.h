#ifndef TR_SMA_H
#define TR_SMA_H

/* SMA (단순이동평균). 종가 기준.
 *
 * 갱신 계약 (진행 중 봉 포함 "현재 값"):
 * - is_new_bar=true  : 새 슬롯 push. 창이 가득 차면(count == period) 가장 오래된 값이 빠진다.
 * - is_new_bar=false : 진행 봉 재호출 — 현재(가장 최근) 슬롯만 덮어쓴다.
 *                      창의 크기와 나머지 원소는 변하지 않는다 (창 오염 없음).
 * - count >= period이면 valid=true, value = 최근 period개 종가 평균.
 *   워밍업 중에는 valid=false, value=0을 유지한다.
 *
 * 종가 창은 yl_var로 구현한다 (docs/PORTING.md 암묵 시계열 원칙):
 * 저장소는 이 구조체 안에 두고, 유효 용량은 period다.
 */

#include <stdbool.h>

#include "core/market/var.h"

typedef struct {
    int period;
    double buf[64];      /* win의 저장소 (yl_var 규약: 상태 구조체 안) */
    yl_var win;          /* 종가 창 ([0]=최신, 유효 용량 period) */
    double value;
    bool valid;
} tr_sma_t;

/* period는 1~64로 클램프한다. */
void tr_sma_init(tr_sma_t *s, int period);

void tr_sma_on_bar(tr_sma_t *s, double close, bool is_new_bar);

/* tr_sma_t를 포함한 구조체의 통째 값 복사(이식) 후 호출: 창 저장소 포인터를
 * 이 인스턴스 자신의 buf로 다시 연결한다 (yl_var 값 복사 불안전 — var.h 참조). */
bool tr_sma_relink(tr_sma_t *s);

#endif
