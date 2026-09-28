#ifndef TR_LINREG_V3_H
#define TR_LINREG_V3_H

/* WSF_Mtf_LinRegV3 포팅 (제공 신형, 원본 192줄, 19인자).
 *
 * - 차트 주기별 회귀기간 자동 선택: 틱/초/1분 이하 30, ≤5분 18, ≤15분 10, ≤30분 6,
 *   그 외 분봉 12, 일봉 20, 주봉 13, 월봉 12 (원본 47~63줄).
 * - 새 봉(sDate/sTime 변화 대응: bar->open_time_us 변경)에서 가격 배열 1칸 시프트 +
 *   유효개수 증가(상한 n). 같은 봉에서는 최신가 [0] 덮어쓰기 (원본 83~106줄).
 * - 30분 이하 분봉은 당일 첫 봉(DayIndex==0 대응 is_session_first)에서 배열·유효개수 리셋.
 * - 유효 조건: 계산개수 >= 5 (원본 최소회귀봉수). 무효 시 회귀선=입력가격, 나머지 0.
 * - 회귀 완료 후 WSF_Mtf_LinRegPredictV4를 낶부 호출해 예측 출력까지 생산 (원본 172~189).
 *
 * 주의: 메인 지표가 호출하는 구형 7인자 V3는 2026-09-28 제공된 `WSF_Mtf_LinRegV3_구형.txt`로
 * 확인된 결과 회귀 코어가 신형과 바이트 단위 동일하다(V4 호출 없음, 7인자). 따라서 이 모듈의
 * 회귀 6종 출력은 구형과도 동치다. 구형 호출 패턴(호출 측이 V4를 별도 호출)은 런타임 조립 시
 * v4 결과를 소비하지 않는 것으로 재현한다. 잔여 차이 기록: 절편 연산 순서 ulp, 새 봉 판정의
 * sTime 해상도(틱/초봉, 불명) — docs/yeslanguage_mapping.md §6-1 참조.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/indicators/indicator.h"
#include "core/indicators/linreg_predict_v4.h"
#include "core/model/units.h"

typedef struct {
    /* 설정 */
    tr_compress_t compress;
    uint32_t bar_interval;
    int32_t predict_bars[3]; /* 예측봉수1~3 */
    /* 상태 */
    uint32_t n;              /* 자동 선택된 회귀기간 */
    double prices[100];      /* [0]=최신 시프트 배열 (원본 회귀가격) */
    uint32_t valid_count;    /* 유효개수 */
    tr_time_us_t last_bar_open;
    bool has_bar;
    tr_lp4_t v4;
    /* 출력 (원본 NumericRef 대응) */
    double line;             /* 회귀선: 현재 봉 위치 적합값 */
    double slope;            /* 회귀기울기 */
    int line_sign;           /* 회귀선_구분: 기울기 부호 +1/−1/0 */
    bool reg_valid;          /* 회귀유효 */
    double r2;               /* 회귀신뢰도 [0,1] — 예측 성공 확률이 아니다 */
    double residual;         /* 회귀잔차 */
    tr_validity_t validity;
} tr_lr3_t;

bool tr_lr3_init(tr_lr3_t *s, tr_compress_t compress, uint32_t bar_interval,
                 int32_t n1, int32_t n2, int32_t n3);

/* 매 평가 호출. ev->bar는 현재 봉. */
void tr_lr3_eval(tr_lr3_t *s, const tr_ind_eval_t *ev);

#endif
