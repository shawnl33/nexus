#include "adapters/history/csv_ticks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

tr_replay_tick_t *tr_csv_ticks_load(const char *path, uint64_t instrument_id, size_t *out_count) {
    if (path == 0 || out_count == 0) {
        return 0;
    }
    FILE *fp = fopen(path, "r");
    if (fp == 0) {
        return 0;
    }
    size_t cap = 1024;
    size_t n = 0;
    tr_replay_tick_t *arr = (tr_replay_tick_t *)malloc(cap * sizeof(tr_replay_tick_t));
    if (arr == 0) {
        fclose(fp);
        return 0;
    }
    char line[256];
    uint64_t row = 0;
    while (fgets(line, sizeof(line), fp) != 0) {
        char *p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) {
            continue;
        }
        long long t, price, qty;
        if (sscanf(p, "%lld , %lld , %lld", &t, &price, &qty) != 3) {
            continue; /* 잘못된 줄은 건성하고 계속한다 (기록은 호출자 몫) */
        }
        if (qty < 0) {
            continue;
        }
        row++;
        if (n == cap) {
            cap *= 2;
            tr_replay_tick_t *grown = (tr_replay_tick_t *)realloc(arr, cap * sizeof(tr_replay_tick_t));
            if (grown == 0) {
                free(arr);
                fclose(fp);
                return 0;
            }
            arr = grown;
        }
        memset(&arr[n], 0, sizeof(arr[n]));
        arr[n].env.kind = TR_EVENT_TICK;
        arr[n].env.event_time_us = (int64_t)t;
        arr[n].env.received_time_us = (int64_t)t;
        arr[n].tick.instrument_id = instrument_id;
        arr[n].tick.price = (tr_price_t)price;
        arr[n].tick.qty = (tr_qty_t)qty;
        arr[n].tick.source_exec_id = row;
        arr[n].tick.volume_meaning = TR_TICK_VOLUME_PER_TRADE;
        n++;
    }
    fclose(fp);
    *out_count = n;
    return arr;
}

void tr_csv_ticks_free(tr_replay_tick_t *ticks) {
    free(ticks);
}
