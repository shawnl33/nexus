#ifndef TR_SCORE_1M_H
#define TR_SCORE_1M_H

/* 1분봉 통합 점수 포팅 (메인 원본 920~989줄).
 *
 * 단계화_1분_통합 =
 *   IFF(핵심세션미래방향 > 0, 2, −2)                ← 방향 0도 −2 분기 (원본 그대로 보존)
 *   + IFF(핵심세션미래방향 > [1], 1, 0) + IFF(< [1], −1, 0)
 *   + 핵심마켓방향 + 핵심회귀방향 + 핵심호가방향
 *
 * - 핵심세션미래방향: 세션 첫 봉(CurrentBar==1 또는 BDate 변경)이면 0, 아니면 Htf 방향 출력(수치).
 *   세션 첫 봉 점수는 −2에서 시작하고, 전일 값이 양수였다면 비교 항에서 추가로 −1.
 * - 핵심마켓방향: Average((H+L)/2, 마켓계산기간) 중심과 그 기울기·종가 비교.
 * - 핵심회귀방향: 회귀유효 && 신뢰도>=최소신뢰도 && 종가와 평탄회귀선 비교.
 * - 핵심호가방향: 호가점수 부호.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/market/var.h"

typedef struct {
    uint32_t market_period;   /* 마켓계산기간 */
    double min_r2;            /* 최소신뢰도 */
    /* 핵심마켓 (H+L)/2 이력: 원본 Average((H+L)/2, 기간)의 변수 대응.
     * yl_var이지만 저장소는 호출자 소유 외부 버퍼를 부착한다 (엔진의
     * score_mid_storage 모델) — 기증 슬롯 남부가 아니므로 이식 시 relink 불필요 */
    yl_var mid_hist;
    double prev_future_dir;
    bool has_prev_future;
    double prev_market_center;
    bool has_prev_market;
    int64_t prev_day;
    bool has_prev_day;
    bool first_bar_done;
    /* 출력 */
    bool active;              /* 핵심일분봉여부 */
    double future_dir;        /* 핵심세션미래방향 (수치) */
    double market_center;     /* 핵심마켓중심 */
    int market_dir, reg_dir, ob_dir;
    int score;                /* 단계화_1분_통합 */
} tr_score1m_t;

typedef struct {
    double high, low, close;
    double htf_direction;     /* 핵심원시미래방향 (Htf 방향 출력, 수치) */
    int64_t trading_day;
    bool is_min_1;            /* DataCompress==2 && BarInterval==1 */
    bool reg_valid;           /* 핵심곡선유효 */
    double reg_r2;            /* 핵심곡선신뢰도 */
    double reg_flat_line;     /* 핵심평탄회귀선 */
    double ob_score;          /* 핵심호가점수 (호가 무효면 0) */
} tr_score1m_input_t;

bool tr_score1m_init(tr_score1m_t *s, uint32_t market_period, double min_r2,
                     double *storage, size_t capacity);

void tr_score1m_on_bar(tr_score1m_t *s, const tr_score1m_input_t *in);

#endif
