#include "core/indicators/score_1m.h"

#include <string.h>

bool tr_score1m_init(tr_score1m_t *s, uint32_t market_period, double min_r2,
                     double *storage, size_t capacity) {
    if (s == 0 || market_period == 0 || storage == 0 || capacity < market_period) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->market_period = market_period;
    s->min_r2 = min_r2;
    /* 외부 저장소 부착 모델 유지: 호출자 소유 버퍼를 그대로 연결한다 */
    return ylv_init(&s->mid_hist, storage, capacity);
}

void tr_score1m_on_bar(tr_score1m_t *s, const tr_score1m_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    if (!in->is_min_1) {
        s->active = false;
        s->score = 0;
        return;
    }
    s->active = true;

    /* 핵심세션첫봉 (원본 927~928) */
    bool session_first = !s->first_bar_done || (s->has_prev_day && in->trading_day != s->prev_day);

    /* 핵심세션미래방향 (원본 935~936): 수치 보존, 세션 첫 봉은 0 */
    double prev_future = s->has_prev_future ? s->prev_future_dir : 0.0;
    s->future_dir = session_first ? 0.0 : in->htf_direction;

    /* 핵심마켓 (원본 951~956): (H+L)/2 의 단순 평균 */
    double mid = (in->high + in->low) / 2.0;
    ylv_push(&s->mid_hist, mid);
    double sum = 0.0;
    size_t cnt = ylv_count(&s->mid_hist);
    for (size_t i = 0; i < cnt; i++) {
        double v = 0.0;
        ylv_at(&s->mid_hist, i, &v); /* 최신→과거 합산 순서 유지 */
        sum += v;
    }
    s->market_center = cnt > 0 ? sum / (double)cnt : mid;
    double market_slope = s->has_prev_market ? s->market_center - s->prev_market_center : 0.0;

    s->market_dir = 0;
    if (in->close > s->market_center && market_slope > 0.0) {
        s->market_dir = 1;
    }
    if (in->close < s->market_center && market_slope < 0.0) {
        s->market_dir = -1;
    }

    /* 핵심회귀방향 (원본 944~949) */
    s->reg_dir = 0;
    if (in->reg_valid && in->reg_r2 >= s->min_r2) {
        if (in->close > in->reg_flat_line) {
            s->reg_dir = 1;
        }
        if (in->close < in->reg_flat_line) {
            s->reg_dir = -1;
        }
    }

    /* 핵심호가방향 (원본 961~962) */
    s->ob_dir = (in->ob_score > 0.0) - (in->ob_score < 0.0);

    /* 단계화_1분_통합 (원본 964~968). 방향 0은 −2 분기로 간다 */
    s->score = (s->future_dir > 0.0 ? 2 : -2) +
               (s->future_dir > prev_future ? 1 : 0) +
               (s->future_dir < prev_future ? -1 : 0) +
               s->market_dir + s->reg_dir + s->ob_dir;

    s->prev_future_dir = s->future_dir;
    s->has_prev_future = true;
    s->prev_market_center = s->market_center;
    s->has_prev_market = true;
    s->prev_day = in->trading_day;
    s->has_prev_day = true;
    s->first_bar_done = true;
}
