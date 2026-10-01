#include "core/functions/gap_regime_v1.h"

#include <math.h>
#include <string.h>

void tr_gap1_init(tr_gap1_t *s, const tr_gap1_config_t *cfg) {
    memset(s, 0, sizeof(*s));
    if (cfg != 0) {
        s->cfg = *cfg;
    }
    s->last_start_bar = -1;
    /* memset 다음에 시계열 저장소를 연결한다 (순서 고정) */
    ylv_init(&s->tr, s->tr_buf, sizeof(s->tr_buf) / sizeof(s->tr_buf[0]));
}

void tr_gap1_prime(tr_gap1_t *s, const double *session_trs, size_t n) {
    if (s == 0 || session_trs == 0 || n == 0) {
        return;
    }
    /* 이미 보유한 완성 세션의 뒤(더 과거)에 이어 붙인다. 진행 중 세션 집계와
     * prev_close/prev_prev_close/completed_days는 건드리지 않는다 — completed_days는
     * 실세션 완성 수라 첫 실세션의 TR 규칙(직전 종가 미보유 시 H−L)이 유지된다. */
    /* 링은 최신 push만 지원하므로, 기존 이력을 보관해 둔 뒤 프라임(오래된→최신)을 먼저
     * 채우고 기존 이력을 그 위에 다시 쌓아 "뒤에 이어 붙이기"를 재현한다.
     * 링이 비어 있으면(일반 호출 경로) 오래된 순 push와 정확히 동등하다. */
    size_t k = ylv_count(&s->tr);
    size_t room = (sizeof(s->tr_buf) / sizeof(s->tr_buf[0])) - k;
    if (n > room) {
        session_trs += n - room; /* 버퍼가 모자라면 가장 오래된 입력부터 버린다 */
        n = room;
    }
    /* 입력은 오래된 순, 내부 버퍼는 [0]=최신 — 보관 후 다시 쌓는다 */
    double keep[sizeof(s->tr_buf) / sizeof(s->tr_buf[0])];
    for (size_t i = 0; i < k; i++) {
        ylv_at(&s->tr, i, &keep[i]); /* 최신→과거 순 보관 */
    }
    ylv_clear(&s->tr);
    for (size_t i = 0; i < n; i++) {
        ylv_push(&s->tr, session_trs[i]); /* 입력은 오래된 순 */
    }
    for (size_t i = k; i-- > 0;) {
        ylv_push(&s->tr, keep[i]); /* 과거→최신 순 복원: 최종 [0]=기존 최신 */
    }
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
            ylv_push(&s->tr, tr);
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

    if (ylv_count(&s->tr) >= (size_t)n && s->prev_close > 0.0) {
        double avg_tr = 0.0;
        for (int32_t j = 0; j < n; j++) {
            double v = 0.0;
            ylv_at(&s->tr, (size_t)j, &v); /* 최신→과거 합산 순서 유지 */
            avg_tr += v;
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

bool tr_gap1_relink(tr_gap1_t *s) {
    if (s == 0) {
        return false;
    }
    return ylv_relink(&s->tr, s->tr_buf);
}
