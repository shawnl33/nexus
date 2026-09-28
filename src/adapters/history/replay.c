#include "adapters/history/replay.h"

bool tr_replay_run(tr_bar_builder_t *bb, const tr_replay_tick_t *events, size_t n,
                   tr_time_us_t end_us, tr_replay_result_t *out) {
    if (bb == 0 || (events == 0 && n > 0) || out == 0) {
        return false;
    }
    out->fed = 0;
    out->rejected = 0;
    out->first_bad_index = (size_t)-1;

    tr_time_us_t prev_t = 0;
    for (size_t i = 0; i < n; i++) {
        tr_time_us_t t = events[i].env.event_time_us;
        if (i > 0 && t < prev_t) {
            out->first_bad_index = i; /* 입력 기록 자체가 정렬되어 있지 않음 */
            return false;
        }
        prev_t = t;
        tr_bar_builder_on_timer(bb, t);
        tr_bb_status_t st = tr_bar_builder_on_tick(bb, &events[i].env, &events[i].tick);
        if (st == TR_BB_ERROR || st == TR_BB_REJECTED_OUT_OF_SESSION) {
            out->rejected++;
        } else {
            out->fed++;
        }
    }
    tr_bar_builder_on_timer(bb, end_us);
    return true;
}
