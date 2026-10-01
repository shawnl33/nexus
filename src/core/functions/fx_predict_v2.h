#ifndef TR_FX_PREDICT_V2_H
#define TR_FX_PREDICT_V2_H

/* WSF_FXPredictV2 포팅 (원본 123줄, 완전 제공). 해외선물 전용 회귀선 기준 미래가격·방향.
 *
 * 국내 linreg_predict_v4(WSF_Mtf_LinRegPredictV4)의 원형이다. 차이:
 * - horizon이 3개에서 5개로 확장 (예측봉수1~5입력, 평가마다 Max(0,·) 클램프 — 원본 42~46줄).
 * - 내장 ATR(14)를 세션 ATR 수동 계산으로 교체 (원본 36~40줄): 세션초기화 봉은
 *   세션ATR = 세션TR(=H−L)로 재시드하고, 이후 세션봉수<=14까지 누적 평균,
 *   초과하면 (이전×13+TR)/14. 즉 세션 리셋을 타는 ATR이다 (국내 내장 ATR은 리셋 없음).
 * - 가속도의 일자 경계 차단(lp4의 day_hist 방식)을 세션봉수<=3 게이트로 대체 (원본 82~83줄).
 * - 세션ATR[1]은 [1] 깊이 재귀라 스칼라로 둔다 (docs/PORTING.md 전환 기준 ①).
 * - 회귀기울기입력[3]은 yl_var로 (lp4의 slope_hist 패턴). 시프트 계약도 lp4와 같다
 *   (새 봉 push, 같은 봉 [0] 덮어쓰기 — 호출자가 is_new_bar를 넘긴다).
 *
 * 세션봉수 계약: 1기반 (세션 첫 봉=세션초기화 1, 다음 봉부터 2,3,...). 세션초기화==0인데
 * 세션봉수<=14 분기에서 나눗셈에 쓰이므로 0은 계약 위반이다 (원본 그대로, 가드 없음).
 * C[1](차트 이전 종가)은 포팅에서 prev_close로 추적한다 — 원본은 세션초기화==0이면 항상
 * C[1]을 쓰지만(차트 이전 봉 존재 가정), 포팅의 첫 평가에는 이전 종가가 없어 H−L만 쓴다
 * (atr.c true_range의 has_prev_close 패턴과 같은 적응. 실전 체인은 첫 세션 봉이
 * 세션초기화==1이라 이 경로에 도달하지 않는다).
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/market/var.h"

typedef struct {
    double cur_line;        /* 현재회귀선입력 */
    double slope;           /* 회귀기울기입력 */
    bool reg_valid;         /* 회귀유효입력 == 1 */
    double r2;              /* 회귀신뢰도입력 [0,1] */
    double high, low, close;/* 세션TR용 현재 봉 (원본 H/L/C builtin 대응, close는 C[1] 추적용) */
    bool session_reset;     /* 세션초기화 */
    int32_t session_bars;   /* 세션봉수 (1기반: ATR 누적 분모·가속도 게이트) */
    bool is_new_bar;        /* 회귀기울기입력[N] 시프트 트리거 (lp4와 같은 계약) */
    int32_t horizons[5];    /* 예측봉수1~5입력 */
} tr_fxp2_input_t;

typedef struct {
    /* 상태 */
    double slope_hist_buf[4]; /* slope_hist 저장소 (yl_var 규약: 상태 구조체 안) */
    yl_var slope_hist;      /* 회귀기울기입력[N] ([0]=현재 봉) */
    double session_atr;     /* 세션ATR (원본 Var — [1] 깊이 재귀라 스칼라) */
    double prev_close;      /* C[1] 대응 */
    bool has_prev_close;
    /* 출력 (원본 NumericRef 대응) */
    double pred_price[5];   /* 예측가격1~5 (무효 시 현재회귀선입력 평탄) */
    int pred_dir[5];        /* 예측방향1~5 (−1/0/+1) */
    double adj_slope;       /* 보정기울기 */
    double accel;           /* 기울기가속도 */
    double volatility;      /* 예측변동성 (= 세션ATR, 유효 무관) */
} tr_fxp2_t;

bool tr_fxp2_init(tr_fxp2_t *s);

/* 매 평가 호출. */
void tr_fxp2_eval(tr_fxp2_t *s, const tr_fxp2_input_t *in);

/* tr_fxp2_t를 포함한 구조체의 통째 값 복사(이식) 후 호출: 시계열 저장소 포인터를
 * 이 인스턴스 자신의 버퍼로 다시 연결한다 (yl_var 값 복사 불안전 — var.h 참조). */
bool tr_fxp2_relink(tr_fxp2_t *s);

#endif
