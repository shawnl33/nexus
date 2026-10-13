#include "adapters/storage/live_bars.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "adapters/storage/store.h"
#include "core/backtest/bar_fill.h"

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

typedef struct {
    tr_candle_t *out;
    size_t cap;
    size_t n;
} live_collect_t;

static void live_collect(void *ctx, const tr_candle_t *c) {
    live_collect_t *col = ctx;
    if (col->n < col->cap) {
        col->out[col->n++] = *c;
    }
}

size_t tr_live_bars_query(uint64_t instrument_id, uint32_t timeframe, int64_t from_us, int64_t to_us,
                          tr_candle_t *out, size_t cap) {
    live_collect_t col;
    int n;
    if (g_store == 0 || out == 0 || cap == 0) {
        return 0;
    }
    col.out = out;
    col.cap = cap;
    col.n = 0;
    n = tr_store_query_candles(g_store, instrument_id, timeframe, from_us, to_us, cap, live_collect, &col);
    return n > 0 ? col.n : 0;
}

int tr_live_bars_fill(const tr_bar_fill_req_t *req, int64_t start_us, int64_t end_us, tr_bar_fetch_fn fetch,
                      void *ctx, char *err, size_t err_cap) {
    if (g_store == 0) {
        if (err != 0 && err_cap > 0) {
            snprintf(err, err_cap, "저장소가 열려 있지 않습니다");
        }
        return -1;
    }
    return tr_bar_fill(g_store, req, start_us, end_us, fetch, ctx, err, err_cap);
}

size_t tr_live_bars_load(uint64_t instrument_id, uint32_t timeframe, tr_candle_t *out, size_t cap) {
    if (g_store == 0 || out == 0 || cap == 0) {
        return 0;
    }
    int n = tr_store_query_recent_candles(g_store, instrument_id, timeframe, INT64_MAX, cap, out, cap);
    return n > 0 ? (size_t)n : 0;
}
