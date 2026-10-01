#ifndef TR_FX_CURVE_V1_H
#define TR_FX_CURVE_V1_H

/* WSF_FXCurveV1 포팅 (원본 82줄, 완전 제공). 해외선물 전용 (L+H)/2 중간값 회귀 곡선.
 *
 * 국내 htf_curve_predict(WSF_Htf_CurvePredict)의 원형이다. 같은 점:
 * (L+H)/2 최근 19표본 롤링 선형회귀, 절대 카운터 X를 x좌표로 한 원시 외삽,
 * 곡선하=MinLRL[1]·곡선상=MinLRL·예측가=기울기×(X+틱수)+절편·변화=|예측−곡선하|·방향=예측−곡선하,
 * 깨진 변곡 감지 블록(원본 72~75줄: 이전기울기에 먼저 대입해서 기울기변화가 항상 0)은
 * 출력 무영향으로 생략한다 (htf와 같은 판단).
 *
 * 다른 점 (원본 코드 근거):
 * - 배열 이동은 봉변화(Index != 최근봉, 원본 15~25줄)에서만 일어난다 — htf의
 *   "매 평가 시프트"와 다르다. 포팅은 호출자가 넘기는 is_new_bar로 대응한다
 *   (새 봉 push / 같은 봉 [0] 덮어쓰기 — lr3 패턴).
 * - 표본수는 실제 개수 n(상한 19)이고 n>=2부터 회귀한다 (원본 23, 27, 56~58줄) —
 *   htf의 "19개 고정 + 워밍업 0 채움"이 아니라 워밍업 0 채움이 없다.
 * - x좌표 X는 세션 리셋 시 0으로 돌아간다 (원본 20줄) — 세션 상대 카운터 (htf는 누적).
 * - 곡선하는 리셋 봉에 MinLRL(현재값) (원본 64줄). MinLRL[1]은 [1] 깊이라 스칼라로 둔다
 *   (docs/PORTING.md 전환 기준 ①). 첫 평가(리셋 아님)의 MinLRL[1]은 원본 Var 초기값 0 대응.
 * - 세션봉수 입력은 선언만 되고 본문에서 사용하지 않는다 (불명 — 입력 구조체에는 유지).
 * - 유효성 출력(evals>=20)은 원본에 없다 — 추가하지 않는다.
 *
 * MinClose[N]은 yl_var로 (용량 19 = 표본수 상한), 저장소는 구조체 안.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/market/var.h"

typedef struct {
    bool session_reset;     /* 세션초기화 (1이면 봉변화에서 배열·표본수·X 리셋) */
    int32_t session_bars;   /* 세션봉수 (원본 본문 미사용 — 체인 계약상 전달만) */
    double high, low;       /* (L+H)/2 중간값용 현재 봉 (원본 H/L builtin 대응) */
    int32_t ticks;          /* 곡선예측틱수 */
    bool is_new_bar;        /* Index 변화 대응 (봉변화 감지 — 호출자 공급) */
} tr_fxc_input_t;

typedef struct {
    /* 상태 */
    double minclose_buf[19]; /* MinClose 저장소 (yl_var 규약: 상태 구조체 안) */
    yl_var minclose;         /* MinClose[N] ([0]=최신, 유효 용량 19 = 표본수 상한) */
    int64_t x;               /* X (세션 상대 봉 카운터 — 리셋 시 0) */
    double prev_minlrl;      /* MinLRL[1] — 직전 봉 말 회귀선 값 (새 봉에서 갱신) */
    double last_minlrl;      /* 이번 봉의 최신 회귀선 값 (다음 봉의 [1] 후보) */
    double slope;            /* MinLRS — 원본 Var (출력 미해당) */
    double intercept;        /* MinB — 원본 Var (출력 미해당) */
    /* 출력 (원본 NumericRef 대응) */
    double low_curve;        /* 곡선하: 직전 평가의 회귀선 값, 리셋 봉은 현재값 */
    double high_curve;       /* 곡선상: 현재 회귀선 값 */
    double pred_price;       /* 곡선예측가격: 원시 외삽 */
    double change;           /* 곡선예측가격_변화: |예측 − 곡선하| */
    double direction;        /* 곡선예측가격_방향: 예측 − 곡선하 (가격 단위 수치) */
} tr_fxc_t;

bool tr_fxc_init(tr_fxc_t *s);

/* 매 평가 호출 (새 봉·진행 봉 재평가 모두). */
void tr_fxc_eval(tr_fxc_t *s, const tr_fxc_input_t *in);

/* tr_fxc_t를 포함한 구조체의 통째 값 복사(이식) 후 호출: 시계열 저장소 포인터를
 * 이 인스턴스 자신의 버퍼로 다시 연결한다 (yl_var 값 복사 불안전 — var.h 참조). */
bool tr_fxc_relink(tr_fxc_t *s);

#endif
