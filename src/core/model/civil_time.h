#ifndef TR_CIVIL_TIME_H
#define TR_CIVIL_TIME_H

/* UTC epoch 마이크로초 ↔ 역법(날짜·시각) 변환.
 *
 * 세션 정책·봉 정렬(계획서 §8)은 '거래소 현지 시각'으로 정의되므로
 * UTC 저장값과 현지 표현 사이의 변환이 필요하다.
 * 알고리즘: Howard Hinnant의 days_from_civil / civil_from_days (public domain).
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/model/time_us.h"

typedef struct {
    int year;          /* 예: 2026 */
    unsigned month;    /* 1..12 */
    unsigned day;      /* 1..31 */
    unsigned hour;     /* 0..23 */
    unsigned min;      /* 0..59 */
    unsigned sec;      /* 0..59 */
} tr_civil_t;

#define TR_US_PER_SEC  INT64_C(1000000)
#define TR_US_PER_MIN  (INT64_C(60) * TR_US_PER_SEC)
#define TR_US_PER_DAY  (INT64_C(86400) * TR_US_PER_SEC)

/* 요일: 0=일요일 .. 6=토요일 */
unsigned tr_weekday_from_days(int64_t days_since_epoch);

/* days since 1970-01-01 (UTC 기준 프레임) */
int64_t tr_days_from_civil(int year, unsigned month, unsigned day);

/* UTC epoch µs + utc_offset_min 을 현지 역법으로 분해한다. */
bool tr_civil_from_time_us(tr_time_us_t t, int32_t utc_offset_min, tr_civil_t *out);

/* 현지 역법 + utc_offset_min → UTC epoch µs. 범위/필드 오류 시 false. */
bool tr_time_us_from_civil(const tr_civil_t *c, int32_t utc_offset_min, tr_time_us_t *out);

/* t가 속한 현지 날짜의 자정(UTC µs)과 경과 분(0..1439)을 구한다. */
void tr_local_midnight(int64_t days_since_epoch, int32_t utc_offset_min, tr_time_us_t *midnight_us);

/* UTC µs → (현지 days, 현지 분). out은 NULL 가능. */
void tr_local_day_and_min(tr_time_us_t t, int32_t utc_offset_min, int64_t *out_days, uint32_t *out_min);

#endif
