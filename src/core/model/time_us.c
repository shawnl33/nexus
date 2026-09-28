#include "core/model/time_us.h"

bool tr_time_us_add(tr_time_us_t t, int64_t delta_us, tr_time_us_t *out) {
    if (out == 0) {
        return false;
    }
    if (delta_us > 0 && t > INT64_MAX - delta_us) {
        return false;
    }
    if (delta_us < 0 && t < INT64_MIN - delta_us) {
        return false;
    }
    *out = t + delta_us;
    return true;
}
