#include "core/functions/linreg_predict_v4.h"

#include <math.h>
#include <string.h>

static double clamp_abs(double v, double limit) {
    if (v > limit) {
        return limit;
    }
    if (v < -limit) {
        return -limit;
    }
    return v;
}

static double clamp01(double v) {
    if (v < 0.0) {
        return 0.0;
    }
    if (v > 1.0) {
        return 1.0;
    }
    return v;
}

bool tr_lp4_init(tr_lp4_t *s, int32_t n1, int32_t n2, int32_t n3) {
    if (s == 0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->n[0] = n1;
    s->n[1] = n2;
    s->n[2] = n3;
    return tr_atr_init(&s->atr, 14);
}

void tr_lp4_on_bar_closed(tr_lp4_t *s, double high, double low, double close) {
    if (s == 0) {
        return;
    }
    tr_atr_on_bar(&s->atr, high, low, close);
}

void tr_lp4_eval(tr_lp4_t *s, const tr_lp4_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    /* 봉 인덱스 기준 입력 이력 갱신: 새 봉이면 시프트, 같은 봉이면 [0] 갱신 */
    if (in->is_new_bar || s->slope_len == 0) {
        if (s->slope_len < 4) {
            for (size_t i = s->slope_len; i > 0; i--) {
                s->slope_hist[i] = s->slope_hist[i - 1];
                s->day_hist[i] = s->day_hist[i - 1];
            }
            s->slope_len++;
            s->day_len++;
        } else {
            for (size_t i = 3; i > 0; i--) {
                s->slope_hist[i] = s->slope_hist[i - 1];
                s->day_hist[i] = s->day_hist[i - 1];
            }
        }
    }
    s->slope_hist[0] = in->slope;
    s->day_hist[0] = in->trading_day;

    /* 예측변동성은 원본과 같이 항상 ATR(14) (진행 봉 포함 추정) */
    double atr = tr_atr_candidate(&s->atr, in->high, in->low, in->close);
    s->volatility = atr;

    if (!in->reg_valid) {
        /* 원본 평탄 기본값 */
        for (int k = 0; k < 3; k++) {
            s->pred_price[k] = in->cur_line;
            s->pred_dir[k] = 0;
        }
        s->adj_slope = 0.0;
        s->accel = 0.0;
        s->valid_out = false;
        return;
    }

    double coef = clamp01((in->r2 - 0.20) / 0.50);
    s->adj_slope = clamp_abs(in->slope * coef, atr * 0.50);

    double raw_accel = 0.0;
    if (s->slope_len >= 4) {
        raw_accel = (in->slope - s->slope_hist[3]) / 3.0;
    }
    double accel = clamp_abs(raw_accel * coef, atr * 0.08);
    if (s->adj_slope * accel >= 0.0) {
        accel = 0.0; /* 같은 방향 가속 억제: 둔화/반전 성분만 사용 */
    }
    if (in->compress_min_le30 && s->day_len >= 4 && in->trading_day != s->day_hist[3]) {
        accel = 0.0; /* 3봉 창이 일자 경계를 걸치면 전일 기울기 차단 */
    }
    s->accel = accel;

    for (int k = 0; k < 3; k++) {
        double n = (double)s->n[k];
        double move = s->adj_slope * n + 0.5 * s->accel * n * n;
        double max_move = atr * 1.50 * sqrt((double)(s->n[k] > 1 ? s->n[k] : 1));
        move = clamp_abs(move, max_move);
        s->pred_price[k] = in->cur_line + move;
        s->pred_dir[k] = (move > 0.0) - (move < 0.0);
    }
    s->valid_out = true;
}
