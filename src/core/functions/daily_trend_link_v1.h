#ifndef TR_DAILY_TREND_LINK_V1_H
#define TR_DAILY_TREND_LINK_V1_H

/* WSF_1m_DailyTrendLinkV1 포팅 (원본 150줄, 완전 제공). 1분봉 전용.
 *
 * - 각 세션의 고가/저가를 집계해 완성 일봉을 생성한다. 대표값은 (세션고+세션저)/2.
 * - 오늘 진행 중인 일봉은 제외하고 직전 n개(일봉회귀기간, 5~100 클램프) 완성 일봉만 회귀.
 * - 일봉회귀선 = 마지막 회귀값 + 기울기 (오늘 위치로 1봉 투영).
 * - 추세 판정(방향/상태/강도/유효)은 WSF_Daily_LinRegTrendV1 포팅(dlrt1)에 위임.
 * - 첫 로딩 세션이 중간부터 시작하면 그 세션은 완성으로 저장하지 않는다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/functions/daily_linreg_trend_v1.h"

typedef struct {
    int32_t reg_period;     /* 일봉회귀기간입력 (5~100 클램프) */
    double min_r2;          /* 최소신뢰도입력 (추세 판정에 전달) */
    double price_scale;
} tr_dtl1_config_t;

typedef struct {
    tr_dtl1_config_t cfg;
    /* 세션 집계 */
    bool initialized;
    bool full_start;
    double sess_high, sess_low;
    int64_t last_start_bar;
    /* 완성 일봉 이력 */
    double day_mids[100];   /* 완성일봉중간, [0]=최신 */
    int32_t day_count;      /* 완성일수 */
    int32_t primed_count;   /* tr_dtl1_prime으로 채운 완성 일봉 수 (마지막 프라임 기준 기록) */
    /* 출력 */
    double reg_line;        /* 일봉회귀선 (1봉 투영) */
    double reg_slope;       /* 일봉회귀기울기 */
    double reg_r2;          /* 일봉회귀신뢰도 */
    double reg_residual;    /* 일봉회귀잔차 */
    tr_dlrt1_output_t trend;/* 일봉추세방향/상태/강도/유효 */
    bool link_valid;        /* 연결유효 */
} tr_dtl1_t;

void tr_dtl1_init(tr_dtl1_t *s, const tr_dtl1_config_t *cfg);

/* 봉 이벤트마다 호출한다 (진행 봉 재호출 포함). 재호출은 현재 세션의 H/L 집계와 회귀
 * 재계산만 갱신해 안전하고, 완성 일봉 저장은 bar_index 게이트(is_session_first &&
 * bar_index != last_start_bar)로 세션당 1회만 일어난다. is_session_first는 DayIndex==0 대응. */
void tr_dtl1_on_bar(tr_dtl1_t *s, double h, double l, double c,
                    bool is_session_first, int64_t bar_index, bool is_min_1);

/* 워밍업 프라임: 완성 세션 대표값((고+저)/2)을 **오래된 순**으로 주입해 과거 이력을 채운다.
 * - 이미 보유한 완성 일봉(실세션 집계분)의 뒤(더 과거)에 이어 붙인다 — 기존 이력과
 *   진행 중 세션 집계를 덮지 않는다 (프라임은 과거 채우기일 뿐이다).
 * - 주입 즉시 회귀 출력과 link_valid를 재계산한다 (추세 판정은 가격이 필요해 다음 봉에 이어짐).
 * - 이후 실세션이 완성되면 [0]에 push되어 프라임 이력은 뒤로 밀린다 (시간순 연속 유지). */
void tr_dtl1_prime(tr_dtl1_t *s, const double *day_mids, size_t n);

#endif
