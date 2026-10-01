#include "core/functions/fx_curve_v1.h"

#include <math.h>
#include <string.h>

bool tr_fxc_init(tr_fxc_t *s) {
    if (s == 0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    /* memset 다음에 시계열 저장소를 연결한다 (순서 고정). 유효 용량 19 = 표본수 상한 */
    return ylv_init(&s->minclose, s->minclose_buf, 19);
}

void tr_fxc_eval(tr_fxc_t *s, const tr_fxc_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }

    /* 배열 이동은 봉변화에서만 (원본 15~25줄: Index != 최근봉) */
    if (in->is_new_bar || ylv_count(&s->minclose) == 0) {
        if (in->session_reset) { /* 원본 17~21줄: 배열·표본수·X 리셋 */
            ylv_clear(&s->minclose);
            s->x = 0;
        }
        /* 표본수 = Min(19,표본수+1) (원본 23줄) — 링 용량 19가 상한을 보장한다 */
        ylv_push(&s->minclose, (in->low + in->high) / 2.0);
        s->x++;
    } else {
        /* 진행 중 봉: 배열 이동 없이 최신 중간값만 갱신 (원본 28줄) */
        ylv_set_current(&s->minclose, (in->low + in->high) / 2.0);
    }

    /* 선형회귀 (원본 46~58줄): x좌표는 세션 상대 카운터 X 기준 (최신 [0]이 x=X).
     * 표본수 n은 실제 개수 — 워밍업 0 채움 없음. n>=2부터 회귀 (원본 56, 58줄) */
    size_t n = ylv_count(&s->minclose);
    double sumXY = 0.0, sumX = 0.0, sumY = 0.0, sumX2 = 0.0;
    for (size_t j = 0; j < n; j++) {
        double v = 0.0;
        ylv_at(&s->minclose, j, &v); /* [0]=최신 (x = X-j) */
        double xj = (double)(s->x - (int64_t)j);
        sumXY += xj * v;
        sumX += xj;
        sumY += v;
        sumX2 += xj * xj;
    }

    double mid = 0.0;
    ylv_at(&s->minclose, 0, &mid); /* MinClose[0] — n<2일 때 MinB 기본값 (원본 57줄) */
    s->slope = 0.0;
    s->intercept = mid;
    if (n >= 2) {
        double dn = (double)n;
        double denom = dn * sumX2 - sumX * sumX; /* 연속 정수 x라 n>=2에서 항상 > 0 */
        s->slope = (dn * sumXY - sumX * sumY) / denom;
        s->intercept = (sumY * sumX2 - sumX * sumXY) / denom;
    }

    double minlrl = s->slope * (double)s->x + s->intercept; /* MinLRL (원본 60줄) */

    /* 원본 63~65줄: 곡선하 = MinLRL[1], 리셋 봉은 현재값 */
    s->low_curve = in->session_reset ? minlrl : s->prev_minlrl;
    s->high_curve = minlrl;
    double pred_x = (double)(s->x + (int64_t)in->ticks); /* 곡선예측X (원본 67줄) */
    s->pred_price = s->slope * pred_x + s->intercept;
    s->change = fabs(s->pred_price - s->low_curve);
    s->direction = s->pred_price - s->low_curve;

    s->prev_minlrl = minlrl;
}

bool tr_fxc_relink(tr_fxc_t *s) {
    if (s == 0) {
        return false;
    }
    return ylv_relink(&s->minclose, s->minclose_buf);
}
