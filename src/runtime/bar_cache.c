#include "runtime/bar_cache.h"

#include <string.h>

static void push_newest(tr_candle_t *out, size_t cap, size_t *n, const tr_candle_t *bar) {
    if (*n < cap) {
        out[(*n)++] = *bar;
        return;
    }
    if (cap == 0) {
        return;
    }
    memmove(out, out + 1, (cap - 1) * sizeof(*out));
    out[cap - 1] = *bar;
}

size_t tr_bar_cache_merge(const tr_candle_t *broker, size_t nbroker,
                          const tr_candle_t *cache, size_t ncache,
                          tr_candle_t *out, size_t cap) {
    if (out == 0 || cap == 0) {
        return 0;
    }
    size_t i = 0, j = 0, n = 0;
    while (i < nbroker || j < ncache) {
        const tr_candle_t *pick;
        if (broker == 0 || i >= nbroker) {
            pick = &cache[j++];
        } else if (cache == 0 || j >= ncache) {
            pick = &broker[i++];
        } else if (broker[i].open_time_us < cache[j].open_time_us) {
            pick = &broker[i++];
        } else if (cache[j].open_time_us < broker[i].open_time_us) {
            pick = &cache[j++];
        } else if (cache[j].revision > broker[i].revision) {
            pick = &cache[j];
            i++;
            j++;
        } else {
            pick = &broker[i];
            i++;
            j++;
        }
        push_newest(out, cap, &n, pick);
    }
    return n;
}
