#ifndef TR_LINREG_PREDICT_V4_H
#define TR_LINREG_PREDICT_V4_H

/* WSF_Mtf_LinRegPredictV4 포팅 (원본 94줄, 완전 제공).
 *
 * 회귀 결과(회귀선·기울기·R²)를 입력받아 n봉 후 예측가격 3개와 부수 출력을 생성.
 * - 신뢰계수: (R²−0.20)/0.50 을 [0,1] 클램프. R²≤0.20이면 예측 평탄화.
 * - 보정기울기: 기울기×신뢰계수, ±0.50·ATR 클랩프.
 * - 가속도: (기울기−기울기[3])/3×신뢰계수, ±0.08·ATR 클랩프.
 *   보정기울기와 같은 부호(추세 가속)면 0으로 억제 — 둔화/반전 성분만 사용.
 *   30분 이하 분봉에서 3봉 창이 일자 경계를 걸치면 0.
 * - 이동값: 보정기울기·n + 0.5·가속도·n², 상한 1.50·ATR·√max(1,n).
 * - 무효 입력: 예측가격=현재회귀선(평탄), 방향=0.
 *
 * 원본 내장 ATR(14)는 커뮤니티 인용 근거로 **TR의 단순이동평균(SMA)** 으로 구현한다
 * (atr.h 참조. Wilder 평활이 아님, 공식 매뉴얼 대조는 미검증).
 * 원본 [3] 참조는 봉 인덱스 기준으로 구현. 초기 4봉 미만에서는 가속도를 0으로 둔다
 * (원본 초기 동작 불명, docs/yeslanguage_mapping.md §7).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/indicators/atr.h"
#include "core/model/units.h"

typedef struct {
    double cur_line;       /* 현재회귀선입력 */
    double slope;          /* 회귀기울기입력 (봉당 가격) */
    bool reg_valid;        /* 회귀유효입력 == 1 */
    double r2;             /* 회귀신뢰도입력 [0,1] */
    double high, low, close; /* ATR 추정용 현재 봉 */
    int64_t trading_day;   /* sDate 대응 */
    bool is_new_bar;       /* 히스토리 시프트 트리거 */
    bool compress_min_le30;/* DataCompress==2 && BarInterval<=30 대응 */
} tr_lp4_input_t;

typedef struct {
    int32_t n[3];             /* 예측봉수1~3 */
    /* 봉 인덱스 기준 입력 이력 ([0]=현재 봉) */
    double slope_hist[4];
    size_t slope_len;
    int64_t day_hist[4];
    size_t day_len;
    tr_atr_t atr;
    /* 출력 (원본 NumericRef 대응) */
    double pred_price[3];     /* 예측가격1~3 */
    int pred_dir[3];          /* 예측방향1~3 (−1/0/+1) */
    double adj_slope;         /* 보정기울기 */
    double accel;             /* 기울기가속도 */
    double volatility;        /* 예측변동성 = ATR(14) */
    bool valid_out;           /* 회귀유효입력이 1로 계산됨 */
} tr_lp4_t;

bool tr_lp4_init(tr_lp4_t *s, int32_t n1, int32_t n2, int32_t n3);

/* 확정된 봉의 H/L/C를 ATR에 반영. 봉 확정 시점에 1회 호출. */
void tr_lp4_on_bar_closed(tr_lp4_t *s, double high, double low, double close);

/* 매 평가 호출. */
void tr_lp4_eval(tr_lp4_t *s, const tr_lp4_input_t *in);

#endif
