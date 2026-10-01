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
    return true;
}

void tr_htf_curve_eval(tr_htf_curve_t *s, double high, double low) {
    /* 원본: 매 평가 시프트 후 최신값 삽입 */
    for (int j = 98; j >= 0; j--) {
        s->minclose[j + 1] = s->minclose[j];
    }
    s->x++;
    s->minclose[0] = (low + high) / 2.0;

    /* 원본: x좌표는 절대 카운터 X 기준 (X-18 .. X) */
    double y[TR_HTF_SAMPLES];
    for (int i = 0; i < TR_HTF_SAMPLES; i++) {
        y[i] = s->minclose[TR_HTF_SAMPLES - 1 - i]; /* 오래된 순 */
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
