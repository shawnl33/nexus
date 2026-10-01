#ifndef TR_DAILY_MARKET_PROFILE_V1_H
#define TR_DAILY_MARKET_PROFILE_V1_H

/* WSF_DailyMarketProfileV1 포팅 (원본 63줄, 완전 제공). 일봉 이상 전용(호출부 책임).
 *
 * - 대표가격 = (H+L+C)/3, 계산기간(하한 2) 봉 롩백, 거래량 가중.
 * - 중심가격 = 계산기간 VWAP, 상단/하단 = 중심 ± 가중 모집단 표준편차×밴드배수.
 * - V<=0 봉은 분자·분모 모두에서 제외. 유효 조건: 창 내 누적거래량 > 0.
 * - 분봉 마켓 프로파일(market_profile)과 달리 세션 앵커링·봉 수 하한이 없다.
 * - 무효 시 출력 4개는 모두 0으로 리셋된다 (이월 없음).
 * - 원본은 히스토리 부족 시 동작이 불명 → 포팅은 가용 봉만 합산하는 것으로 기록한다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/market/candle.h"
#include "core/market/ring.h"

typedef struct {
    uint32_t period;       /* 계산기간입력 (하한 2) */
    double band_mult;      /* 밴드배수입력 (abs, 0→1) */
    tr_ring hist;          /* tr_candle_t, [0]=현재 봉 */
    /* 출력 */
    double center;         /* 중심가격 */
    double upper;          /* 상단가격 */
    double lower;          /* 하단가격 */
    bool valid;            /* 계산유효 */
} tr_dmp1_t;

bool tr_dmp1_init(tr_dmp1_t *s, uint32_t period, double band_mult,
                  tr_candle_t *storage, size_t capacity);

void tr_dmp1_on_bar(tr_dmp1_t *s, const tr_candle_t *bar);

#endif
