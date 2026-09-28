#ifndef TR_PAST_PREDICTION_H
#define TR_PAST_PREDICTION_H

/* 과거예측 검증 포팅 (메인 원본 190~279줄).
 *
 * - "예측봉수k봉 전에 계산된 예측가격"을 현재 봉에서 꺼내 실제 가격과 비교하는 검증부.
 * - 가변 룩백: MTF예측가격k[예측봉수k] — 런타임 파라미터 오프셋의 봉 인덱스 참조가 필요하다.
 * - 세션 번호: 30분 이하 분봉에서 당일 첫 봉(DayIndex==0)마다 +1 (원본 200~205).
 *   세션을 넘긴 예측은 무효화한다 (원본 224~226).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/market/ring.h"

typedef struct {
    double pred_price[3];  /* MTF예측가격1~3 */
    double r2;             /* 곡선회귀신뢰도 */
    int pred_dir[3];       /* MTF예측방향1~3 */
    bool reg_valid;        /* 곡선회귀유효 */
    uint64_t session_no;   /* 낶부에서 채움 */
} tr_ppv_frame_t;

typedef struct {
    int32_t predict_bars[3];  /* 예측봉수1~3 */
    bool compress_min_le30;   /* DataCompress==2 && BarInterval<=30 대응 */
    uint64_t session_no;      /* MTF세션번호 */
    int64_t session_mark;     /* MTF세션최근봉 */
    bool has_mark;
    tr_ring hist;             /* tr_ppv_frame_t, [0]=현재 봉 */
    /* 출력 (원본 대응) */
    double past_pred[3];      /* MTF지난예측1~3 */
    int past_valid[3];        /* MTF과거유효1~3 */
    double past_r2[3];        /* MTF과거신뢰1~3 */
    int past_dir[3];          /* MTF과거방향1~3 */
} tr_ppv_t;

bool tr_ppv_init(tr_ppv_t *s, const int32_t predict_bars[3], bool compress_min_le30,
                 tr_ppv_frame_t *storage, size_t capacity);

/* 매 봉 1회 호출. bar_index는 Index 대응, session_first는 DayIndex==0 대응. */
void tr_ppv_on_bar(tr_ppv_t *s, int64_t bar_index, bool session_first, const tr_ppv_frame_t *frame);

#endif
