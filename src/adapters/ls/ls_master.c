#include "adapters/ls/ls_master.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adapters/ls/ls_http.h"
#include "yyjson.h"

#define LS_STOCK_MASTER_URL "https://openapi.ls-sec.co.kr:8080/stock/etc"
#define LS_FUT_MASTER_URL "https://openapi.ls-sec.co.kr:8080/futureoption/market-data"
#define LS_MASTER_STOCK_CAP 4608
#define LS_MASTER_FUT_CAP 64

int ls_master_parse_stock(const char *body, size_t len, ls_instrument_info_t *out, size_t cap) {
    yyjson_doc *doc = yyjson_read((char *)body, len, 0);
    if (doc == 0) {
        return -1;
    }
    yyjson_val *arr = yyjson_obj_get(yyjson_doc_get_root(doc), "t8436OutBlock");
    if (!yyjson_is_arr(arr)) {
        yyjson_doc_free(doc);
        return 0;
    }
    int n = 0;
    size_t idx, max;
    yyjson_val *row;
    yyjson_arr_foreach(arr, idx, max, row) {
        if ((size_t)n >= cap) {
            break;
        }
        const char *shcode = yyjson_get_str(yyjson_obj_get(row, "shcode"));
        const char *hname = yyjson_get_str(yyjson_obj_get(row, "hname"));
        const char *expcode = yyjson_get_str(yyjson_obj_get(row, "expcode"));
        const char *gubun = yyjson_get_str(yyjson_obj_get(row, "gubun"));
        if (shcode == 0 || shcode[0] == 0) {
            continue;
        }
        ls_instrument_info_t *it = &out[n];
        memset(it, 0, sizeof(*it));
        snprintf(it->shcode, sizeof(it->shcode), "%.11s", shcode);
        snprintf(it->name, sizeof(it->name), "%.63s", hname != 0 ? hname : "");
        snprintf(it->expcode, sizeof(it->expcode), "%.19s", expcode != 0 ? expcode : "");
        it->market = gubun != 0 && gubun[0] == '2' ? LS_MARKET_KOSDAQ : LS_MARKET_KOSPI;
        it->is_futures = false;
        n++;
    }
    yyjson_doc_free(doc);
    return n;
}

int ls_master_parse_fut(const char *body, size_t len, ls_instrument_info_t *out, size_t cap) {
    yyjson_doc *doc = yyjson_read((char *)body, len, 0);
    if (doc == 0) {
        return -1;
    }
    yyjson_val *arr = yyjson_obj_get(yyjson_doc_get_root(doc), "t8467OutBlock");
    if (!yyjson_is_arr(arr)) {
        yyjson_doc_free(doc);
        return 0;
    }
    int n = 0;
    size_t idx, max;
    yyjson_val *row;
    yyjson_arr_foreach(arr, idx, max, row) {
        if ((size_t)n >= cap) {
            break;
        }
        const char *shcode = yyjson_get_str(yyjson_obj_get(row, "shcode"));
        const char *hname = yyjson_get_str(yyjson_obj_get(row, "hname"));
        const char *expcode = yyjson_get_str(yyjson_obj_get(row, "expcode"));
        if (shcode == 0 || shcode[0] == 0) {
            continue;
        }
        ls_instrument_info_t *it = &out[n];
        memset(it, 0, sizeof(*it));
        snprintf(it->shcode, sizeof(it->shcode), "%.11s", shcode);
        snprintf(it->name, sizeof(it->name), "%.63s", hname != 0 ? hname : "");
        snprintf(it->expcode, sizeof(it->expcode), "%.19s", expcode != 0 ? expcode : "");
        it->market = LS_MARKET_KP200_FUT;
        it->is_futures = true;
        n++;
    }
    yyjson_doc_free(doc);
    return n;
}

static int fetch_block(ls_auth_t *auth, const char *url, const char *tr_cd,
                       const char *inblock_body, ls_buf_t *out, char *errbuf, size_t errlen) {
    const char *token;
    if (!ls_auth_ensure(auth, &token)) {
        snprintf(errbuf, errlen, "%.120s", auth->last_error);
        return LS_HTTP_TRANSPORT_ERR;
    }
    ls_http_req_t req = {0};
    req.url = url;
    req.token = token;
    req.tr_cd = tr_cd;
    req.tr_cont = "N";
    req.body_json = inblock_body;
    req.timeout_ms = 15000;
    ls_http_resp_t resp;
    ls_http_rc_t rc = ls_http_post(&req, &resp);
    if (rc != LS_HTTP_OK) {
        snprintf(errbuf, errlen, "%.80s %.40s", resp.err_detail, resp.rsp_msg);
        ls_http_resp_free(&resp);
        return rc;
    }
    *out = resp.body;
    return LS_HTTP_OK;
}

tr_ls_master_t *ls_master_fetch(ls_auth_t *auth, char *errbuf, size_t errlen) {
    ls_instrument_info_t *items =
        (ls_instrument_info_t *)calloc(LS_MASTER_STOCK_CAP + LS_MASTER_FUT_CAP, sizeof(ls_instrument_info_t));
    if (items == 0) {
        return 0;
    }
    ls_buf_t body = {0, 0};
    int rc = fetch_block(auth, LS_STOCK_MASTER_URL, "t8436",
                         "{\"t8436InBlock\":{\"gubun\":\"0\"}}", &body, errbuf, errlen);
    if (rc != LS_HTTP_OK) {
        free(items);
        return 0;
    }
    int n = ls_master_parse_stock(body.data, body.len, items, LS_MASTER_STOCK_CAP);
    free(body.data);
    body.data = 0;
    if (n < 0) {
        snprintf(errbuf, errlen, "stock master parse failed");
        free(items);
        return 0;
    }
    size_t count = (size_t)n;

    rc = fetch_block(auth, LS_FUT_MASTER_URL, "t8467",
                     "{\"t8467InBlock\":{\"gubun\":\"\"}}", &body, errbuf, errlen);
    if (rc == LS_HTTP_OK) {
        n = ls_master_parse_fut(body.data, body.len, items + count, LS_MASTER_FUT_CAP);
        if (n > 0) {
            count += (size_t)n;
        }
    } else {
        /* 선물 마스터 실패는 주식 레지스트리를 무효화하지 않는다 (부분 지원 기록) */
        snprintf(errbuf, errlen, "futures master unavailable: stock only");
    }
    free(body.data);

    tr_ls_master_t *m = (tr_ls_master_t *)malloc(sizeof(tr_ls_master_t));
    if (m == 0) {
        free(items);
        return 0;
    }
    m->items = items;
    m->count = count;
    return m;
}

void ls_master_free(tr_ls_master_t *m) {
    if (m != 0) {
        free(m->items);
        free(m);
    }
}

const ls_instrument_info_t *ls_master_find(tr_ls_master_t *m, const char *shcode) {
    if (m == 0 || shcode == 0) {
        return 0;
    }
    for (size_t i = 0; i < m->count; i++) {
        if (strcmp(m->items[i].shcode, shcode) == 0) {
            return &m->items[i];
        }
    }
    return 0;
}

size_t ls_master_count(tr_ls_master_t *m) {
    return m != 0 ? m->count : 0;
}

const ls_instrument_info_t *ls_master_at(tr_ls_master_t *m, size_t index) {
    return (m != 0 && index < m->count) ? &m->items[index] : 0;
}

/* ASCII 대소문자 무시 부분 문자열 검색 (한글 종목명은 바이트열 비교라 그대로 동작) */
static bool str_contains_ci(const char *hay, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) {
        return true;
    }
    for (const char *h = hay; *h != 0; h++) {
        size_t i = 0;
        while (i < nlen && h[i] != 0 &&
               tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) {
            return true;
        }
        if (h[i] == 0) {
            break;
        }
    }
    return false;
}

size_t ls_master_search(const tr_ls_master_t *m, const char *q,
                        const ls_instrument_info_t **out, size_t cap) {
    if (m == 0 || out == 0 || cap == 0) {
        return 0;
    }
    size_t n = 0;
    size_t qlen = q != 0 ? strlen(q) : 0;
    for (size_t i = 0; i < m->count && n < cap; i++) {
        const ls_instrument_info_t *it = &m->items[i];
        bool match = qlen == 0 || strncmp(it->shcode, q, qlen) == 0 || str_contains_ci(it->name, q);
        if (match) {
            out[n++] = it;
        }
    }
    return n;
}
