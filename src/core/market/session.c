#include "core/market/session.h"

#include "core/model/civil_time.h"

bool tr_session_policy_validate(const tr_session_policy_t *p) {
    if (p == 0) {
        return false;
    }
    if (p->utc_offset_min < -12 * 60 || p->utc_offset_min > 14 * 60) {
        return false;
    }
    if (p->open_min >= 1440 || p->close_min >= 1440) {
        return false;
    }
    if (p->open_min == p->close_min) {
        return false; /* 24시간 세션은 별도 정책으로 표현한다 */
    }
    if (p->days_mask == 0) {
        return false;
    }
    return true;
}

static bool session_opens_on(const tr_session_policy_t *p, int64_t local_day) {
    unsigned wd = tr_weekday_from_days(local_day);
    return (p->days_mask & (uint8_t)(1u << wd)) != 0;
}

static void session_instant(int64_t local_day, uint32_t min, int32_t offset_min, tr_time_us_t *out) {
    tr_time_us_t midnight;
    tr_local_midnight(local_day, offset_min, &midnight);
    *out = midnight + (int64_t)min * TR_US_PER_MIN;
}

bool tr_session_span(const tr_session_policy_t *p, tr_time_us_t t,
                     tr_time_us_t *out_open_us, tr_time_us_t *out_close_us) {
    if (!tr_session_policy_validate(p)) {
        return false;
    }
    bool overnight = p->close_min <= p->open_min;
    int64_t local_day;
    uint32_t local_min;
    tr_local_day_and_min(t, p->utc_offset_min, &local_day, &local_min);

    int64_t open_day = -1;
    if (session_opens_on(p, local_day) && local_min >= p->open_min &&
        (overnight || local_min < p->close_min)) {
        open_day = local_day; /* 당일 개장 세션 */
    } else if (overnight && session_opens_on(p, local_day - 1) && local_min < p->close_min) {
        open_day = local_day - 1; /* 전일 개장 야간 세션의 자정 이후 구간 */
    } else {
        return false;
    }

    if (out_open_us != 0) {
        session_instant(open_day, p->open_min, p->utc_offset_min, out_open_us);
    }
    if (out_close_us != 0) {
        int64_t close_day = overnight ? open_day + 1 : open_day;
        session_instant(close_day, p->close_min, p->utc_offset_min, out_close_us);
    }
    return true;
}

bool tr_session_is_open(const tr_session_policy_t *p, tr_time_us_t t) {
    return tr_session_span(p, t, 0, 0);
}

bool tr_session_trading_day(const tr_session_policy_t *p, tr_time_us_t t, int64_t *out_open_day) {
    if (out_open_day == 0) {
        return false;
    }
    if (!tr_session_policy_validate(p)) {
        return false;
    }
    bool overnight = p->close_min <= p->open_min;
    int64_t local_day;
    uint32_t local_min;
    tr_local_day_and_min(t, p->utc_offset_min, &local_day, &local_min);

    if (session_opens_on(p, local_day) && local_min >= p->open_min &&
        (overnight || local_min < p->close_min)) {
        *out_open_day = local_day;
        return true;
    }
    if (overnight && session_opens_on(p, local_day - 1) && local_min < p->close_min) {
        *out_open_day = local_day - 1;
        return true;
    }
    return false;
}
