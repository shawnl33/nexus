#include "adapters/storage/live_bars.h"

#include <stdint.h>
#include <string.h>

#include "adapters/storage/store.h"

static tr_store_t *g_store;

int tr_live_bars_open(const char *path, char *err, size_t errlen) {
    tr_live_bars_close();
    if (path == 0 || path[0] == '\0') {
        return -1;
    }
    char local[256];
    if (err == 0) {
        err = local;
        errlen = sizeof(local);
    }
    err[0] = '\0';
    if (tr_store_open(path, &g_store, err, errlen) != TR_STORE_OK) {
        g_store = 0;
        return -1;
    }
    if (tr_store_migrate(g_store) != TR_STORE_OK) {
        tr_store_close(g_store);
        g_store = 0;
        if (errlen > 0 && err[0] == '\0') {
            strncpy(err, "migrate failed", errlen - 1);
            err[errlen - 1] = '\0';
        }
        return -1;
    }
    return 0;
}

void tr_live_bars_close(void) {
    if (g_store != 0) {
        tr_store_close(g_store);
        g_store = 0;
    }
}

void tr_live_bars_put(const tr_candle_t *bar) {
    if (g_store != 0 && bar != 0) {
        tr_store_put_candle(g_store, bar);
    }
}

size_t tr_live_bars_load(uint64_t instrument_id, uint32_t timeframe, tr_candle_t *out, size_t cap) {
    if (g_store == 0 || out == 0 || cap == 0) {
        return 0;
    }
    int n = tr_store_query_recent_candles(g_store, instrument_id, timeframe, INT64_MAX, cap, out, cap);
    return n > 0 ? (size_t)n : 0;
}
