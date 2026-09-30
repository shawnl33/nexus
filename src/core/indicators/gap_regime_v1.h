#ifndef TR_GAP_REGIME_V1_H
#define TR_GAP_REGIME_V1_H

/* WSF_GapRegimeV1 포팅 (원본 146줄, 완전 제공). 1분봉 전용.
 *
 * - 완성 세션의 TR을 최근 n개(변동기간, 5~10 클램프) 평균내어 갭 환경을 판별한다.
 * - 갭비율 = |당일 세션 시가 − 직전 완성 세션 종가| / 평균TR.
 * - 갭등급: 0 일반 / 1 중간(>=중간갭기준) / 2 큰 갭(>=큰갭기준).
 * - 일봉적용비중: 등급 0/1/2 → 1.0/0.5/0.0.
 * - 장시작경과분 = 현재 봉 시각(분) − 세션 시작 시각(분). 자정 경과 시 +1440.
 * - 유효: 완성 세션 TR이 n개 이상 && 직전 완성 종가 > 0 && 평균TR > 0.
 * - 첫 로딩 세션이 중간부터 시작하면(DayIndex!=0) 그 세션은 완성으로 저장하지 않는다.
 * - 갭비율·등급·방향은 세션당 고정값, 장시작경과분은 매 봉 증가.
 * - stime→분 변환(TimeToMinutes)은 호출자가 준비한다 (bar_time_min 입력).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t volatility_period; /* 변동기간입력 (5~10으로 클램프) */
    double mid_gap_threshold;  /* 중간갭기준입력 */
    double big_gap_threshold;  /* 큰갭기준입력 */
} tr_gap1_config_t;

typedef struct {
    tr_gap1_config_t cfg;
    /* 세션 집계 상태 */
    bool initialized;
    bool full_start;        /* 완전장시작확인 */
    double sess_open, sess_high, sess_low, sess_close;
    int32_t sess_start_min;
    int64_t last_start_bar; /* 최근장시작봉 */
    /* 완성 세션 이력 */
    double prev_close;      /* 직전완성종가 */
    double prev_prev_close; /* 직전직전종가 */
    double tr_array[20];    /* [0]=최신 완성 세션 TR */
    int32_t tr_count;
    int32_t primed_count;   /* tr_gap1_prime으로 채운 완성 세션 TR 수 (마지막 프라임 기준 기록) */
    int32_t completed_days; /* 완성일수 (첫 완성 세션 TR 규칙용, 실세션 완성만 센다) */
    /* 출력 */
    double gap_ratio;       /* 갭비율 */
    int gap_grade;          /* 갭등급: 0/1/2 */
    double daily_weight;    /* 일봉적용비중: 1.0/0.5/0.0 */
    int32_t elapsed_min;    /* 장시작경과분 */
    int gap_dir;            /* 갭방향: 1/0/−1 */
    bool valid;             /* 갭판별유효 */
} tr_gap1_t;

void tr_gap1_init(tr_gap1_t *s, const tr_gap1_config_t *cfg);

/* 봉 이벤트마다 호출한다 (진행 봉 재호출 포함). 재호출은 현재 세션 집계(H/L/C)·
 * 장시작경과분만 갱신해 안전하고, 완성 세션 저장은 bar_index 게이트(is_session_first &&
 * bar_index != last_start_bar)로 세션당 1회만 일어난다.
 * bar_time_min은 봉의 현지 시각(자정 기준 분). is_session_first는 DayIndex==0 대응. */
void tr_gap1_on_bar(tr_gap1_t *s, double o, double h, double l, double c,
                    int32_t bar_time_min, bool is_session_first, int64_t bar_index, bool is_min_1);

/* 워밍업 프라임: 완성 세션 TR(고−저)을 **오래된 순**으로 주입해 과거 이력을 채운다.
 * - 이미 보유한 완성 세션의 뒤(더 과거)에 이어 붙인다 — 기존 이력과 진행 중 세션 집계를
 *   덮지 않는다 (프라임은 과거 채우기일 뿐이다).
 * - prev_close/prev_prev_close/completed_days는 건드리지 않는다: 프라임 입력에는 종가가
 *   없으므로 첫 실세션 완성은 기존 첫 완성 규칙(TR=고−저)을 따르고, 그때 prev_close가
 *   채워지며 valid가 성립한다 (TR 개수 조건은 프라임으로 즉시 충족된다).
 * - 이후 실세션이 완성되면 [0]에 push되어 프라임 이력은 뒤로 밀린다 (시간순 연속 유지). */
void tr_gap1_prime(tr_gap1_t *s, const double *session_trs, size_t n);

#endif
