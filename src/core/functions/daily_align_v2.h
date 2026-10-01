#ifndef TR_DAILY_ALIGN_V2_H
#define TR_DAILY_ALIGN_V2_H

/* WSF_1m_DailyAlignV2 포팅 (원본 142줄, 완전 제공, 무상태).
 *
 * 1분봉 예측 3종의 합의를 만들고 일봉 추세·갭 레짐에 따라 가감점·필터를 적용한다.
 * - 합의: 3개 예측 중 2개 이상 같은 부호 → 그 방향. 상태: 2/3 합의 ±1, 3/3 만장일치 ±2.
 * - 기본강도 = 신뢰도×100 (2/3 합의면 ×0.75).
 * - 적용일봉비중: 갭등급>=2(큰 갭)이면 0, 장 경과분 >= 재평가분이면 0.5로 복원.
 * - 일반장(비중>=1): 일봉과 같은 방향만 허용 +Min(20, 일봉강도×0.20).
 * - 갭장(비중<1): 같은 방향 +Min(20,강도×0.20)×비중, 반대 −Min(35,강도×0.35)×비중, 유효.
 * - 최종 상태: |합의상태|==2 && 강도>=60이면 방향×2, 아니면 방향×1.
 * - 일봉추세상태입력은 원본에서 미사용(죽은 인자) — 시그니처 호환을 위해 유지하되 무시.
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int pred_dir[3];          /* 분봉예측방향1~3입력 */
    bool reg_valid;           /* 분봉회귀유효입력 (실제로는 곡선회귀유효) */
    double r2;                /* 분봉회귀신뢰도입력 */
    int trend_dir;            /* 일봉추세방향입력 */
    int trend_state;          /* 일봉추세상태입력 (원본 미사용, 무시) */
    double trend_strength;    /* 일봉추세강도입력 */
    bool trend_valid;         /* 일봉추세유효입력 */
    int gap_grade;            /* 갭등급입력 */
    double daily_weight_in;   /* 일봉적용비중입력 */
    double elapsed_min;       /* 장시작경과분입력 */
    double big_gap_reeval_min;/* 큰갭재평가분입력 */
    double min_r2;            /* 분봉최소신뢰도입력 */
    double min_final_strength;/* 최종최소강도입력 */
} tr_dalign2_input_t;

typedef struct {
    int consensus_dir;        /* 분봉합의방향 */
    int consensus_state;      /* 분봉합의상태 */
    double applied_weight;    /* 적용일봉비중 */
    int final_dir;            /* 일치방향 */
    int final_state;          /* 일치상태 */
    double final_strength;    /* 일치강도 */
    bool final_valid;         /* 일치유효 */
} tr_dalign2_output_t;

void tr_dalign2_eval(const tr_dalign2_input_t *in, tr_dalign2_output_t *out);

#endif
