#ifndef TR_FX_REG_V1_H
#define TR_FX_REG_V1_H

/* WSF_FXRegV1 포팅 (원본 158줄, 완전 제공). 해외선물 전용 세션 리셋형 자동 선형회귀.
 *
 * 국내 WSF_Mtf_LinRegV3(linreg_v3.c)의 원형이다. 회귀 코어(자동 회귀기간 표·OLS·
 * 출력 6종)는 같고, 차이는 세션 리셋을 날짜 변경 감지 대신 호출자가 넘기는
 * 세션초기화 인자로 받는다는 점이다 (원본 2줄, 72줄).
 * - 차트 주기별 회귀기간 자동 선택 (원본 30~49줄): 국내 V3와 동일한 표.
 *   원본은 매 평가 Var에 다시 계산하지만 DataCompress/BarInterval이 상수라
 *   포팅은 init에서 1회 계산한다 (결과 동일).
 * - 새 봉 판정: 원본은 sDate/sTime 변화(56~66줄). 포팅은 lr3와 같이 bar open_time
 *   변경으로 대응한다.
 * - 세션초기화==1인 새 봉에서 배열·유효개수 리셋 (원본 71~80줄).
 *   같은 봉 재평가에는 리셋하지 않는다 (리셋은 새 봉 블록 안에 있다).
 * - 새 봉: 시프트+유효개수 증가(상한 n), 같은 봉: [0] 덮어쓰기 (원본 82~92줄).
 * - 계산개수 = Min(유효개수, n) >= 최소회귀봉수(5)부터 유효 (원본 103~105줄).
 * - 절편 B는 공용 tr_ols_fit와 대수 동치지만 원본 산식 순서가 다르다 (원본 129줄:
 *   B = (sumY*sumX2 − sumX*sumXY)/분모). 비트 동일 지향으로 원본 순서대로 인라인 계산한다.
 *
 * 원본 기록 (불명 — 포팅에서 적용하지 않는다):
 * - 자동장초기화 (원본 52~54줄): 계산만 하고 본문에서 사용하지 않는 죽은 변수다.
 *   실제 리셋은 세션초기화 입력이 담당한다.
 * - 세션봉수 입력: 선언만 되고 본문에서 사용하지 않는다 (체인 계약상 입력 구조체에는 유지).
 *
 * 회귀가격[N]은 yl_var로 (docs/PORTING.md 암묵 시계열 원칙), 저장소는 구조체 안.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/indicators/indicator.h" /* tr_compress_t (DataCompress 대응) */
#include "core/market/var.h"
#include "core/model/time_us.h"

typedef struct {
    bool session_reset;     /* 세션초기화 (1이면 새 봉에서 배열·유효개수 리셋) */
    int32_t session_bars;   /* 세션봉수 (원본 본문 미사용 — 체인 계약상 전달만) */
    double price;           /* 회귀가격입력 */
    tr_time_us_t bar_open;  /* 새 봉 판정용 봉 시작 시각 (원본 sDate/sTime 대응) */
} tr_fxreg_input_t;

typedef struct {
    /* 설정 */
    tr_compress_t compress;
    uint32_t bar_interval;
    /* 상태 */
    uint32_t n;             /* 자동 선택된 회귀기간 */
    double prices_buf[100]; /* 회귀가격 저장소 (yl_var 규약: 상태 구조체 안) */
    yl_var prices;          /* 회귀가격[N] 대응 ([0]=최신, 유효 용량 n) */
    tr_time_us_t last_bar_open;
    bool has_bar;
    /* 출력 (원본 NumericRef 대응) */
    double line;            /* 회귀선 (무효 시 회귀가격입력) */
    double slope;           /* 회귀기울기 (LRS) */
    int line_sign;          /* 회귀선_구분: 기울기 부호 +1/−1/0 */
    bool reg_valid;         /* 회귀유효 */
    double r2;              /* 회귀신뢰도 [0,1] */
    double residual;        /* 회귀잔차 (잔차 표준편차) */
} tr_fxreg_t;

bool tr_fxreg_init(tr_fxreg_t *s, tr_compress_t compress, uint32_t bar_interval);

/* 매 평가 호출 (새 봉·진행 봉 재평가 모두). */
void tr_fxreg_eval(tr_fxreg_t *s, const tr_fxreg_input_t *in);

/* tr_fxreg_t를 포함한 구조체의 통째 값 복사(이식) 후 호출: 시계열 저장소 포인터를
 * 이 인스턴스 자신의 버퍼로 다시 연결한다 (yl_var 값 복사 불안전 — var.h 참조). */
bool tr_fxreg_relink(tr_fxreg_t *s);

#endif
