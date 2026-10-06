#include "core/functions/ks_session_v1.h"

#include <string.h>

void tr_ks_session_init(tr_ks_session_t *s) {
    if (s == 0) {
        return;
    }
    memset(s, 0, sizeof(*s));
}

int64_t tr_ks_session_eval(tr_ks_session_t *s, bool is_new_bar, int32_t current_bar,
                           int64_t bdate, int32_t day_index) {
    if (s == 0) {
        return 0;
    }
    if (!is_new_bar) {
        return s->session_no;
    }
    /* 원본 6~9줄. [1]은 직전 새 봉에 저장해 둔 BDate·DayIndex다. */
    if (current_bar == 1) {
        s->session_no = 1;
    } else if (bdate != s->bdate || day_index < s->day_index ||
               (day_index == 0 && s->day_index != 0)) {
        s->session_no += 1;
    }
    s->bdate = bdate;
    s->day_index = day_index;
    s->has_bar = true;
    return s->session_no;
}
