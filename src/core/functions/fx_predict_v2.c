#include "core/functions/fx_predict_v2.h"

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

bool tr_fxp2_init(tr_fxp2_t *s) {
    if (s == 0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    /* memset 다음에 시계열 저장소를 연결한다 (순서 고정) */
    ylv_init(&s->slope_hist, s->slope_hist_buf, 4);
    return true;
}

void tr_fxp2_eval(tr_fxp2_t *s, const tr_fxp2_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }

    /* 회귀기울기입력 이력 갱신 (lp4와 같은 계약: 새 봉이면 push, 같은 봉이면 [0] 갱신) */
    if (in->is_new_bar || ylv_count(&s->slope_hist) == 0) {
        ylv_push(&s->slope_hist, in->slope);
    } else {
        ylv_set_current(&s->slope_hist, in->slope);
    }

    /* 세션TR/세션ATR (원본 36~40줄). 세션초기화 봉은 TR=H−L로 재시드한다 */
    double sess_tr = in->high - in->low;
    if (!in->session_reset && s->has_prev_close) {
        double hc = fabs(in->high - s->prev_close);
        double lc = fabs(in->low - s->prev_close);
        double m = fmax(hc, lc);
        sess_tr = fmax(sess_tr, m);
    }
    if (in->session_reset) {
        s->session_atr = sess_tr;
    } else if (in->session_bars <= 14) {
        s->session_atr = (s->session_atr * (double)(in->session_bars - 1) + sess_tr) /
                         (double)in->session_bars;
    } else {
        s->session_atr = (s->session_atr * 13.0 + sess_tr) / 14.0;
    }
    s->prev_close = in->close;
    s->has_prev_close = true;

    /* 예측봉수 (원본 42~46줄: Max(0,입력)) */
    double n[5];
    for (int k = 0; k < 5; k++) {
        n[k] = fmax(0.0, (double)in->horizons[k]);
    }

    /* 기본값 (원본 48~60줄: 예측가격 평탄, 방향 0, 예측변동성=세션ATR) */
    for (int k = 0; k < 5; k++) {
        s->pred_price[k] = in->cur_line;
        s->pred_dir[k] = 0;
    }
    s->adj_slope = 0.0;
    s->accel = 0.0;
    s->volatility = s->session_atr;

    if (in->reg_valid) { /* 원본 62~120줄 */
        double coef = (in->r2 - 0.20) / 0.50; /* 신뢰계수 */
        coef = fmax(0.0, fmin(1.0, coef));

        double atr = s->session_atr; /* ATR값 */
        double max_slope = atr * 0.50;
        double max_accel = atr * 0.08;

        s->adj_slope = clamp_abs(in->slope * coef, max_slope);

        double raw_accel = 0.0; /* 원가속도 = (기울기 − 기울기[3])/3 (원본 74줄) */
        if (ylv_count(&s->slope_hist) >= 4) {
            double s3 = 0.0;
            ylv_at(&s->slope_hist, 3, &s3);
            raw_accel = (in->slope - s3) / 3.0;
        }
        double accel = clamp_abs(raw_accel * coef, max_accel);
        if (s->adj_slope * accel >= 0.0) {
            accel = 0.0; /* 추세를 더 과장하지 않고 둔화/반전 성분만 (원본 78~79줄) */
        }
        if (in->session_bars <= 3) {
            accel = 0.0; /* 세션 3봉 이하는 전일 기울기를 섞지 않음 (원본 81~83줄) */
        }
        s->accel = accel;

        for (int k = 0; k < 5; k++) {
            /* 이동값/최대이동/예측가격/예측방향 (원본 85~117줄) */
            double move = s->adj_slope * n[k] + 0.5 * accel * n[k] * n[k];
            double max_move = atr * 1.50 * sqrt(fmax(1.0, n[k]));
            move = clamp_abs(move, max_move);
            s->pred_price[k] = in->cur_line + move;
            s->pred_dir[k] = (move > 0.0) - (move < 0.0);
        }
        s->volatility = atr;
    }
}

bool tr_fxp2_relink(tr_fxp2_t *s) {
    if (s == 0) {
        return false;
    }
    return ylv_relink(&s->slope_hist, s->slope_hist_buf);
}
