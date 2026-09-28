#ifndef TR_MARKET_PROFILE_H
#define TR_MARKET_PROFILE_H

/* 마켓 프로파일(분봉) 포팅 (메인 원본 806~901줄).
 *
 * - 당일 봉만 계산: 세션 첫 봉(DayIndex==0)에서 봉 카운터 리셋.
 * - 계산봉수 = Min(마켓계산기간, 세션봉수), 대표가격 = (H+L+C)/3, 거래량 가중.
 * - 중심가격 = VWAP, 가중표준편차, 상단1/하단1 = 중심 ± 표준편차×밴드배수, 상단2/하단2 = 2배 거리.
 * - 유효 조건: 누적거래량 > 0 && 계산봉수 >= 2. 무효 봉에는 값을 이월하고 유효 플래그만 0
 *   (원본 var 이월 의미 보존 — 소비자는 반드시 유효 플래그를 확인).
 * - 중심단계(−3..+3): 중심기울기·종가와 중심 비교·종가와 평탄회귀선 비교가 같은 방향일 때만.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/market/candle.h"
#include "core/market/ring.h"

typedef struct {
    uint32_t period;       /* 마켓계산기간 */
    double band_mult;      /* 마켓밴드배수 */
    double price_scale;
    /* 상태 */
    uint32_t session_bars; /* 마켓세션봉수 */
    bool has_bar;
    tr_ring hist;          /* tr_candle_t, [0]=현재 봉 */
    /* 출력 */
    bool valid;            /* 마켓계산유효 */
    double center;         /* 마켓중심가격 (무효 봉 이월) */
    double upper1, lower1, upper2, lower2;
    double center_slope;   /* 마켓중심기울기 */
    double position_strength; /* 마켓위치강도 (−100..100) */
    int stage;             /* 마켓중심단계 (−3..+3) */
} tr_market_t;

bool tr_market_init(tr_market_t *s, uint32_t period, double band_mult, double price_scale,
                    tr_candle_t *storage, size_t capacity);

/* 매 봉 1회. session_first는 DayIndex==0 대응, flat_reg_line은 곡선회귀선_평탄. */
void tr_market_on_bar(tr_market_t *s, const tr_candle_t *bar, bool session_first, double flat_reg_line);

#endif
