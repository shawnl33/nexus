#include "core/market/candle.h"

bool tr_candle_is_consistent(const tr_candle_t *c) {
    if (c == 0) {
        return false;
    }
    if (c->open_time_us >= c->close_time_us) {
        return false;
    }
    if (c->timeframe_sec == 0) {
        return false;
    }
    if (c->volume < 0) {
        return false;
    }
    if (c->high < c->low) {
        return false;
    }
    if (c->high < c->open || c->high < c->close) {
        return false;
    }
    if (c->low > c->open || c->low > c->close) {
        return false;
    }
    return true;
}
