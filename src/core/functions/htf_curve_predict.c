#include "core/functions/htf_curve_predict.h"

#include <math.h>
#include <string.h>

#include "core/functions/linreg.h"

bool tr_htf_curve_init(tr_htf_curve_t *s, int32_t predict_ticks) {
    if (s == 0 || predict_ticks < 0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->ticks = predict_ticks;
    /* memset 다음에 시계열 저장소를 연결한다 (순서 고정) */
    return ylv_init(&s->minclose, s->minclose_buf, TR_HTF_SAMPLES);
}

void tr_htf_curve_eval(tr_htf_curve_t *s, double high, double low) {
    /* 원본: 매 평가 시프트 후 최신값 삽입 (봉당 1값이 아니라 평가당 push) */
    ylv_push(&s->minclose, (low + high) / 2.0);
    s->x++;

    /* 원본: x좌표는 절대 카운터 X 기준 (X-18 .. X).
     * 워밍업(count<19)은 원본의 0 채움 배열 읽기와 같게 없는 슬롯을 0으로 둔다 */
    double y[TR_HTF_SAMPLES];
    for (int i = 0; i < TR_HTF_SAMPLES; i++) {
        double v = 0.0;
        ylv_at(&s->minclose, (size_t)(TR_HTF_SAMPLES - 1 - i), &v); /* 오래된 순 */
        y[i] = v;
    }
    tr_ols_result_t r;
    tr_ols_fit(y, TR_HTF_SAMPLES, (double)(s->x - (TR_HTF_SAMPLES - 1)), 1, &r);

    double minlrl = r.slope * (double)s->x + r.intercept;

    s->low_curve = s->has_prev ? s->prev_minlrl : 0.0;
    s->high_curve = minlrl;
    s->pred_price = r.slope * (double)(s->x + s->ticks) + r.intercept;
    s->change = fabs(s->pred_price - s->low_curve);
    s->direction = s->pred_price - s->low_curve;
    s->slope = r.slope;
    s->intercept = r.intercept;

    s->prev_minlrl = minlrl;
    s->has_prev = true;
    s->evals++;
}

tr_validity_t tr_htf_curve_validity(const tr_htf_curve_t *s) {
    if (s == 0) {
        return TR_VALIDITY_MISSING;
    }
    /* 현재값 출력은 19봉, [1] 의존 출력(곡선하/변화/방향) 포함 전체 유효는 20봉 */
    return s->evals >= 20 ? TR_VALIDITY_VALID : TR_VALIDITY_MISSING;
}

bool tr_htf_curve_relink(tr_htf_curve_t *s) {
    if (s == 0) {
        return false;
    }
    return ylv_relink(&s->minclose, s->minclose_buf);
}
