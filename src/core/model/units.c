#include "core/model/units.h"

bool tr_scale_is_valid(int64_t scale) {
    if (scale <= 0) {
        return false;
    }
    while (scale % 10 == 0) {
        scale /= 10;
    }
    return scale == 1;
}

bool tr_price_to_double_checked(tr_price_t raw, int64_t scale, double *out) {
    if (out == 0 || !tr_scale_is_valid(scale)) {
        return false;
    }
    *out = (double)raw / (double)scale;
    return true;
}
