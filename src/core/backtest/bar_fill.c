#include "core/backtest/bar_fill.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/model/civil_time.h"
#include "yyjson.h"

#define KST_MIN 540
#define END_NXT "99999999999999"

static int nth_sunday(int year, int month, int n) {
    int64_t days = tr_days_from_civil(year, (unsigned)month, 1);
    unsigned wd = tr_weekday_from_days(days);
    int delta = (7 - (int)wd) % 7;
    return 1 + delta + (n - 1) * 7;
}

static int eastern_offset_min(int year, int month, int day) {
    int begin = nth_sunday(year, 3, 2);
    int end = nth_sunday(year, 11, 1);
    int edt = 0;
    if (month > 3 && month < 11) {
        edt = 1;
    } else if (month == 3 && day >= begin) {
        edt = 1;
    } else if (month == 11 && day < end) {
        edt = 1;
    }
    return edt ? -240 : -300;
}

static void utc_to_eastern(int64_t us, tr_civil_t *out) {
    tr_civil_t est;
    if (!tr_civil_from_time_us(us, -300, &est)) {
        memset(out, 0, sizeof(*out));
        return;
    }
    if (eastern_offset_min(est.year, (int)est.month, (int)est.day) == -240) {
        tr_civil_from_time_us(us, -240, out);
    } else {
        *out = est;
    }
}

static int64_t eastern_us(int year, int month, int day, int hour, int min) {
    tr_civil_t c;
    tr_time_us_t t = 0;
    memset(&c, 0, sizeof(c));
    c.year = year;
    c.month = (unsigned)month;
    c.day = (unsigned)day;
    c.hour = (unsigned)hour;
    c.min = (unsigned)min;
    if (!tr_time_us_from_civil(&c, eastern_offset_min(year, month, day), &t)) {
        return 0;
    }
    return t;
}

static int ymd_key(const tr_civil_t *c) {
    return c->year * 10000 + (int)c->month * 100 + (int)c->day;
}

static int parse_option(const char *code, char *root, size_t root_cap, char *exp, double *strike,
                        char *right) {
    char exp_raw[16];
    char side[8];
    unsigned i;
    unsigned j = 0;
    if (code == 0 || sscanf(code, "%31[^:]:%15[^:]:%lf:%7s", root, exp_raw, strike, side) != 4) {
        return 0;
    }
    if (strlen(root) == 0 || strlen(root) >= root_cap) {
        return 0;
    }
    for (i = 0; exp_raw[i] != '\0' && j + 1 < 9; i++) {
        if (exp_raw[i] >= '0' && exp_raw[i] <= '9') {
            exp[j++] = exp_raw[i];
        }
    }
    exp[j] = '\0';
    if (j != 8) {
        return 0;
    }
    if (strcmp(side, "call") == 0) {
        strcpy(right, "call");
    } else if (strcmp(side, "put") == 0) {
        strcpy(right, "put");
    } else {
        return 0;
    }
    return 1;
}

static int64_t scaled(double v, int64_t scale) {
    if (scale <= 0) {
        scale = 1;
    }
    return (int64_t)(v * (double)scale + (v >= 0 ? 0.5 : -0.5));
}

static int put_bar(tr_store_t *store, const tr_bar_fill_req_t *req, int64_t open_us, double o,
                   double h, double l, double c, double vol, uint64_t source) {
    tr_candle_t bar;
    if (open_us <= 0) {
        return 0;
    }
    memset(&bar, 0, sizeof(bar));
    bar.instrument_id = req->instrument_id;
    bar.timeframe_sec = 60;
    bar.open_time_us = open_us;
    bar.close_time_us = open_us + 60 * 1000000LL;
    bar.open = scaled(o, req->price_scale);
    bar.high = scaled(h, req->price_scale);
    bar.low = scaled(l, req->price_scale);
    bar.close = scaled(c, req->price_scale);
    bar.volume = scaled(vol, req->qty_scale > 0 ? req->qty_scale : 1);
    bar.state = TR_CANDLE_CLOSED;
    bar.source_id = source;
    return tr_store_put_candle(store, &bar) == TR_STORE_OK;
}

static int day_has_bars(tr_store_t *store, uint64_t id, int year, int month, int day) {
    int64_t from = eastern_us(year, month, day, 9, 30);
    int64_t to = eastern_us(year, month, day, 16, 16);
    return tr_store_query_candles(store, id, 60, from, to, 1, 0, 0) > 0;
}

static int parse_theta_time(const char *ts, int64_t *out) {
    int y, mo, d, h, mi, se;
    tr_civil_t c;
    tr_time_us_t t = 0;
    if (ts == 0 || sscanf(ts, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) {
        return 0;
    }
    memset(&c, 0, sizeof(c));
    c.year = y;
    c.month = (unsigned)mo;
    c.day = (unsigned)d;
    c.hour = (unsigned)h;
    c.min = (unsigned)mi;
    c.sec = (unsigned)se;
    if (!tr_time_us_from_civil(&c, eastern_offset_min(y, mo, d), &t)) {
        return 0;
    }
    *out = t;
    return 1;
}

static int store_theta(tr_store_t *store, const tr_bar_fill_req_t *req, const char *json,
                       int64_t start_us, int64_t end_us) {
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *arr;
    size_t i, n;
    int saved = 0;
    if (doc == 0) {
        return -1;
    }
    arr = yyjson_obj_get(yyjson_doc_get_root(doc), "response");
    if (!yyjson_is_arr(arr)) {
        yyjson_doc_free(doc);
        return -1;
    }
    n = yyjson_arr_size(arr);
    for (i = 0; i < n; i++) {
        yyjson_val *data = yyjson_obj_get(yyjson_arr_get(arr, i), "data");
        size_t j, m;
        if (!yyjson_is_arr(data)) {
            continue;
        }
        m = yyjson_arr_size(data);
        for (j = 0; j < m; j++) {
            yyjson_val *row = yyjson_arr_get(data, j);
            int64_t open_us = 0;
            if (!parse_theta_time(yyjson_get_str(yyjson_obj_get(row, "timestamp")), &open_us)) {
                continue;
            }
            if (open_us < start_us || open_us >= end_us) {
                continue;
            }
            if (put_bar(store, req, open_us, yyjson_get_num(yyjson_obj_get(row, "open")),
                        yyjson_get_num(yyjson_obj_get(row, "high")),
                        yyjson_get_num(yyjson_obj_get(row, "low")),
                        yyjson_get_num(yyjson_obj_get(row, "close")),
                        yyjson_get_num(yyjson_obj_get(row, "volume")), 41)) {
                saved++;
            }
        }
    }
    yyjson_doc_free(doc);
    return saved;
}

static int fill_option(tr_store_t *store, const tr_bar_fill_req_t *req, int64_t start_us,
                       int64_t end_us, tr_bar_fetch_fn fetch, void *ctx, char *err, size_t err_cap) {
    char root[32];
    char exp[16];
    char right[8];
    double strike = 0;
    tr_civil_t cur;
    int ymd;
    int end_ymd;
    char chunk_from[16] = "";
    char chunk_to[16] = "";
    int chunk_n = 0;
    if (!parse_option(req->code, root, sizeof(root), exp, &strike, right)) {
        snprintf(err, err_cap, "옵션 종목코드에서 기초자산, 만기, 행사가, 콜·풋을 뽑지 못했습니다");
        return -1;
    }
    utc_to_eastern(start_us, &cur);
    ymd = ymd_key(&cur);
    utc_to_eastern(end_us - 1, &cur);
    end_ymd = ymd_key(&cur);
    utc_to_eastern(start_us, &cur);
    while (ymd <= end_ymd) {
        int missing = !day_has_bars(store, req->instrument_id, cur.year, (int)cur.month, (int)cur.day);
        if (missing) {
            char key[16];
            snprintf(key, sizeof(key), "%04d%02u%02u", cur.year, cur.month, cur.day);
            if (chunk_n == 0) {
                snprintf(chunk_from, sizeof(chunk_from), "%s", key);
            }
            snprintf(chunk_to, sizeof(chunk_to), "%s", key);
            chunk_n++;
        }
        if ((!missing || ymd == end_ymd) && chunk_n > 0) {
            char url[640];
            char *resp = (char *)malloc(2 * 1024 * 1024);
            snprintf(url, sizeof(url),
                     "http://127.0.0.1:25503/v3/option/history/ohlc?symbol=%s&expiration=%s"
                     "&strike=%g&right=%s&interval=1m&start_date=%s&end_date=%s"
                     "&start_time=09:30:00&end_time=16:15:00&format=json",
                     root, exp, strike, right, chunk_from, chunk_to);
            if (resp == 0) {
                snprintf(err, err_cap, "옵션 분봉을 담을 메모리가 없습니다");
                return -1;
            }
            if (fetch(ctx, TR_BAR_GET, url, "", resp, 2 * 1024 * 1024, err, err_cap) != 0) {
                free(resp);
                return -1;
            }
            if (store_theta(store, req, resp, start_us, end_us) < 0) {
                free(resp);
                snprintf(err, err_cap, "옵션 분봉 응답을 읽지 못했습니다");
                return -1;
            }
            free(resp);
            chunk_n = 0;
        }
        {
            tr_civil_t next;
            tr_time_us_t noon = 0;
            memset(&next, 0, sizeof(next));
            next.year = cur.year;
            next.month = cur.month;
            next.day = cur.day;
            next.hour = 12;
            if (!tr_time_us_from_civil(&next, eastern_offset_min(cur.year, (int)cur.month, (int)cur.day),
                                      &noon)) {
                break;
            }
            utc_to_eastern(noon + 24 * 60 * 60 * 1000000LL, &cur);
            ymd = ymd_key(&cur);
        }
    }
    if (tr_store_query_candles(store, req->instrument_id, 60, start_us, end_us, 1, 0, 0) <= 0) {
        snprintf(err, err_cap, "저장소에 그 구간의 봉이 없습니다");
        return -1;
    }
    return 0;
}

static int kst_us(const char *date, const char *hhmmss, int64_t *out) {
    tr_civil_t c;
    tr_time_us_t t = 0;
    if (date == 0 || hhmmss == 0 || strlen(date) != 8 || strlen(hhmmss) < 4) {
        return 0;
    }
    memset(&c, 0, sizeof(c));
    c.year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 + (date[2] - '0') * 10 + (date[3] - '0');
    c.month = (unsigned)((date[4] - '0') * 10 + (date[5] - '0'));
    c.day = (unsigned)((date[6] - '0') * 10 + (date[7] - '0'));
    c.hour = (unsigned)((hhmmss[0] - '0') * 10 + (hhmmss[1] - '0'));
    c.min = (unsigned)((hhmmss[2] - '0') * 10 + (hhmmss[3] - '0'));
    if (!tr_time_us_from_civil(&c, KST_MIN, &t)) {
        return 0;
    }
    *out = t;
    return 1;
}

static int all_nines(const char *s) {
    size_t i;
    if (s == 0 || s[0] == '\0') {
        return 1;
    }
    for (i = 0; s[i] != '\0'; i++) {
        if (s[i] != '9') {
            return 0;
        }
    }
    return 1;
}

static double num_of(yyjson_val *v) {
    if (yyjson_is_num(v)) {
        return yyjson_get_num(v);
    }
    if (yyjson_is_str(v)) {
        return atof(yyjson_get_str(v));
    }
    return 0;
}

typedef struct {
    int64_t prev;
    int64_t first;
    int64_t last;
    int n;
    int hole;
} span_t;

static void span_cb(void *ctx, const tr_candle_t *c) {
    span_t *s = ctx;
    if (s->n > 0 && c->open_time_us - s->prev > INT64_C(96) * 3600 * 1000000) {
        s->hole = 1;
    }
    if (s->n == 0) {
        s->first = c->open_time_us;
    }
    s->prev = c->open_time_us;
    s->last = c->open_time_us;
    s->n++;
}

/* 96시간을 넘는 공백이 있거나 구간 앞뒤가 비어 있으면 다시 받는다. */
static int futures_covered(tr_store_t *store, uint64_t id, int64_t start_us, int64_t end_us) {
    span_t span;
    const int64_t slack = INT64_C(96) * 3600 * 1000000;
    memset(&span, 0, sizeof(span));
    if (tr_store_query_candles(store, id, 60, start_us, end_us, 100000, span_cb, &span) <= 0) {
        return 0;
    }
    if (span.hole) {
        return 0;
    }
    if (span.first > start_us + slack) {
        return 0;
    }
    if (span.last + slack < end_us) {
        return 0;
    }
    return 1;
}

static int fill_futures(tr_store_t *store, const tr_bar_fill_req_t *req, int64_t start_us,
                        int64_t end_us, tr_bar_fetch_fn fetch, void *ctx, char *err, size_t err_cap) {
    char nxt[80] = "";
    int page;
    int64_t oldest = 0;
    int saw = 0;
    if (futures_covered(store, req->instrument_id, start_us, end_us)) {
        return 0;
    }
    for (page = 0; page < 400; page++) {
        char body[512];
        char *resp = (char *)malloc(1024 * 1024);
        yyjson_doc *doc;
        yyjson_val *arr;
        size_t i, n;
        int64_t page_oldest = 0;
        int tx = 0;
        if (resp == 0) {
            snprintf(err, err_cap, "선물 분봉을 담을 메모리가 없습니다");
            return -1;
        }
        snprintf(body, sizeof(body),
                 "{\"exch_cd\":\"FCME\",\"sym\":\"%s\",\"inq_strt_dt\":\"00000000\","
                 "\"strt_tm\":\"000000\",\"inq_ed_dt\":\"99999999\",\"ed_tm\":\"235959\","
                 "\"req_qty\":\"9999\",\"dcnt\":\"1\",\"cidx_yn\":\"0\",\"nxt_key\":\"%s\"}",
                 req->code, nxt);
        if (fetch(ctx, TR_BAR_POST, "https://api.futures.co.kr/trade/v1/overseas/chart-minute", body,
                  resp, 1024 * 1024, err, err_cap) != 0) {
            free(resp);
            return -1;
        }
        doc = yyjson_read(resp, strlen(resp), 0);
        free(resp);
        if (doc == 0) {
            snprintf(err, err_cap, "NH 분봉 응답을 읽지 못했습니다");
            return -1;
        }
        {
            const char *nk = yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(doc), "nxt_key"));
            snprintf(nxt, sizeof(nxt), "%s", nk != 0 ? nk : "");
        }
        arr = yyjson_obj_get(yyjson_doc_get_root(doc), "Occurs1");
        n = yyjson_is_arr(arr) ? yyjson_arr_size(arr) : 0;
        if (n > 0 && tr_store_tx_begin(store) == TR_STORE_OK) {
            tx = 1;
        }
        for (i = 0; i < n; i++) {
            yyjson_val *row = yyjson_arr_get(arr, i);
            int64_t open_us = 0;
            const char *dt = yyjson_get_str(yyjson_obj_get(row, "occurs1_ref_dt"));
            const char *tm = yyjson_get_str(yyjson_obj_get(row, "occurs1_exch_tm"));
            if (!kst_us(dt, tm, &open_us)) {
                continue;
            }
            page_oldest = open_us;
            if (!saw || open_us < oldest) {
                oldest = open_us;
                saw = 1;
            }
            if (open_us < end_us) {
                put_bar(store, req, open_us, num_of(yyjson_obj_get(row, "occurs1_open_pric")),
                        num_of(yyjson_obj_get(row, "occurs1_high_pric")),
                        num_of(yyjson_obj_get(row, "occurs1_low_pric")),
                        num_of(yyjson_obj_get(row, "occurs1_close_pric")),
                        num_of(yyjson_obj_get(row, "occurs1_trd_qty")), 42);
            }
        }
        if (tx && tr_store_tx_commit(store) != TR_STORE_OK) {
            tr_store_tx_rollback(store);
            yyjson_doc_free(doc);
            snprintf(err, err_cap, "선물 분봉을 저장하지 못했습니다");
            return -1;
        }
        yyjson_doc_free(doc);
        if (n == 0 || all_nines(nxt) || (page_oldest > 0 && page_oldest < start_us)) {
            break;
        }
    }
    if (tr_store_query_candles(store, req->instrument_id, 60, start_us, end_us, 1, 0, 0) <= 0) {
        tr_civil_t c;
        if (saw && tr_civil_from_time_us(oldest, KST_MIN, &c)) {
            snprintf(err, err_cap, "가장 오래된 봉은 %04d-%02u-%02u %02u:%02u", c.year, c.month, c.day,
                     c.hour, c.min);
        } else {
            snprintf(err, err_cap, "저장소에 그 구간의 봉이 없습니다");
        }
        return -1;
    }
    return 0;
}

int tr_bar_fill(tr_store_t *store, const tr_bar_fill_req_t *req, int64_t start_us, int64_t end_us,
                tr_bar_fetch_fn fetch, void *ctx, char *err, size_t err_cap) {
    if (err != 0 && err_cap > 0) {
        err[0] = '\0';
    }
    if (store == 0 || req == 0 || fetch == 0 || start_us >= end_us) {
        snprintf(err, err_cap, "봉 채우기 요청이 잘못되었습니다");
        return -1;
    }
    if (req->product == TR_PRODUCT_INDEX) {
        snprintf(err, err_cap, "지수 1분봉은 받지 않습니다");
        return -1;
    }
    if (req->product == TR_PRODUCT_OPTIONS) {
        return fill_option(store, req, start_us, end_us, fetch, ctx, err, err_cap);
    }
    if (req->product == TR_PRODUCT_FUTURES) {
        return fill_futures(store, req, start_us, end_us, fetch, ctx, err, err_cap);
    }
    snprintf(err, err_cap, "봉을 받을 수 없는 종목입니다");
    return -1;
}
