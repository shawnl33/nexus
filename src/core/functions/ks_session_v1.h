#ifndef TR_KS_SESSION_V1_H
#define TR_KS_SESSION_V1_H

/* WSF_KSSession1mV1 / WSF_KSSession15V1
 * (reference/yeslanguage/functions/WSF_KSSession1mV1.txt).
 * 두 원본은 함수 이름만 다르고 본문이 같다. 날짜·시각 입력은 선언만 되고 쓰이지 않는다.
 *
 * 장번호는 새 봉(Index 변화)에서만 갱신한다. 같은 봉 재평가는 같은 번호를 돌려준다.
 * - CurrentBar == 1 이면 장번호 = 1 (원본 7줄).
 * - 그 외 BDate != BDate[1], 또는 DayIndex < DayIndex[1],
 *   또는 DayIndex == 0 이고 직전 DayIndex != 0 이면 장번호를 1 올린다 (원본 8줄).
 * 해외 WSF_FXSessionKeyV1의 07:00/15:30 시계 키와 다르다.
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool has_bar;
    int64_t session_no; /* 장번호 */
    int64_t bdate;      /* 직전 새 봉의 BDate ([1] 대응) */
    int32_t day_index;  /* 직전 새 봉의 DayIndex */
} tr_ks_session_t;

void tr_ks_session_init(tr_ks_session_t *s);

/* is_new_bar는 Index 변화다. current_bar는 CurrentBar.
 * 같은 봉을 다시 평가하면 저장된 장번호를 그대로 돌려준다. */
int64_t tr_ks_session_eval(tr_ks_session_t *s, bool is_new_bar, int32_t current_bar,
                           int64_t bdate, int32_t day_index);

#endif
