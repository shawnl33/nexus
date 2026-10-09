#include "core/functions/fx_flat_count_v2.h"

#include <string.h>

void tr_fxflat_step(const tr_fxflat_out_t *prev, int session_reset,
                    double flat, double flat_prev, double high, double low,
                    double close, double close_prev, double price_scale, int min_bars,
                    tr_fxflat_out_t *out) {
    tr_fxflat_out_t b;
    if (out == 0) {
        return;
    }
    if (min_bars < 1) {
        min_bars = 1;
    }
    if (prev != 0) {
        b = *prev;
    } else {
        memset(&b, 0, sizeof(b));
    }
    if (session_reset) {
        memset(&b, 0, sizeof(b));
    }
    b.valid = flat > 0.0 ? 1 : 0;
    b.range_ticks = 0.0;
    if (b.valid && !session_reset && prev != 0 && flat_prev > 0.0) {
        int side = 0;
        if (close > flat && close_prev > flat_prev) {
            side = 1;
        } else if (close < flat && close_prev < flat_prev) {
            side = -1;
        }
        if (side == 1) {
            if (b.pos <= 0) {
                if (b.pos < 0 && -b.pos >= min_bars) {
                    b.prev_dn_pos = -b.pos;
                }
                b.pos = 1;
                b.hi = high;
                b.lo = low;
            } else {
                b.pos += 1;
                if (high > b.hi) b.hi = high;
                if (low < b.lo) b.lo = low;
            }
        } else if (side == -1) {
            if (b.pos >= 0) {
                if (b.pos > 0 && b.pos >= min_bars) {
                    b.prev_up_pos = b.pos;
                }
                b.pos = -1;
                b.hi = high;
                b.lo = low;
            } else {
                b.pos -= 1;
                if (high > b.hi) b.hi = high;
                if (low < b.lo) b.lo = low;
            }
        } else if (b.pos != 0) {
            if (high > b.hi) b.hi = high;
            if (low < b.lo) b.lo = low;
        }
        int step = 0;
        if (flat > flat_prev) {
            step = 1;
        } else if (flat < flat_prev) {
            step = -1;
        }
        if (step != 0 && step != b.slope_dir) {
            if (b.slope_dir == 1 && b.slope >= min_bars) {
                b.prev_up_slope = b.slope;
            }
            if (b.slope_dir == -1 && -b.slope >= min_bars) {
                b.prev_dn_slope = -b.slope;
            }
            b.slope_dir = step;
            b.slope = step;
        } else if (b.slope_dir != 0) {
            b.slope += b.slope_dir;
        }
    }
    if (b.pos != 0 && price_scale > 0.0) {
        b.range_ticks = (b.hi - b.lo) / price_scale;
    }
    *out = b;
}
