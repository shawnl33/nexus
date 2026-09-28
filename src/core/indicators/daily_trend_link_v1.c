#include "core/indicators/daily_trend_link_v1.h"

#include <string.h>

#include "core/indicators/linreg.h"

void tr_dtl1_init(tr_dtl1_t *s, const tr_dtl1_config_t *cfg) {
    memset(s, 0, sizeof(*s));
    if (cfg != 0) {
        s->cfg = *cfg;
    }
    s->last_start_bar = -1;
}

void tr_dtl1_on_bar(tr_dtl1_t *s, double h, double l, double c,
                    bool is_session_first, int64_t bar_index, bool is_min_1) {
    if (s == 0) {
        return;
    }
    int32_t n = s->cfg.reg_period;
    if (n < 5) {
        n = 5;
    }
    if (n > 100) {
        n = 100;
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
                for (int j = 98; j >= 0; j--) {
                    s->day_mids[j + 1] = s->day_mids[j];
                }
                s->day_mids[0] = (s->sess_high + s->sess_low) / 2.0;
                if (s->day_count < 100) {
                    s->day_count++;
                }
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
    s->link_valid = false;
    if (is_min_1 && s->day_count >= n) {
        double y[100];
        for (int32_t j = 0; j < n; j++) {
            y[j] = s->day_mids[n - 1 - j]; /* 오래된 일봉이 x=0 */
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
