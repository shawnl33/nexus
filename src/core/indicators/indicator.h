#ifndef TR_INDICATOR_H
#define TR_INDICATOR_H

/* 지표 공통 계약 (계획서 §9)
 *
 * - 지표는 설정·낶부 상태·출력을 가진 독립 인스턴스다. 같은 코드가 여러 종목을 처리핢 상태가 섞이지 않는다.
 * - 시장 데이터는 읽기 전용으로 전달한다. 지표는 주문을 본지 않는다.
 * - 출력에는 유효성(tr_validity_t)이 붙는다. 원본 YesLanguage의 0 채움 쓰레기값을
 *   그대로 유효한 값처럼 낸지 않고, 값은 원본과 같이 계산하되 유효성을 별도로 표시한다.
 * - 원본 내장 컨텍스트는 입력으로 명시 주입한다:
 *   DataCompress→tr_compress_t, BarInterval→bar_interval, sDate/BDate→trading_day,
 *   DayIndex==0→is_session_first, 새 봉(sDate/sTime 변화)→is_new_bar.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/market/candle.h"
#include "core/model/units.h"

typedef enum {
    TR_COMPRESS_TICK = 0, /* DataCompress 0 */
    TR_COMPRESS_SEC = 1,  /* 1 */
    TR_COMPRESS_MIN = 2,  /* 2 (분봉) */
    TR_COMPRESS_DAY = 3,  /* 3 */
    TR_COMPRESS_WEEK = 4, /* 4 */
    TR_COMPRESS_MONTH = 5 /* 5 */
} tr_compress_t;

typedef struct {
    const tr_candle_t *bar;     /* 현재 봉 (진행 중일 수 있음) */
    tr_time_us_t event_time_us;
    int64_t trading_day;        /* 봉의 거래일 id (sDate/BDate 대응) */
    bool is_new_bar;            /* 새 봉 첫 평가 (sDate/sTime 변화 대응) */
    bool is_session_first;      /* 당일 첫 봉 (DayIndex==0 대응) */
    tr_compress_t compress;     /* DataCompress 대응 */
    uint32_t bar_interval;      /* BarInterval 대응 (분봉이면 분 수) */
} tr_ind_eval_t;

#endif
