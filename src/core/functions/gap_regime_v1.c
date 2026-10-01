#include "core/functions/gap_regime_v1.h"

#include <math.h>
#include <string.h>

void tr_gap1_init(tr_gap1_t *s, const tr_gap1_config_t *cfg) {
    memset(s, 0, sizeof(*s));
    if (cfg != 0) {
        s->cfg = *cfg;
    }
    s->last_start_bar = -1;
}

void tr_gap1_prime(tr_gap1_t *s, const double *session_trs, size_t n) {
    if (s == 0 || session_trs == 0 || n == 0) {
        return;
    }
    /* 이미 보유한 완성 세션의 뒤(더 과거)에 이어 붙인다. 진행 중 세션 집계와
     * prev_close/prev_prev_close/completed_days는 건드리지 않는다 — completed_days는
     * 실세션 완성 수라 첫 실세션의 TR 규칙(직전 종가 미보유 시 H−L)이 유지된다. */
    size_t k = (size_t)s->tr_count;
    size_t room = (sizeof(s->tr_array) / sizeof(s->tr_array[0])) - k;
    if (n > room) {
        session_trs += n - room; /* 버퍼가 모자라면 가장 오래된 입력부터 버린다 */
        n = room;
    }
    /* 입력은 오래된 순, 내부 버퍼는 [0]=최신 — 뒤집어 채운다 */
    for (size_t i = 0; i < n; i++) {
        s->tr_array[k + i] = session_trs[n - 1 - i];
    }
    s->tr_count = (int32_t)(k + n);
    s->primed_count += (int32_t)n;
}

void tr_gap1_on_bar(tr_gap1_t *s, double o, double h, double l, double c,
                    int32_t bar_time_min, bool is_session_first, int64_t bar_index, bool is_min_1) {
    if (s == 0 || !is_min_1) {
        return; /* 1분봉 전용 (원본 41줄 가드) */
    }
    int32_t n = s->cfg.volatility_period;
    if (n < 5) {
        n = 5;
    }
    if (n > 10) {
        n = 10;
    }

    if (!s->initialized) {
        s->initialized = true;
        s->full_start = is_session_first; /* 중간 시작 세션은 저장하지 않음 */
        s->sess_open = o;
        s->sess_high = h;
        s->sess_low = l;
        s->sess_close = c;
        s->sess_start_min = bar_time_min;
        s->last_start_bar = bar_index;
    } else if (is_session_first && bar_index != s->last_start_bar) {
        /* 세션 경계: 직전 세션 확정 */
        if (s->full_start) {
            /* 원본 순서: 종가 이전을 먼저 진행한 뒤 그 직전직전종가로 TR을 계산한다 */
            s->prev_prev_close = s->prev_close;
            s->prev_close = s->sess_close;
            double tr;
            if (s->completed_days == 0) {
                tr = s->sess_high - s->sess_low;
            } else {
                double hl = s->sess_high - s->sess_low;
                double hc = fabs(s->sess_high - s->prev_prev_close);
                double lc = fabs(s->sess_low - s->prev_prev_close);
                tr = fmax(hl, fmax(hc, lc));
            }
            for (int j = 18; j >= 0; j--) {
                s->tr_array[j + 1] = s->tr_array[j];
            }
            s->tr_array[0] = tr;
            if (s->tr_count < 20) {
                s->tr_count++;
            }
            s->completed_days++;
        }

        s->sess_open = o;
        s->sess_high = h;
        s->sess_low = l;
        s->sess_close = c;
        s->sess_start_min = bar_time_min;
        s->last_start_bar = bar_index;
        s->full_start = true;
    } else {
        if (h > s->sess_high) {
            s->sess_high = h;
        }
        if (l < s->sess_low) {
            s->sess_low = l;
        }
        s->sess_close = c;
    }

    /* 장시작경과분: 무효 상태에서도 매 봉 계산 */
    int32_t elapsed = bar_time_min - s->sess_start_min;
    if (elapsed < 0) {
        elapsed += 1440; /* 자정 경과 */
    }
    s->elapsed_min = elapsed;

    /* 갭 출력: 세션당 고정값. 유효 조건 미충족 시 0 */
    s->gap_ratio = 0.0;
    s->gap_grade = 0;
    s->daily_weight = 0.0;
    s->gap_dir = 0;
    s->valid = false;

    if (s->tr_count >= n && s->prev_close > 0.0) {
        double avg_tr = 0.0;
        for (int32_t j = 0; j < n; j++) {
            avg_tr += s->tr_array[j];
        }
        avg_tr /= (double)n;
        if (avg_tr > 0.0) {
            s->gap_ratio = fabs(s->sess_open - s->prev_close) / avg_tr;
            s->gap_dir = (s->sess_open > s->prev_close) - (s->sess_open < s->prev_close);
            if (s->gap_ratio >= s->cfg.mid_gap_threshold) {
                s->gap_grade = 1;
                s->daily_weight = 0.5;
            } else {
                s->daily_weight = 1.0;
            }
            if (s->gap_ratio >= s->cfg.big_gap_threshold) {
                s->gap_grade = 2;
                s->daily_weight = 0.0;
            }
            s->valid = true;
        }
    }
}
