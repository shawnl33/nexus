#include "core/functions/daily_trend_link_v1.h"

#include <string.h>

#include "core/functions/linreg.h"

void tr_dtl1_init(tr_dtl1_t *s, const tr_dtl1_config_t *cfg) {
    memset(s, 0, sizeof(*s));
    if (cfg != 0) {
        s->cfg = *cfg;
    }
    s->last_start_bar = -1;
    /* memset 다음에 시계열 저장소를 연결한다 (순서 고정) */
    ylv_init(&s->day_mids, s->day_mids_buf, sizeof(s->day_mids_buf) / sizeof(s->day_mids_buf[0]));
}

/* 회귀 + 유효 재계산 (원본 33~41 기본값 후 갱신). 무효면 회귀선은 c로 둔다 (원본 기본값). */
static void dtl1_update_regression(tr_dtl1_t *s, double c, bool is_min_1) {
    int32_t n = s->cfg.reg_period;
    if (n < 5) {
        n = 5;
    }
    if (n > 100) {
        n = 100;
    }
    s->link_valid = false;
    if (is_min_1 && ylv_count(&s->day_mids) >= (size_t)n) {
        double y[100];
        for (int32_t j = 0; j < n; j++) {
            ylv_at(&s->day_mids, (size_t)(n - 1 - j), &y[j]); /* 오래된 일봉이 x=0 */
        }
        tr_ols_result_t r;
        tr_ols_fit(y, (size_t)n, 0.0, 1, &r);
        s->reg_slope = r.slope;
        double last_fit = r.slope * (double)(n - 1) + r.intercept;
        s->reg_line = last_fit + r.slope; /* 오늘 위치로 1봉 투영 */
        s->reg_r2 = r.r2;
        s->reg_residual = r.residual_sd;
        s->link_valid = true;
    } else {
        /* 원본 기본값: 회귀선=C, 나머지 0 */
        s->reg_line = c;
        s->reg_slope = 0.0;
        s->reg_r2 = 0.0;
        s->reg_residual = 0.0;
    }
}

void tr_dtl1_prime(tr_dtl1_t *s, const double *day_mids, size_t n) {
    if (s == 0 || day_mids == 0 || n == 0) {
        return;
    }
    /* 이미 보유한 완성 일봉의 뒤(더 과거)에 이어 붙인다. 진행 중 세션 집계
     * (initialized/sess_high/low/full_start)는 건드리지 않는다. */
    size_t k = ylv_count(&s->day_mids);
    size_t room = (sizeof(s->day_mids_buf) / sizeof(s->day_mids_buf[0])) - k;
    if (n > room) {
        day_mids += n - room; /* 버퍼가 모자라면 가장 오래된 입력부터 버린다 */
        n = room;
    }
    /* 입력은 오래된 순, 내부 버퍼는 [0]=최신이다.
     * 링은 최신 push만 지원하므로, 기존 이력을 보관해 둔 뒤 프라임(오래된→최신)을 먼저 채우고
     * 기존 이력을 그 위에 다시 쌓는다. 링이 비어 있으면(일반 호출 경로) 오래된 순 push와 정확히 동등하다. */
    double keep[sizeof(s->day_mids_buf) / sizeof(s->day_mids_buf[0])];
    for (size_t i = 0; i < k; i++) {
        ylv_at(&s->day_mids, i, &keep[i]); /* 최신→과거 순 보관 */
    }
    ylv_clear(&s->day_mids);
    for (size_t i = 0; i < n; i++) {
        ylv_push(&s->day_mids, day_mids[i]); /* 입력은 오래된 순 */
    }
    for (size_t i = k; i-- > 0;) {
        ylv_push(&s->day_mids, keep[i]); /* 과거→최신 순 복원: 최종 [0]=기존 최신 */
    }
    s->primed_count += (int32_t)n;
    /* link_valid·회귀 출력을 즉시 재계산한다. 무효일 때 회귀선 자리에는
     * 가장 최근 완성 일봉 중간값을 둔다 (원본 기본값 C에 해당하는 현재가 대리) */
    double cur = 0.0;
    ylv_at(&s->day_mids, 0, &cur);
    dtl1_update_regression(s, cur, true);
}

void tr_dtl1_on_bar(tr_dtl1_t *s, double h, double l, double c,
                    bool is_session_first, int64_t bar_index, bool is_min_1) {
    if (s == 0) {
        return;
    }

    if (is_min_1) {
        if (!s->initialized) {
            s->initialized = true;
            s->full_start = is_session_first;
            s->sess_high = h;
            s->sess_low = l;
            s->last_start_bar = bar_index;
        } else if (is_session_first && bar_index != s->last_start_bar) {
            /* 세션 경계: 직전 세션을 완성 일봉으로 저장 */
            if (s->full_start) {
                ylv_push(&s->day_mids, (s->sess_high + s->sess_low) / 2.0);
            }
            s->sess_high = h;
            s->sess_low = l;
            s->last_start_bar = bar_index;
            s->full_start = true;
        } else {
            if (h > s->sess_high) {
                s->sess_high = h;
            }
            if (l < s->sess_low) {
                s->sess_low = l;
            }
        }
    }

    /* 회귀 + 추세 판정: 매 봉 재계산 (원본 33~41 기본값 후 갱신) */
    dtl1_update_regression(s, c, is_min_1);

    tr_dlrt1_input_t tin;
    tin.line = s->reg_line;
    tin.slope = s->reg_slope;
    tin.reg_valid = s->link_valid;
    tin.r2 = s->reg_r2;
    tin.residual = s->reg_residual;
    tin.price = c;
    tin.min_r2 = s->cfg.min_r2;
    tin.price_scale = s->cfg.price_scale;
    tr_dlrt1_eval(&tin, &s->trend);
}

bool tr_dtl1_relink(tr_dtl1_t *s) {
    if (s == 0) {
        return false;
    }
    return ylv_relink(&s->day_mids, s->day_mids_buf);
}
