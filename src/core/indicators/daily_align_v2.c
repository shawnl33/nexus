#include "core/indicators/daily_align_v2.h"

#include <math.h>
#include <string.h>

static double clamp01(double v) {
    if (v < 0.0) {
        return 0.0;
    }
    if (v > 1.0) {
        return 1.0;
    }
    return v;
}

void tr_dalign2_eval(const tr_dalign2_input_t *in, tr_dalign2_output_t *out) {
    memset(out, 0, sizeof(*out));
    if (in == 0) {
        return;
    }
    /* 합의 (원본 45~68) */
    int up = 0, down = 0;
    for (int k = 0; k < 3; k++) {
        if (in->pred_dir[k] > 0) {
            up++;
        }
        if (in->pred_dir[k] < 0) {
            down++;
        }
    }
    if (up >= 2) {
        out->consensus_dir = 1;
        out->consensus_state = up == 3 ? 2 : 1;
    }
    if (down >= 2) {
        out->consensus_dir = -1;
        out->consensus_state = down == 3 ? -2 : -1;
    }

    /* 적용일봉비중 (원본 39, 71~77) */
    double weight = clamp01(in->daily_weight_in);
    if (in->gap_grade >= 2) {
        weight = 0.0;
        if (in->elapsed_min >= in->big_gap_reeval_min) {
            weight = 0.5;
        }
    }
    out->applied_weight = weight;

    /* 게이트 (원본 79~81) */
    if (!in->reg_valid || in->r2 < in->min_r2 || out->consensus_dir == 0) {
        return;
    }

    double base = in->r2 * 100.0;
    if (out->consensus_state == 1 || out->consensus_state == -1) {
        base *= 0.75;
    }
    bool same = false, opposite = false;
    if (in->trend_valid) {
        same = out->consensus_dir == in->trend_dir;
        opposite = out->consensus_dir != in->trend_dir;
    }

    double strength = 0.0;
    bool valid = false;
    if (weight >= 1.0) {
        /* 일반장: 일봉과 같은 방향만 허용 */
        if (same) {
            strength = base + fmin(20.0, in->trend_strength * 0.20);
            valid = true;
        }
    } else {
        /* 갭장 */
        strength = base;
        if (same) {
            strength += fmin(20.0, in->trend_strength * 0.20) * weight;
        }
        if (opposite) {
            strength -= fmin(35.0, in->trend_strength * 0.35) * weight;
        }
        valid = true;
    }
    if (strength > 100.0) {
        strength = 100.0;
    }
    if (strength < 0.0) {
        strength = 0.0;
    }

    if (valid && strength >= in->min_final_strength) {
        out->final_dir = out->consensus_dir;
        out->final_strength = strength;
        out->final_state = out->final_dir;
        if ((out->consensus_state == 2 || out->consensus_state == -2) && strength >= 60.0) {
            out->final_state = out->final_dir * 2;
        }
        out->final_valid = true;
    }
}
