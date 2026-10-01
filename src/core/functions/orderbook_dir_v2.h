#ifndef TR_ORDERBOOK_DIR_V2_H
#define TR_ORDERBOOK_DIR_V2_H

/* WSF_OrderBookDirectionV2 포팅 (원본 181줄, 완전 제공).
 *
 * 호가 잔량 불균형을 점수화해 호가 방향·기울기·상태를 산출.
 * - 잔량차이: 선물은 매수−매도, 주식은 매도−매수 (원본 42~47줄). 부호반전 시 반전.
 * - 핵심점수 = (잔량차이점수×60 + 잔량비율점수×40)/100.
 * - 방향점수 = (핵심점수×60 + 평균3×25 + 평균5×15)/100.
 * - 기울기3 = 방향점수 − 방향점수[3], ±100 클랩프. 상태 = ±2(강세)/±1(관심)/0.
 * - 유효 조건: 전체잔량 > 0. 호가 없는 종목은 유효=0으로만 처리하고 0으로 채우지 않는다.
 * - 일자(BDate) 변경 시 상태 리셋.
 *
 * Bids/Asks는 매수·매도 총잔량으로 가정한다. 몇 단계 호가 합계인지는 데이터 소스 정의에 따륾며
 * LS 필드 매핑 시 확정한다 (docs/yeslanguage_mapping.md §7).
 * 시계열(핵심점수·방향점수)은 유효 평가에서만 갱신한다 (무효 봉 이월 의미는 원본 불명,
 * '유효 봉에서만' 해택을 채택하고 비교 자료 확보 시 재확인).
 *
 * WSF_OrderBookDirectionV1은 2026-09-28 제공 원본 확인 결과 V2와 함수명 대입 1줄만 다른
 * 동일 로직이다. 이 모듈을 V1/V2 호출 경로별 별도 인스턴스로 재사용한다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/model/units.h"
#include "core/market/var.h"

typedef struct {
    /* 설정 */
    double interest_level; /* 관심기준 (abs, >=1) */
    double strong_level;   /* 강세기준 (abs, >= 관심기준) */
    bool sign_reverse;     /* 부호반전입력 */
    bool is_futures;       /* CodeCategory==4 대응 */
    /* 상태 */
    bool started;
    int64_t last_day;
    uint64_t quote_no;     /* 호가번호 (당일 유효 평가 횟수, 0부터) */
    double cum_total;      /* 누적잔량 */
    /* 핵심점수·계산호가방향: 원본 Var의 암묵 시계열 대응 (유효 평가만 push, [0]=최신).
     * yl_var 전환 (docs/PORTING.md): 저장소는 이 구조체 안에 둔다 */
    double core_hist_buf[5];
    yl_var core_hist;      /* 핵심점수[0..4] (유효 평가만) */
    double dir_hist_buf[4];
    yl_var dir_hist;       /* 계산호가방향[0..3] (유효 평가만) */
    /* 출력 */
    double score;          /* 호가방향점수 */
    double slope3;         /* 호가기울기3 */
    int state;             /* 호가방향상태: −2..+2 */
    tr_validity_t validity;/* 호가유효 대응 */
} tr_obd2_t;

bool tr_obd2_init(tr_obd2_t *s, double interest_level, double strong_level,
                  bool sign_reverse, bool is_futures);

/* 매 평가 호출. bids/asks는 총잔량, trading_day는 BDate 대응. */
void tr_obd2_eval(tr_obd2_t *s, double bids, double asks, int64_t trading_day);

/* tr_obd2_t를 포함한 구조체의 통째 값 복사(이식) 후 호출: 시계열 저장소 포인터를
 * 이 인스턴스 자신의 버퍼로 다시 연결한다 (yl_var 값 복사 불안전 — var.h 참조). */
bool tr_obd2_relink(tr_obd2_t *s);

#endif
