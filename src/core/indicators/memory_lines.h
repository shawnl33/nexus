#ifndef TR_MEMORY_LINES_H
#define TR_MEMORY_LINES_H

/* 고정 기억선·지속선 상태 기계 포팅 (메인 원본 553~799줄).
 *
 * 회귀기억(방향 전환 고정선, 원본 553~711):
 * - 회귀기본방향: 운영최종방향(MISSING_DEPENDENCY 입력) 우선, 없으면 회귀선_구분 부호.
 * - 호가 관성: 분봉에서 방향이 바뀌어도 호가가 동의할 때까지 기존 방향 유지 (원본 578~607).
 * - 저장 조건: 회귀유효 && 현재방향!=0 && 예측가격1~3!=0 && (무효 또는 방향 전환) (원본 630~647).
 *   목표·상하단은 틱 단위 양자화(round(x/PriceScale,0)*PriceScale). 저장된 그 봉에는 밴드 숨김.
 * - 세션 변경 시 리셋(30분 이하 분봉). 5봉 연속 목표3 이탈 시 반대편 밴드 숨김.
 *
 * 지속선(원본 719~799):
 * - MTF예측방향2가 Max(1,지속봉수)봉 연속 동일해진 정확히 그 봉에만 저장 (== 조건).
 * - 저장 조건에 신뢰도 >= 최소신뢰도 포함. 숨김 규칙은 기억선과 동일.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------- 회귀기억 ---------- */

typedef struct {
    double price_scale;
    double steep_slope_ticks;   /* 가파른기울기틱 */
    int32_t steep_extra_bars;   /* 가파른추가확인봉 */
    bool show_range;            /* 과거범위표시_적용 */
} tr_regmem_config_t;

typedef struct {
    bool reg_valid;             /* 곡선회귀유효 */
    double line_flat;           /* 곡선회귀선_평탄 */
    int line_sign;              /* 곡선회귀선_구분 */
    double slope;               /* 곡선회귀기울기 */
    int final_dir;              /* 운영최종방향 (MISSING_DEPENDENCY 입력, 없으면 0) */
    bool final_dir_valid;       /* 운영최종유효 */
    double pred_price[3];       /* MTF예측가격1~3 */
    double upper[3], lower[3];  /* MTF상단/하단1~3 */
    uint64_t session_no;        /* MTF세션번호 */
    bool compress_min;          /* DataCompress==2 */
    bool compress_min_le30;     /* && BarInterval<=30 */
    bool ob_applicable;         /* 호가적용가능 */
    int ob_state;               /* 호가방향상태 */
    const double *h5, *l5;      /* 최근 5봉 H/L ([0]=현재). hl_count<5이면 이탈 검사 생략 */
    size_t hl_count;
} tr_regmem_input_t;

typedef struct {
    tr_regmem_config_t cfg;
    int current_dir;            /* 회귀현재방향 */
    int mem_dir;                /* 회귀기억방향 */
    bool mem_valid;             /* 회귀기억유효 */
    double mem_price;           /* 회귀기억가격 */
    double mem_target[3];       /* 회귀기억목표1~3 */
    double mem_upper[3], mem_lower[3]; /* 회귀기억상단/하단1~3 */
    uint64_t mem_session;       /* 회귀기억세션 */
    int confirm_accum;          /* 호가확인누적 */
    bool updated;               /* 회귀기억갱신 (저장된 그 봉만 true) */
    bool session_reset;         /* 세션 리셋 봉 표시 (리셋 분기에서 set, 저장과 무관) */
    /* 출력 */
    bool show_targets;          /* Plot32~35 표시 여부 */
    bool show_upper, show_lower;/* Plot36~41 표시 여부 */
} tr_regmem_t;

void tr_regmem_init(tr_regmem_t *s, const tr_regmem_config_t *cfg);
void tr_regmem_on_bar(tr_regmem_t *s, const tr_regmem_input_t *in);

/* ---------- 지속선 ---------- */

typedef struct {
    double price_scale;
    int32_t persist_bars;       /* 지속봉수 */
    double min_r2;              /* 최소신뢰도 */
} tr_persist_config_t;

typedef struct {
    bool reg_valid;             /* 곡선회귀유효 */
    double r2;                  /* 곡선회귀신뢰도 */
    int pred_dir2;              /* MTF예측방향2 */
    double pred_price[3], upper[3], lower[3];
    const double *h5, *l5;
    size_t hl_count;
} tr_persist_input_t;

typedef struct {
    tr_persist_config_t cfg;
    bool started;               /* CurrentBar==1 이후 */
    int streak;                 /* 지속연속봉수 */
    int prev_dir2;              /* MTF예측방향2[1] */
    bool saved_valid;           /* 지속저장유효 */
    int saved_dir;              /* 지속저장방향 */
    double target[3], upper[3], lower[3];
    bool show_upper, show_lower;
} tr_persist_t;

void tr_persist_init(tr_persist_t *s, const tr_persist_config_t *cfg);
void tr_persist_on_bar(tr_persist_t *s, const tr_persist_input_t *in);

#endif
