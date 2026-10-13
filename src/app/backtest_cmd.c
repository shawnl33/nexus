#include "app/backtest_cmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "adapters/nh/nh_auth.h"
#include "adapters/nh/nh_http.h"
#include "adapters/storage/live_bars.h"
#include "core/backtest/bar_fill.h"
#include "core/backtest/yl_fill.h"
#include "core/backtest/yl_link.h"
#include "core/model/civil_time.h"
#include "core/signals/weekly_pair_v1.h"
#include "yyjson.h"

#define BT_CAP 100000
#define KST_MIN 540

static uint64_t id_of(const char *code) {
    uint64_t h = 1469598103934665603ULL;
    if (code == 0) {
        return 1;
    }
    while (*code) {
        h ^= (unsigned char)*code++;
        h *= 1099511628211ULL;
    }
    return h != 0 ? h : 1;
}

static int kst_span(const char *start, const char *end, int64_t *from, int64_t *to) {
    int y1, m1, d1, y2, m2, d2;
    tr_civil_t a;
    tr_civil_t b;
    if (sscanf(start, "%d-%d-%d", &y1, &m1, &d1) != 3 || sscanf(end, "%d-%d-%d", &y2, &m2, &d2) != 3) {
        return 0;
    }
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.year = y1;
    a.month = (unsigned)m1;
    a.day = (unsigned)d1;
    b.year = y2;
    b.month = (unsigned)m2;
    b.day = (unsigned)d2;
    if (!tr_time_us_from_civil(&a, KST_MIN, (tr_time_us_t *)from)) {
        return 0;
    }
    if (!tr_time_us_from_civil(&b, KST_MIN, (tr_time_us_t *)to)) {
        return 0;
    }
    *to += INT64_C(86400) * INT64_C(1000000);
    return *from < *to;
}

static int net_fetch(void *ctx, int method, const char *url, const char *body, char *resp, size_t cap,
                     char *err, size_t err_cap) {
    nh_http_resp_t http;
    int rc;
    (void)ctx;
    if (method == TR_BAR_GET) {
        rc = nh_http_get(url, 60000, &http);
    } else {
        nh_auth_t auth;
        const char *token = 0;
        if (!nh_auth_init(&auth, "https://api.futures.co.kr", "NH_OVERSEAS_APP_KEY", "NH_OVERSEAS_SECRET_KEY") ||
            !nh_auth_ensure(&auth, &token)) {
            snprintf(err, err_cap, "NH 인증에 실패했습니다");
            return -1;
        }
        rc = nh_http_post_json(url, token, body, 60000, &http);
    }
    if (rc != 0 || http.body.data == 0 || http.http_status >= 400) {
        snprintf(err, err_cap, "연결할 수 없습니다");
        nh_http_resp_free(&http);
        return -1;
    }
    if (http.body.len + 1 > cap) {
        snprintf(err, err_cap, "응답이 너무 큽니다");
        nh_http_resp_free(&http);
        return -1;
    }
    memcpy(resp, http.body.data, http.body.len + 1);
    nh_http_resp_free(&http);
    return 0;
}

static int ensure_bars(const char *code, tr_product_type_t product, int64_t from, int64_t to, char *err,
                       size_t err_cap) {
    tr_bar_fill_req_t req;
    memset(&req, 0, sizeof(req));
    req.product = product;
    req.instrument_id = id_of(code);
    req.price_scale = 100;
    req.qty_scale = 1;
    req.code = code;
    return tr_live_bars_fill(&req, from, to, net_fetch, 0, err, err_cap);
}

static void candle_px(tr_wpair_pxbar_t *b, const tr_candle_t *c, int32_t day_index) {
    tr_civil_t cv;
    memset(b, 0, sizeof(*b));
    b->open_us = c->open_time_us;
    if (tr_civil_from_time_us(c->open_time_us, KST_MIN, &cv)) {
        b->ymd = (int64_t)cv.year * 10000 + (int64_t)cv.month * 100 + cv.day;
        b->hhmmss = (int64_t)cv.hour * 10000 + (int64_t)cv.min * 100 + cv.sec;
    }
    b->day_index = day_index;
    b->open = (double)c->open / 100.0;
    b->high = (double)c->high / 100.0;
    b->low = (double)c->low / 100.0;
    b->close = (double)c->close / 100.0;
    b->volume = (double)c->volume;
}

static size_t load_px(const char *code, int64_t from, int64_t to, tr_wpair_pxbar_t *dst, size_t cap,
                      char *err, size_t err_cap) {
    tr_product_type_t product = strchr(code, ':') != 0 ? TR_PRODUCT_OPTIONS : TR_PRODUCT_FUTURES;
    tr_candle_t *bars;
    size_t n;
    size_t i;
    size_t n_out = 0;
    int64_t prev_ymd = -1;
    int32_t day_i = 0;
    if (ensure_bars(code, product, from, to, err, err_cap) != 0) {
        return 0;
    }
    bars = (tr_candle_t *)malloc(cap * sizeof(*bars));
    if (bars == 0) {
        snprintf(err, err_cap, "봉을 담을 메모리가 없습니다");
        return 0;
    }
    n = tr_live_bars_query(id_of(code), 60, from, to, bars, cap);
    if (n >= cap) {
        free(bars);
        snprintf(err, err_cap, "종목당 봉이 10만 개를 넘습니다. 구간을 줄이세요");
        return 0;
    }
    for (i = 0; i < n; i++) {
        tr_wpair_pxbar_t *b;
        if (i > 0 && bars[i].open_time_us == bars[i - 1].open_time_us && n_out > 0) {
            b = &dst[n_out - 1];
        } else {
            b = &dst[n_out++];
            if (i == 0 || bars[i].open_time_us != bars[i - 1].open_time_us) {
                tr_civil_t cv;
                int64_t ymd = 0;
                if (tr_civil_from_time_us(bars[i].open_time_us, KST_MIN, &cv)) {
                    ymd = (int64_t)cv.year * 10000 + (int64_t)cv.month * 100 + cv.day;
                }
                if (ymd != prev_ymd) {
                    day_i = 0;
                    prev_ymd = ymd;
                } else {
                    day_i++;
                }
            }
        }
        candle_px(b, &bars[i], day_i);
    }
    free(bars);
    if (n_out == 0) {
        snprintf(err, err_cap, "저장소에 그 구간의 봉이 없습니다");
    }
    return n_out;
}

static int append(char *out, size_t cap, int *used, const char *s) {
    int n = snprintf(out + *used, cap - (size_t)*used, "%s", s);
    if (n < 0 || (size_t)(*used + n) >= cap) {
        return 0;
    }
    *used += n;
    return 1;
}

static int write_bars(char *out, size_t cap, int *used, const tr_wpair_pxbar_t *bars, size_t n) {
    size_t i;
    if (!append(out, cap, used, "[")) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char row[160];
        snprintf(row, sizeof(row), "%s{\"t\":%lld,\"o\":%.10g,\"h\":%.10g,\"l\":%.10g,\"c\":%.10g}",
                 i ? "," : "", (long long)(bars[i].open_us / 1000000LL), bars[i].open, bars[i].high,
                 bars[i].low, bars[i].close);
        if (!append(out, cap, used, row)) {
            return 0;
        }
    }
    return append(out, cap, used, "]");
}

static int write_trades(char *out, size_t cap, int *used, const tr_yl_book_t *book, int64_t from_sec) {
    int i;
    int first = 1;
    if (!append(out, cap, used, "[")) {
        return 0;
    }
    for (i = 0; i < tr_yl_book_trade_count(book); i++) {
        const tr_yl_trade_t *t = tr_yl_book_trade(book, i);
        char row[256];
        if (t->time < from_sec) {
            continue;
        }
        if (t->filled && t->has_pnl) {
            snprintf(row, sizeof(row),
                     "%s{\"t\":%lld,\"name\":\"%s\",\"status\":\"체결\",\"price\":%.10g,\"qty\":%d,\"pnl\":%.10g}",
                     first ? "" : ",", (long long)t->time, t->name, t->price, t->qty, t->pnl);
        } else if (t->filled) {
            snprintf(row, sizeof(row),
                     "%s{\"t\":%lld,\"name\":\"%s\",\"status\":\"체결\",\"price\":%.10g,\"qty\":%d,\"pnl\":null}",
                     first ? "" : ",", (long long)t->time, t->name, t->price, t->qty);
        } else {
            snprintf(row, sizeof(row),
                     "%s{\"t\":%lld,\"name\":\"%s\",\"status\":\"미체결\",\"price\":null,\"qty\":%d,\"pnl\":null}",
                     first ? "" : ",", (long long)t->time, t->name, t->qty);
        }
        if (!append(out, cap, used, row)) {
            return 0;
        }
        first = 0;
    }
    return append(out, cap, used, "]");
}

#define BT_IPC_MAX (60u * 1024u)
#define BT_RESULT_FILE "web/.runtime/backtest-result.json"

static int fail(char *out, size_t cap, const char *msg);

int tr_backtest_fit_ipc(char *out, size_t cap) {
    size_t n;
    FILE *f;
    if (out == 0 || cap < 32) {
        return -1;
    }
    n = strlen(out);
    if (n + 1 < BT_IPC_MAX) {
        return 0;
    }
#ifdef _WIN32
    _mkdir("web");
    _mkdir("web/.runtime");
#else
    mkdir("web", 0755);
    mkdir("web/.runtime", 0700);
#endif
    f = fopen(BT_RESULT_FILE ".tmp", "wb");
    if (f == 0 || fwrite(out, 1, n, f) != n) {
        if (f != 0) {
            fclose(f);
        }
        return fail(out, cap, "결과를 저장하지 못했습니다");
    }
    if (fclose(f) != 0) {
        return fail(out, cap, "결과를 저장하지 못했습니다");
    }
    if (rename(BT_RESULT_FILE ".tmp", BT_RESULT_FILE) != 0) {
        return fail(out, cap, "결과를 저장하지 못했습니다");
    }
    snprintf(out, cap, "{\"result_file\":true}");
    return 0;
}

static int fail(char *out, size_t cap, const char *msg) {
    snprintf(out, cap, "{\"error\":\"%s\"}", msg);
    return -1;
}

int tr_backtest_run(const char *payload, char *out, size_t cap) {
    yyjson_doc *doc;
    yyjson_val *data;
    const char *signal;
    const char *start;
    const char *end;
    const char *symbol = "";
    const char *base = "";
    const char *opp = "";
    const char *fut = "";
    int qty = 1;
    int64_t from = 0;
    int64_t to = 0;
    char err[240];
    tr_wpair_pxbar_t *self = 0;
    size_t nself = 0;
    tr_yl_book_t book;
    int used = 0;
    err[0] = 0;
    if (payload == 0 || out == 0 || cap < 32) {
        return -1;
    }
    doc = yyjson_read(payload, strlen(payload), 0);
    data = doc != 0 ? yyjson_obj_get(yyjson_doc_get_root(doc), "data") : 0;
    if (data == 0) {
        data = doc != 0 ? yyjson_doc_get_root(doc) : 0;
    }
    signal = data != 0 ? yyjson_get_str(yyjson_obj_get(data, "signal")) : 0;
    start = data != 0 ? yyjson_get_str(yyjson_obj_get(data, "start")) : 0;
    end = data != 0 ? yyjson_get_str(yyjson_obj_get(data, "end")) : 0;
    if (data != 0 && yyjson_is_num(yyjson_obj_get(data, "qty"))) {
        qty = (int)yyjson_get_num(yyjson_obj_get(data, "qty"));
    }
    if (data != 0 && yyjson_is_str(yyjson_obj_get(data, "symbol"))) {
        symbol = yyjson_get_str(yyjson_obj_get(data, "symbol"));
    }
    if (data != 0 && yyjson_is_str(yyjson_obj_get(data, "base"))) {
        base = yyjson_get_str(yyjson_obj_get(data, "base"));
    }
    if (data != 0 && yyjson_is_str(yyjson_obj_get(data, "opp"))) {
        opp = yyjson_get_str(yyjson_obj_get(data, "opp"));
    }
    if (data != 0 && yyjson_is_str(yyjson_obj_get(data, "fut"))) {
        fut = yyjson_get_str(yyjson_obj_get(data, "fut"));
    }
    if (signal == 0 || start == 0 || end == 0 || !kst_span(start, end, &from, &to)) {
        if (doc != 0) {
            yyjson_doc_free(doc);
        }
        return fail(out, cap, "시작일과 종료일, 신호를 확인하세요");
    }
    if (qty < 1) {
        yyjson_doc_free(doc);
        return fail(out, cap, "수량은 1 이상이어야 합니다");
    }
    self = (tr_wpair_pxbar_t *)malloc(BT_CAP * sizeof(*self));
    if (self == 0) {
        yyjson_doc_free(doc);
        return fail(out, cap, "봉을 담을 메모리가 없습니다");
    }
    tr_yl_book_init(&book, qty, 0.25);
    if (strcmp(signal, "pair-long") == 0 || strcmp(signal, "pair-short") == 0) {
        tr_wpair_t st;
        tr_wpair_config_t cfg;
        tr_wpair_pxbar_t *obars;
        tr_wpair_pxbar_t *fbars;
        size_t no, nf;
        int side = strcmp(signal, "pair-long") == 0 ? TR_WPAIR_LONG : TR_WPAIR_SHORT;
        if (base[0] == 0 || opp[0] == 0 || fut[0] == 0) {
            free(self);
            yyjson_doc_free(doc);
            return fail(out, cap, "페어는 기준, 상대, 선물 종목이 필요합니다");
        }
        obars = (tr_wpair_pxbar_t *)malloc(BT_CAP * sizeof(*obars));
        fbars = (tr_wpair_pxbar_t *)malloc(BT_CAP * sizeof(*fbars));
        nself = load_px(base, from, to, self, BT_CAP, err, sizeof(err));
        no = obars != 0 ? load_px(opp, from, to, obars, BT_CAP, err, sizeof(err)) : 0;
        nf = fbars != 0 ? load_px(fut, from, to, fbars, BT_CAP, err, sizeof(err)) : 0;
        tr_wpair_default_config(&cfg);
        if (nself == 0 || no == 0 || nf == 0 || obars == 0 || fbars == 0 ||
            tr_wpair_init(&st, side, &cfg) != 0 ||
            tr_wpair_fill(&st, self, nself, obars, no, fbars, nf, 0, &book) < 0) {
            free(obars);
            free(fbars);
            free(self);
            yyjson_doc_free(doc);
            return fail(out, cap, err[0] ? err : "페어 봉을 채우지 못했습니다");
        }
        free(obars);
        free(fbars);
        symbol = base;
    } else if (strcmp(signal, "fx_sig_v6") == 0) {
        tr_fxos_t *scope;
        size_t i;
        if (symbol[0] == 0) {
            free(self);
            yyjson_doc_free(doc);
            return fail(out, cap, "선택한 화면틀의 종목이 없습니다");
        }
        nself = load_px(symbol, from, to, self, BT_CAP, err, sizeof(err));
        scope = (tr_fxos_t *)calloc(1, sizeof(*scope));
        if (nself == 0 || scope == 0 || !tr_fxos_init(scope, 100.0)) {
            free(scope);
            free(self);
            yyjson_doc_free(doc);
            return fail(out, cap, err[0] ? err : "진입 신호 봉을 채우지 못했습니다");
        }
        scope->cfg.order_qty = qty;
        for (i = 0; i < nself; i++) {
            tr_fxos_input_t in;
            memset(&in, 0, sizeof(in));
            in.date = self[i].ymd;
            in.time = self[i].hhmmss;
            in.bar_open = self[i].open_us;
            in.high = self[i].high;
            in.low = self[i].low;
            in.close = self[i].close;
            in.volume = self[i].volume;
            in.is_new_bar = true;
            tr_yl_fxsig_bar(&book, scope, &in);
        }
        free(scope);
    } else {
        free(self);
        yyjson_doc_free(doc);
        return fail(out, cap, "알 수 없는 신호입니다");
    }
    yyjson_doc_free(doc);
    if (!append(out, cap, &used, "{\"symbol\":\"") || !append(out, cap, &used, symbol) ||
        !append(out, cap, &used, "\",\"bars\":") || !write_bars(out, cap, &used, self, nself) ||
        !append(out, cap, &used, ",\"trades\":") ||
        !write_trades(out, cap, &used, &book, from / 1000000LL) || !append(out, cap, &used, "}")) {
        free(self);
        return fail(out, cap, "결과가 너무 큽니다");
    }
    free(self);
    return 0;
}
