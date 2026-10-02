#ifndef TR_FX_ALIGN_V1_H
#define TR_FX_ALIGN_V1_H

/* WSF_FXAlignV1 (원본 142줄, 무상태).
 * 1분 예측 3개의 합의에 일봉 추세·갭 비중을 더한다.
 * 수식은 국내 WSF_1m_DailyAlignV2와 같다. 일봉추세상태는 원본에서 쓰이지 않는다.
 */

#include "core/functions/daily_align_v2.h"

typedef tr_dalign2_input_t tr_fxalign_input_t;
typedef tr_dalign2_output_t tr_fxalign_output_t;

void tr_fxalign_eval(const tr_fxalign_input_t *in, tr_fxalign_output_t *out);

#endif
