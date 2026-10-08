#include "adapters/nh/nh_chart.h"

#include <ctype.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adapters/nh/nh_http.h"
#include "core/model/civil_time.h"
#include "yyjson.h"

/* 2026-10-08 분봉이 열린 CME 품목. 긴 코드가 앞에 온다. 틱은 포인트 ×100. */
static const struct {
    const char *root;
    double tick_raw;
} k_cme[] = {
    {"MES", 25.0}, {"MNQ", 25.0}, {"M2K", 10.0}, {"NKD", 500.0}, {"EMD", 10.0},
    {"RTY", 10.0}, {"ES", 25.0},  {"NQ", 25.0},  {"6A", 1.0},    {"6B", 1.0},
    {"6C", 1.0},   {"6E", 1.0},   {"6J", 1.0},
};

static int cme_index(const char *sym) {
    if (sym == 0) {
        return -1;
    }
    for (size_t i = 0; i < sizeof(k_cme) / sizeof(k_cme[0]); i++) {
        size_t n = strlen(k_cme[i].root);
        if (strncmp(sym, k_cme[i].root, n) != 0) {
            continue;
        }
        char month = sym[n];
        if (strchr("FGHJKMNQUVXZ", month) == 0 || !isdigit((unsigned char)sym[n + 1]) ||
            !isdigit((unsigned char)sym[n + 2]) || sym[n + 3] != '\0') {
            continue;
        }
        return (int)i;
    }
    return -1;
}

/* CBOE 변동성 선물. VX + 월물문자 + 연도, 미니는 VXM + 월물문자 + 연도. */
static bool nh_cboe_future(const char *sym) {
    if (strncmp(sym, "VX", 2) != 0) {
        return false;
    }
    const char *p = sym + 2;
    if (*p == 'M' && strchr("FGHJKMNQUVXZ", p[1]) != 0) {
        p += 2;
    } else if (strchr("FGHJKMNQUVXZ", *p) != 0) {
        p += 1;
    } else {
        return false;
    }
    return isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) && p[2] == '\0';
}

const char *nh_exch_for_symbol(const char *sym) {
    if (sym == 0) {
        return 0;
    }
    if (strncmp(sym, "O_SPW", 5) == 0 || strncmp(sym, "O_SPX", 5) == 0 ||
        strncmp(sym, "O_NDX", 5) == 0) {
        return "OCBO";
    }
    if (nh_cboe_future(sym)) {
        return "FCBO";
    }
    if (cme_index(sym) >= 0) {
        return "FCME";
    }
    return 0;
}

double nh_tick_raw(const char *sym) {
    const char *exch = nh_exch_for_symbol(sym);
    if (exch == 0) {
        return 0.0;
    }
    if (strcmp(exch, "FCME") == 0) {
        int i = cme_index(sym);
        return i >= 0 ? k_cme[i].tick_raw : 0.0;
    }
    return 5.0;
}

static int64_t price_raw(yyjson_val *v) {
    if (yyjson_is_num(v)) {
        return (int64_t)llround(yyjson_get_num(v) * 100.0);
    }
    if (yyjson_is_str(v)) {
        return (int64_t)llround(atof(yyjson_get_str(v)) * 100.0);
    }
    return 0;
}

static int64_t qty_of(yyjson_val *v) {
    if (yyjson_is_num(v)) {
        return (int64_t)yyjson_get_sint(v);
    }
    if (yyjson_is_str(v)) {
        return atoll(yyjson_get_str(v));
    }
    return 0;
}

static bool kst_minute(const char *date, const char *hhmmss, tr_time_us_t *open_us) {
    if (date == 0 || hhmmss == 0 || strlen(date) != 8 || strlen(hhmmss) < 4) {
        return false;
    }
    tr_civil_t c;
    memset(&c, 0, sizeof(c));
    char y[5] = {date[0], date[1], date[2], date[3], 0};
    char mo[3] = {date[4], date[5], 0};
    char d[3] = {date[6], date[7], 0};
    char hh[3] = {hhmmss[0], hhmmss[1], 0};
    char mm[3] = {hhmmss[2], hhmmss[3], 0};
    c.year = atoi(y);
    c.month = (unsigned)atoi(mo);
    c.day = (unsigned)atoi(d);
    c.hour = (unsigned)atoi(hh);
    c.min = (unsigned)atoi(mm);
    c.sec = 0;
    return tr_time_us_from_civil(&c, 540, open_us);
}

int nh_chart_parse_minutes(const char *json, size_t len, uint64_t instrument_id,
                           tr_candle_t *out, size_t cap, size_t *out_count, char *nxt_key,
                           size_t nxt_cap) {
    if (out_count != 0) {
        *out_count = 0;
    }
    if (nxt_key != 0 && nxt_cap > 0) {
        nxt_key[0] = 0;
    }
    if (json == 0 || out == 0 || out_count == 0 || cap == 0) {
        return -1;
    }
    yyjson_doc *doc = yyjson_read(json, len, 0);
    if (doc == 0) {
        return -1;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (nxt_key != 0 && nxt_cap > 0) {
        const char *nk = yyjson_get_str(yyjson_obj_get(root, "nxt_key"));
        if (nk != 0) {
            snprintf(nxt_key, nxt_cap, "%s", nk);
        }
    }
    yyjson_val *arr = yyjson_obj_get(root, "Occurs1");
    if (!yyjson_is_arr(arr)) {
        yyjson_doc_free(doc);
        return 0;
    }
    size_t n = yyjson_arr_size(arr);
    if (n > cap) {
        n = cap;
    }
    /* 응답은 최신이 앞이다. 일단 그 순서로 담고 뒤집는다. */
    for (size_t i = 0; i < n; i++) {
        yyjson_val *row = yyjson_arr_get(arr, i);
        tr_candle_t *c = &out[i];
        memset(c, 0, sizeof(*c));
        tr_time_us_t open_us = 0;
        const char *dt = yyjson_get_str(yyjson_obj_get(row, "occurs1_ref_dt"));
        const char *tm = yyjson_get_str(yyjson_obj_get(row, "occurs1_exch_tm"));
        if (!kst_minute(dt, tm, &open_us)) {
            yyjson_doc_free(doc);
            return -1;
        }
        c->instrument_id = instrument_id;
        c->timeframe_sec = 60;
        c->open_time_us = open_us;
        c->close_time_us = open_us + 60 * 1000000LL;
        c->open = price_raw(yyjson_obj_get(row, "occurs1_open_pric"));
        c->high = price_raw(yyjson_obj_get(row, "occurs1_high_pric"));
        c->low = price_raw(yyjson_obj_get(row, "occurs1_low_pric"));
        c->close = price_raw(yyjson_obj_get(row, "occurs1_close_pric"));
        c->volume = qty_of(yyjson_obj_get(row, "occurs1_cum_trd_qty"));
        c->state = TR_CANDLE_CLOSED;
        c->source_id = 4;
    }
    yyjson_doc_free(doc);
    for (size_t i = 0; i < n / 2; i++) {
        tr_candle_t tmp = out[i];
        out[i] = out[n - 1 - i];
        out[n - 1 - i] = tmp;
    }
    /* 이제 오름차순. volume 필드는 아직 누적이다. 가장 오래된 봉은 기준이 없어 0. */
    int64_t prev = n > 0 ? out[0].volume : 0;
    if (n > 0) {
        out[0].volume = 0;
    }
    for (size_t i = 1; i < n; i++) {
        int64_t cum = out[i].volume;
        out[i].volume = cum >= prev ? cum - prev : 0;
        prev = cum;
    }
    *out_count = n;
    return 0;
}

int nh_chart_fetch_minutes(nh_auth_t *auth, const char *sym, const char *exch, int req_qty,
                           const char *nxt_in, uint64_t instrument_id, tr_candle_t *out,
                           size_t cap, size_t *out_count, char *nxt_out, size_t nxt_cap,
                           char *err, size_t err_cap) {
    if (out_count != 0) {
        *out_count = 0;
    }
    if (auth == 0 || sym == 0 || exch == 0 || out == 0 || out_count == 0) {
        return -1;
    }
    if (req_qty < 1) {
        req_qty = 1;
    }
    if (req_qty > 9999) {
        req_qty = 9999;
    }
    const char *token = 0;
    if (!nh_auth_ensure(auth, &token)) {
        if (err != 0 && err_cap > 0) {
            snprintf(err, err_cap, "%s", auth->last_error);
        }
        return -1;
    }
    char body[512];
    snprintf(body, sizeof(body),
             "{\"exch_cd\":\"%s\",\"sym\":\"%s\",\"inq_strt_dt\":\"00000000\",\"strt_tm\":\"000000\","
             "\"inq_ed_dt\":\"99999999\",\"ed_tm\":\"235959\",\"req_qty\":\"%d\",\"dcnt\":\"1\","
             "\"cidx_yn\":\"0\",\"nxt_key\":\"%s\"}",
             exch, sym, req_qty, nxt_in != 0 ? nxt_in : "");
    char url[200];
    snprintf(url, sizeof(url), "%s/trade/v1/overseas/chart-minute", auth->base);
    nh_http_resp_t resp;
    if (nh_http_post_json(url, token, body, 20000, &resp) != 0 || resp.http_status != 200 ||
        resp.body.data == 0) {
        if (err != 0 && err_cap > 0) {
            snprintf(err, err_cap, "NH chart HTTP %ld", resp.http_status);
        }
        nh_http_resp_free(&resp);
        return -1;
    }
    int rc = nh_chart_parse_minutes(resp.body.data, resp.body.len, instrument_id, out, cap,
                                    out_count, nxt_out, nxt_cap);
    nh_http_resp_free(&resp);
    if (rc != 0 && err != 0 && err_cap > 0) {
        snprintf(err, err_cap, "NH chart parse failed");
    }
    return rc;
}
