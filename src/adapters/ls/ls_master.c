#include "adapters/ls/ls_master.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adapters/ls/ls_http.h"
#include "adapters/ls/ls_ovsfut.h"
#include "yyjson.h"

#define LS_STOCK_MASTER_URL "https://openapi.ls-sec.co.kr:8080/stock/etc"
#define LS_FUT_MASTER_URL "https://openapi.ls-sec.co.kr:8080/futureoption/market-data"
#define LS_OVS_MASTER_URL "https://openapi.ls-sec.co.kr:8080/overseas-futureoption/market-data"
#define LS_MASTER_STOCK_CAP 4608
#define LS_MASTER_FUT_CAP 64
#define LS_MASTER_OVS_CAP 512 /* o3101 행 수 + 정적 표 (2026-10-01 실측 75행) */
#define LS_MASTER_OPT_CAP 8192 /* t8433 4610 + t8435 WK 228 (2026-10-04). 행사가 증가 여유 */

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

int ls_master_parse_opt(const char *body, size_t len, ls_instrument_info_t *out, size_t cap) {
    yyjson_doc *doc = yyjson_read((char *)body, len, 0);
    if (doc == 0) {
        return -1;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    /* t8433은 월물, t8435(gubun WK)는 위클리. 필드와 종목 종류는 같다. */
    yyjson_val *arr = yyjson_obj_get(root, "t8433OutBlock");
    if (!yyjson_is_arr(arr)) {
        arr = yyjson_obj_get(root, "t8435OutBlock");
    }
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
        it->market = LS_MARKET_KP200_OPT;
        it->is_futures = false;
        /* 프리미엄 3.00 미만은 0.01, 이상은 0.05. 파이프 틱은 하나라 더 잘게(0.01, raw 1) 둔다.
         * 0.05는 0.01의 배수라 큰 프리미엄도 이 격자에 맞는다. */
        it->tick_raw = 1.0;
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
    const size_t item_cap =
        LS_MASTER_STOCK_CAP + LS_MASTER_FUT_CAP + LS_MASTER_OVS_CAP + LS_MASTER_OPT_CAP;
    ls_instrument_info_t *items =
        (ls_instrument_info_t *)calloc(item_cap, sizeof(ls_instrument_info_t));
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
    body.data = 0;

    /* 지수옵션 t8433 (2026-10-04 실측 4610행, 코드 8자리, 이름 "C 2610   745.0").
     * 이 응답에는 위클리 문구가 없다. 실패해도 주식·선물 레지스트리는 유지한다. */
    int opt_n = 0;
    rc = fetch_block(auth, LS_FUT_MASTER_URL, "t8433",
                     "{\"t8433InBlock\":{\"dummy\":\"\"}}", &body, errbuf, errlen);
    if (rc == LS_HTTP_OK) {
        n = ls_master_parse_opt(body.data, body.len, items + count, LS_MASTER_OPT_CAP);
        if (n > 0) {
            count += (size_t)n;
            opt_n += n;
        }
        if (n == LS_MASTER_OPT_CAP) {
            printf("instruments: index-option master hit cap %d; dropped the rest\n",
                   LS_MASTER_OPT_CAP);
            fflush(stdout);
        }
    } else {
        snprintf(errbuf, errlen, "option master unavailable");
    }
    free(body.data);
    body.data = 0;

    /* 코스피200 위클리 t8435 gubun WK (2026-10-04 실측 228행, BAFC/CAFC,
     * 이름 "C 월 W1 1,140.0"). 월물과 같은 옵션으로 등록한다. 실패해도 앞 목록은 유지. */
    if (opt_n < LS_MASTER_OPT_CAP && count < item_cap) {
        size_t opt_room = (size_t)(LS_MASTER_OPT_CAP - opt_n);
        if (opt_room > item_cap - count) {
            opt_room = item_cap - count;
        }
        rc = fetch_block(auth, LS_FUT_MASTER_URL, "t8435",
                         "{\"t8435InBlock\":{\"gubun\":\"WK\"}}", &body, errbuf, errlen);
        if (rc == LS_HTTP_OK) {
            n = ls_master_parse_opt(body.data, body.len, items + count, opt_room);
            if (n > 0) {
                count += (size_t)n;
            }
            if (n == (int)opt_room) {
                printf("instruments: weekly-option master hit the remaining %d slots; dropped the rest\n",
                       (int)opt_room);
                fflush(stdout);
            }
        } else {
            snprintf(errbuf, errlen, "weekly option master unavailable");
        }
        free(body.data);
        body.data = 0;
    }

    /* 해외선물: o3101(마스터)은 있으면 로딩 — 이 계정은 HKEX/LME만 온다 (2026-10-01 실측).
     * 실패핏 국내 레지스트리를 무효화하지 않는다 (t8467과 같은 관용 패턴).
     * DotGb > 2(소수 3자리 이상) 종목은 ×100 raw 가격이 절단되므로 등록하지 않는다 —
     * 정적 표의 배제 원칙과 같다 (ls_ovsfut.h 헤더 참조). 기동 경로라 종목당 로그는
     * 찍지 않고 요약 1줄만 남긴다 */
    rc = fetch_block(auth, LS_OVS_MASTER_URL, "o3101",
                     "{\"o3101InBlock\":{\"gubun\":\"\"}}", &body, errbuf, errlen);
    if (rc == LS_HTTP_OK) {
        static ls_ovsfut_master_row_t rows[LS_MASTER_OVS_CAP];
        n = ls_ovsfut_parse_master(body.data, body.len, rows, LS_MASTER_OVS_CAP);
        int excluded = 0;
        for (int i = 0; i < n && count < item_cap; i++) {
            if (!ls_ovsfut_fits_raw100(rows[i].dot_gb)) {
                excluded++;
                continue;
            }
            ls_instrument_info_t *it = &items[count];
            memset(it, 0, sizeof(*it));
            snprintf(it->shcode, sizeof(it->shcode), "%.11s", rows[i].symbol);
            snprintf(it->name, sizeof(it->name), "%.63s", rows[i].name);
            snprintf(it->expcode, sizeof(it->expcode), "%.19s", rows[i].exch_cd);
            it->market = LS_MARKET_OVS_FUT;
            it->is_futures = true;
            it->tick_raw = rows[i].tick_raw;
            count++;
        }
        if (excluded > 0) {
            printf("instruments: excluded %d overseas futures for precision (DotGb>2, avoids truncating x100 raw prices)\n",
                   excluded);
            fflush(stdout); /* 데몬의 stdout은 블록 버퍼링이라 즉시 보이게 한다 */
        }
    }
    free(body.data);

    /* CME 등 o3101에 없는 해외선물은 내장 정적 표를 등록한다 (월물 롤링 시 표 한 줄 갱신).
     * o3101에 이미 같은 코드가 있으면(거래소 개방 확대) 정적 쪽은 건너뛴다 */
    for (size_t i = 0; i < ls_ovsfut_count() && count < item_cap; i++) {
        const ls_ovsfut_entry_t *e = ls_ovsfut_at(i);
        bool dup = false;
        for (size_t j = 0; j < count; j++) {
            if (strcmp(items[j].shcode, e->contract) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        ls_instrument_info_t *it = &items[count];
        memset(it, 0, sizeof(*it));
        snprintf(it->shcode, sizeof(it->shcode), "%.11s", e->contract);
        snprintf(it->name, sizeof(it->name), "%.63s", e->name);
        snprintf(it->expcode, sizeof(it->expcode), "%.19s", e->prefix);
        it->market = LS_MARKET_OVS_FUT;
        it->is_futures = true;
        it->tick_raw = e->tick_raw;
        count++;
    }

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

/* ASCII 대소문자 무시 접두 비교. 종목코드는 대문자로 저장된다. */
static bool prefix_ci(const char *s, const char *q, size_t qlen) {
    for (size_t i = 0; i < qlen; i++) {
        if (s[i] == 0 ||
            tolower((unsigned char)s[i]) != tolower((unsigned char)q[i])) {
            return false;
        }
    }
    return true;
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

static int item_kind(const ls_instrument_info_t *it) {
    if (it->market == LS_MARKET_OVS_FUT) {
        return 2;
    }
    if (it->market == LS_MARKET_KP200_OPT) {
        return 3;
    }
    return it->is_futures ? 1 : 0;
}

static bool all_digits(const char *s, size_t n) {
    if (s == 0) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return n > 0;
}

bool ls_opt_parse_name(const char *name, char *cp_out, char *expiry, size_t expiry_cap,
                       double *strike_out) {
    if (cp_out != 0) {
        *cp_out = 0;
    }
    if (expiry != 0 && expiry_cap > 0) {
        expiry[0] = 0;
    }
    if (strike_out != 0) {
        *strike_out = 0;
    }
    if (name == 0) {
        return false;
    }
    while (*name == ' ') {
        name++;
    }
    if (*name != 'C' && *name != 'P') {
        return false;
    }
    char cp = *name++;
    while (*name == ' ') {
        name++;
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "%s", name);
    char *toks[8];
    int nt = 0;
    char *p = buf;
    while (*p != 0 && nt < 8) {
        while (*p == ' ') {
            p++;
        }
        if (*p == 0) {
            break;
        }
        toks[nt++] = p;
        while (*p != 0 && *p != ' ') {
            p++;
        }
        if (*p != 0) {
            *p++ = 0;
        }
    }
    if (nt < 2) {
        return false;
    }
    const char *st = toks[nt - 1];
    bool digit = false;
    for (const char *s = st; *s != 0; s++) {
        if (*s >= '0' && *s <= '9') {
            digit = true;
        }
    }
    if (!digit) {
        return false;
    }
    char num[32];
    size_t k = 0;
    for (const char *s = st; *s != 0 && k + 1 < sizeof(num); s++) {
        if (*s == ',') {
            continue;
        }
        num[k++] = *s;
    }
    num[k] = 0;
    char *end = 0;
    double strike = strtod(num, &end);
    if (end == num) {
        return false;
    }
    char ex[32];
    ex[0] = 0;
    if (nt == 2 && strlen(toks[0]) == 4 && all_digits(toks[0], 4)) {
        snprintf(ex, sizeof(ex), "%s", toks[0]);
    } else {
        size_t used = 0;
        for (int i = 0; i < nt - 1; i++) {
            size_t len = strlen(toks[i]);
            if (used > 0 && used + 1 < sizeof(ex)) {
                ex[used++] = ' ';
            }
            if (used + len >= sizeof(ex)) {
                return false;
            }
            memcpy(ex + used, toks[i], len);
            used += len;
            ex[used] = 0;
        }
    }
    if (ex[0] == 0) {
        return false;
    }
    if (cp_out != 0) {
        *cp_out = cp;
    }
    if (strike_out != 0) {
        *strike_out = strike;
    }
    if (expiry != 0 && expiry_cap > 0) {
        snprintf(expiry, expiry_cap, "%s", ex);
    }
    return true;
}

static bool expiry_is_month(const char *s) {
    return strlen(s) == 4 && all_digits(s, 4);
}

static int expiry_cmp(const void *a, const void *b) {
    const char *ea = (const char *)a;
    const char *eb = (const char *)b;
    int ma = expiry_is_month(ea);
    int mb = expiry_is_month(eb);
    if (ma != mb) {
        return ma - mb; /* 위클리가 월물보다 앞 */
    }
    return strcmp(ea, eb);
}

size_t ls_master_option_expiries(const tr_ls_master_t *m, char *out, size_t out_stride, size_t cap) {
    if (m == 0 || out == 0 || out_stride < 2 || cap == 0) {
        return 0;
    }
    size_t n = 0;
    for (size_t i = 0; i < m->count; i++) {
        if (item_kind(&m->items[i]) != 3) {
            continue;
        }
        char ex[24];
        if (!ls_opt_parse_name(m->items[i].name, 0, ex, sizeof(ex), 0)) {
            continue;
        }
        bool seen = false;
        for (size_t j = 0; j < n; j++) {
            if (strcmp(out + j * out_stride, ex) == 0) {
                seen = true;
                break;
            }
        }
        if (seen || n >= cap) {
            continue;
        }
        snprintf(out + n * out_stride, out_stride, "%s", ex);
        n++;
    }
    if (n > 1) {
        /* out_stride가 정렬 원소 크기와 같아야 qsort가 맞다. 호출은 24로 고정한다. */
        if (out_stride == 24) {
            qsort(out, n, out_stride, expiry_cmp);
        }
    }
    return n;
}

static bool collect_take(const ls_instrument_info_t *it, int kind, const char *expiry) {
    if (kind >= 0 && item_kind(it) != kind) {
        return false;
    }
    if (expiry != 0 && expiry[0] != 0) {
        char ex[24];
        if (!ls_opt_parse_name(it->name, 0, ex, sizeof(ex), 0) || strcmp(ex, expiry) != 0) {
            return false;
        }
    }
    return true;
}

size_t ls_master_collect(const tr_ls_master_t *m, int kind, const char *q, const char *expiry,
                         const ls_instrument_info_t **out, size_t cap) {
    if (m == 0 || out == 0 || cap == 0) {
        return 0;
    }
    size_t qlen = q != 0 ? strlen(q) : 0;
    size_t n = 0;
    if (qlen == 0) {
        for (size_t i = 0; i < m->count && n < cap; i++) {
            if (collect_take(&m->items[i], kind, expiry)) {
                out[n++] = &m->items[i];
            }
        }
        return n;
    }
    for (int pass = 0; pass < 3 && n < cap; pass++) {
        for (size_t i = 0; i < m->count && n < cap; i++) {
            const ls_instrument_info_t *it = &m->items[i];
            if (!collect_take(it, kind, expiry)) {
                continue;
            }
            size_t slen = strlen(it->shcode);
            bool exact = slen == qlen && prefix_ci(it->shcode, q, qlen);
            bool prefix = slen > qlen && prefix_ci(it->shcode, q, qlen);
            bool name = str_contains_ci(it->name, q) && !exact && !prefix;
            bool take = (pass == 0 && exact) || (pass == 1 && prefix) || (pass == 2 && name);
            if (take) {
                out[n++] = it;
            }
        }
    }
    return n;
}

size_t ls_master_search(const tr_ls_master_t *m, const char *q,
                        const ls_instrument_info_t **out, size_t cap) {
    if (m == 0 || out == 0 || cap == 0) {
        return 0;
    }
    size_t n = 0;
    size_t qlen = q != 0 ? strlen(q) : 0;
    if (qlen == 0) {
        for (size_t i = 0; i < m->count && n < cap; i++) {
            out[n++] = &m->items[i];
        }
        return n;
    }
    /* 정확 코드, 코드 접두, 이름 부분일치 순. 상한 안에서 옵션 코드가 이름 오탐에 밀리지 않게 한다. */
    for (int pass = 0; pass < 3 && n < cap; pass++) {
        for (size_t i = 0; i < m->count && n < cap; i++) {
            const ls_instrument_info_t *it = &m->items[i];
            size_t slen = strlen(it->shcode);
            bool exact = slen == qlen && prefix_ci(it->shcode, q, qlen);
            bool prefix = slen > qlen && prefix_ci(it->shcode, q, qlen);
            bool name = str_contains_ci(it->name, q) && !exact && !prefix;
            bool take = (pass == 0 && exact) || (pass == 1 && prefix) || (pass == 2 && name);
            if (take) {
                out[n++] = it;
            }
        }
    }
    return n;
}
