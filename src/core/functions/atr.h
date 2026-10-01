#ifndef TR_ATR_H
#define TR_ATR_H

/* ATR (Average True Range).
 *
 * WSF_Mtf_LinRegPredictV4의 내장 ATR(14) 대응 구현.
 *
 * 산식 근거: 예스스탁 공식 커뮤니티에 인용된 예스랭귀지 내장 ATR 함수식
 * (https://www.yesstock.com/community/qna-type1-dtl?postNo=128609):
 *   TrueHigh = max(C[1], H), TrueLow = min(C[1], L), TrueRange = TrueHigh-TrueLow,
 *   ATR = Ma(TrueRange, Period) — 즉 **TR의 단순이동평균(SMA)**.
 * Wilder 평활이 아니므로 SMA를 기본 모드로 하고, 비교·대조용으로 Wilder 모드를 남긴다.
 * 공식 매뉴얼 원문 대조가 남은 미검증 항목이다.
 *
 * 시딩(기간 미만): 가용 봉의 평균을 사용한다 (원본 Ma()의 초기 동작은 불명, 기록).
 *
 * 갱신 계약:
 * - 확정된 봉은 tr_atr_on_bar로 한 번만 반영한다 (증분).
 * - 진행 중 봉 포함 추정은 tr_atr_candidate로 상태 변경 없이 조회한다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/market/series.h"

typedef enum {
    TR_ATR_SMA = 0,   /* 예스랭귀지 내장 산식 (기본) */
    TR_ATR_WILDER = 1 /* 비교용 Wilder 평활 */
} tr_atr_mode_t;

#define TR_ATR_MAX_PERIOD 250

typedef struct {
    tr_atr_mode_t mode;
    uint32_t period;
    double prev_close;
    bool has_prev_close;
    /* SMA 모드: 최근 period개 TR의 시계열 ([0]=최신 TR).
     * yl_series 파일럿 전환 (docs/PORTING.md): 저장소는 이 구조체 안에 둔다. */
    double sma_hist_buf[TR_ATR_MAX_PERIOD];
    yl_series sma_hist;
    double sma_sum;
    /* Wilder 모드 */
    double wilder_atr;
    double wilder_seed_sum;
    uint32_t wilder_count;
    bool wilder_seeded;
} tr_atr_t;

/* mode: TR_ATR_SMA(기본) 또는 TR_ATR_WILDER. */
bool tr_atr_init_ex(tr_atr_t *a, uint32_t period, tr_atr_mode_t mode);

/* SMA 기본 모드. */
bool tr_atr_init(tr_atr_t *a, uint32_t period);

/* 확정된 봉 반영. */
double tr_atr_on_bar(tr_atr_t *a, double high, double low, double close);

/* 진행 중 봉 포함 추정 (상태 변경 없음). */
double tr_atr_candidate(const tr_atr_t *a, double high, double low, double close);

/* 현재 ATR. 시딩 전이면 가용 봉 평균, 데이터 없으면 0. */
double tr_atr_value(const tr_atr_t *a);

/* tr_atr_t를 포함한 구조체의 통째 값 복사(이식) 후 호출: SMA 시계열의 저장소
 * 포인터를 이 인스턴스 자신의 sma_hist_buf로 다시 연결한다 (yl_series 값 복사
 * 불안전 — series.h, docs/PORTING.md 참조). */
bool tr_atr_relink(tr_atr_t *a);

#endif
