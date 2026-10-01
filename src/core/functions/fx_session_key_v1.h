#ifndef TR_FX_SESSION_KEY_V1_H
#define TR_FX_SESSION_KEY_V1_H

/* WSF_FXSessionKeyV1 (reference/yeslanguage/functions/WSF_FXSessionKeyV1.txt) — 세션 키.
 *
 * 해외선물 23시간 장의 "하루"를 한국시간 07:00/15:30 두 구간 리셋으로 표현한다:
 * - 시각 >= 15:30:00 → 당일 날짜의 구간 1 (저녁 구간 시작)
 * - 시각 <  07:00:00 → 전날 날짜의 구간 1 (자정 넘김 — 전날로 당기고 월/연도 롤백)
 * - 07:00:00 ~ 15:29:59 → 당일 날짜의 구간 0 (낮 구간)
 * 반환: 기준날짜(YYYYMMDD)*2 + 구간. 모든 FX 함수의 세션 리셋 판별 기준.
 * 상태 없는 순수 함수 — 원본의 Var(연도/월/일/기준날짜/구간)는 지역 변수에 대응한다. */

#include <stdint.h>

int64_t tr_fx_session_key_v1(int64_t yyyymmdd, int64_t hhmmss);

#endif
