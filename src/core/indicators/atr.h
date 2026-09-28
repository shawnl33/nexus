#ifndef TR_ATR_H
#define TR_ATR_H

/* ATR (Average True Range) — Wilder 평활.
 *
 * WSF_Mtf_LinRegPredictV4의 내장 ATR(14) 대응 구현.
 * 원본의 내장 ATR 산식은 미제공으로 불명(docs/yeslanguage_mapping.md §7)이므로
 * 표준 Wilder 평활로 구현하고, 산식이 다륩으로 확인되면 버전을 올려 교정한다.
 *
 * 갱신 계약:
 * - 확정된 봉은 tr_atr_on_bar로 한 번만 반영한다 (증분).
 * - 진행 중 봉 포함 추정은 tr_atr_candidate로 상태 변경 없이 조회한다.
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t period;
    double prev_close;
    bool has_prev_close;
    double atr;      /* 시딩 완료 후 Wilder 값 */
    double tr_sum;   /* 시딩 중 누적 */
    uint32_t count;  /* 반영된 봉 수 */
    bool seeded;
} tr_atr_t;

bool tr_atr_init(tr_atr_t *a, uint32_t period);

/* 확정된 봉 반영. 현재 ATR 추정값 반환. */
double tr_atr_on_bar(tr_atr_t *a, double high, double low, double close);

/* 진행 중 봉 포함 추정 (상태 변경 없음). */
double tr_atr_candidate(const tr_atr_t *a, double high, double low, double close);

/* 현재 ATR. 시딩 전이면 누적 단순 평균, 데이터 없으면 0. */
double tr_atr_value(const tr_atr_t *a);

#endif
