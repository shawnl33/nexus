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
 */

#include <stdbool.h>

typedef struct {
    int period;
    int count;         /* 창에 들어 있는 종가 수 (<= period) */
    int head;          /* 가장 오래된 원소의 buf 인덱스 */
    double buf[64];
    double value;
    bool valid;
} tr_sma_t;

/* period는 1~64로 클램프한다. */
void tr_sma_init(tr_sma_t *s, int period);

void tr_sma_on_bar(tr_sma_t *s, double close, bool is_new_bar);

#endif
