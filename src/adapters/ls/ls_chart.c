#include "adapters/ls/ls_chart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "adapters/ls/ls_http.h"
#include "core/model/civil_time.h"
#include "yyjson.h"

#define KST_OFFSET_MIN 540

static const char *path_for(ls_chart_kind_t kind) {
    switch (kind) {
    case LS_CHART_STOCK_MIN: return "https://openapi.ls-sec.co.kr:8080/stock/chart";
    case LS_CHART_FUT_MIN: return "https://openapi.ls-sec.co.kr:8080/futureoption/chart";
    }
    return 0;
}

static const char *tr_for(ls_chart_kind_t kind) {
    switch (kind) {
    case LS_CHART_STOCK_MIN: return "t8412";
    case LS_CHART_FUT_MIN: return "t8465";
    }
    return 0;
}

/* date "20260928" + time "124900" (KST) → UTC epoch µs */
static bool kst_to_epoch_us(const char *date, const char *time_s, tr_time_us_t *out) {
    if (date == 0 || time_s == 0 || strlen(date) != 8 || strlen(time_s) < 4) {
        return false;
    }
    tr_civil_t c;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.4s", date);
    c.year = atoi(buf);
    snprintf(buf, sizeof(buf), "%.2s", date + 4);
    c.month = (unsigned)atoi(buf);
    snprintf(buf, sizeof(buf), "%.2s", date + 6);
    c.day = (unsigned)atoi(buf);
    size_t tl = strlen(time_s);
    char hh[3] = {time_s[0], time_s[1], 0};
    char mm[3] = {time_s[2], time_s[3], 0};
    char ss[3] = {tl >= 6 ? time_s[4] : '0', tl >= 6 ? time_s[5] : '0', 0};
    c.hour = (unsigned)atoi(hh);
    c.min = (unsigned)atoi(mm);
    c.sec = (unsigned)atoi(ss);
    return tr_time_us_from_civil(&c, KST_OFFSET_MIN, out);
}

/* 주식은 Number, 선물은 문자열 가격. raw는 항상 ×100 스케일로 통일한다. */
static bool parse_price_scaled(yyjson_val *v, int64_t *out) {
    if (yyjson_is_num(v)) {
        *out = (int64_t)llround(yyjson_get_num(v) * 100.0); /* get_num은 int/real 모두 처리 */
        return true;
    }
    if (yyjson_is_str(v)) {
        *out = (int64_t)llround(atof(yyjson_get_str(v)) * 100.0);
        return true;
    }
    return false;
}

static int64_t parse_volume(yyjson_val *v) {
    if (yyjson_is_num(v)) {
        return yyjson_get_sint(v);
    }
    if (yyjson_is_str(v)) {
        return atoll(yyjson_get_str(v));
    }
    return 0;
}

static int64_t hhmm_to_min(yyjson_val *v) {
    if (!yyjson_is_str(v) || strlen(yyjson_get_str(v)) < 4) {
        return -1;
    }
    const char *s = yyjson_get_str(v);
    char hh[3] = {s[0], s[1], 0};
    char mm[3] = {s[2], s[3], 0};
    return atoi(hh) * 60 + atoi(mm);
}

int ls_chart_parse_page(const char *body, size_t body_len, ls_chart_kind_t kind,
                        uint64_t instrument_id, uint64_t source_id, uint32_t timeframe_sec,
                        tr_candle_t *out, size_t out_cap, ls_chart_page_t *page,
                        char *errbuf, size_t errlen) {
    memset(page, 0, sizeof(*page));
    yyjson_doc *doc = yyjson_read((char *)body, body_len, 0);
    if (doc == 0) {
        snprintf(errbuf, errlen, "chart response is not JSON");
        return LS_HTTP_PARSE_ERR;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    char ob[24], ob1[24];
    snprintf(ob, sizeof(ob), "%sOutBlock", tr_for(kind));
    snprintf(ob1, sizeof(ob1), "%sOutBlock1", tr_for(kind));
    yyjson_val *summary = yyjson_obj_get(root, ob);
    yyjson_val *bars = yyjson_obj_get(root, ob1);

    if (!yyjson_is_arr(bars)) {
        /* 성공이지만 데이터 없음 (주말·초기 구간 등): 오류가 아니다 */
        yyjson_doc_free(doc);
        return LS_CHART_EMPTY;
    }

    if (yyjson_is_obj(summary)) {
        yyjson_val *cd = yyjson_obj_get(summary, "cts_date");
        yyjson_val *ct = yyjson_obj_get(summary, "cts_time");
        if (yyjson_is_str(cd)) {
            snprintf(page->cts_date, sizeof(page->cts_date), "%.8s", yyjson_get_str(cd));
        }
        if (yyjson_is_str(ct)) {
            snprintf(page->cts_time, sizeof(page->cts_time), "%.10s", yyjson_get_str(ct));
        }
        page->session_open_min = hhmm_to_min(yyjson_obj_get(summary, "s_time"));
        page->session_close_min = hhmm_to_min(yyjson_obj_get(summary, "e_time"));
        page->has_more = page->cts_date[0] != 0 && strcmp(page->cts_date, " ") != 0 &&
                         page->cts_time[0] != 0;
    }

    size_t n = 0;
    size_t idx, max;
    yyjson_val *bar;
    yyjson_arr_foreach(bars, idx, max, bar) {
        if (n >= out_cap) {
            break;
        }
        tr_time_us_t open_us;
        if (!kst_to_epoch_us(yyjson_get_str(yyjson_obj_get(bar, "date")),
                             yyjson_get_str(yyjson_obj_get(bar, "time")), &open_us)) {
            continue;
        }
        tr_candle_t *c = &out[n];
        memset(c, 0, sizeof(*c));
        c->instrument_id = instrument_id;
        c->timeframe_sec = timeframe_sec;
        c->open_time_us = open_us;
        c->close_time_us = open_us + (int64_t)timeframe_sec * TR_US_PER_SEC;
        if (!parse_price_scaled(yyjson_obj_get(bar, "open"), &c->open) ||
            !parse_price_scaled(yyjson_obj_get(bar, "high"), &c->high) ||
            !parse_price_scaled(yyjson_obj_get(bar, "low"), &c->low) ||
            !parse_price_scaled(yyjson_obj_get(bar, "close"), &c->close)) {
            continue;
        }
        c->volume = parse_volume(yyjson_obj_get(bar, "jdiff_vol"));
        c->state = TR_CANDLE_CLOSED;
        c->revision = 0;
        c->source_id = source_id;
        c->quality = TR_QUALITY_NONE;
        n++;
    }
    yyjson_doc_free(doc);

    /* LS 차트 페이지는 페이지 내 오름차순으로 온다 (실제 응답으로 확인, docs/ls_api_mapping.md).
       연속 페이지는 더 과거 구간을 가리킨다. 중복·역순 최종 확인만 한다. */
    for (size_t i = 1; i < n; i++) {
        if (out[i].open_time_us <= out[i - 1].open_time_us) {
            snprintf(errbuf, errlen, "duplicate or out-of-order bars in page");
            return LS_HTTP_PARSE_ERR;
        }
    }

    page->count = n;
    return n > 0 ? LS_HTTP_OK : LS_CHART_EMPTY;
}

int ls_chart_fetch_minute(ls_auth_t *auth, ls_chart_kind_t kind, const char *shcode,
                          int32_t ncnt, int32_t qrycnt, const char *edate,
                          const char *cts_date, const char *cts_time,
                          uint64_t instrument_id, uint64_t source_id,
                          tr_candle_t *out, size_t out_cap, ls_chart_page_t *page,
                          char *errbuf, size_t errlen) {
    if (shcode == 0 || out == 0 || page == 0 || ncnt <= 0 || qrycnt <= 0) {
        return LS_HTTP_PARSE_ERR;
    }
    const char *token;
    if (!ls_auth_ensure(auth, &token)) {
        snprintf(errbuf, errlen, "%.120s", auth->last_error);
        return LS_HTTP_TRANSPORT_ERR;
    }

    const char *inblock = tr_for(kind);
    char body[1024];
    snprintf(body, sizeof(body),
        "{\"%sInBlock\":{\"shcode\":\"%s\",\"ncnt\":%d,\"qrycnt\":%d,"
        "\"nday\":\"0\",\"sdate\":\" \",\"stime\":\" \",\"edate\":\"%s\",\"etime\":\" \","
        "\"cts_date\":\"%s\",\"cts_time\":\"%s\",\"comp_yn\":\"N\"}}",
        inblock, shcode, (int)ncnt, (int)qrycnt,
        edate != 0 && edate[0] != 0 ? edate : "99999999",
        cts_date != 0 && cts_date[0] > ' ' ? cts_date : " ",
        cts_time != 0 && cts_time[0] > ' ' ? cts_time : " ");

    ls_http_req_t req = {0};
    req.url = path_for(kind);
    req.token = token;
    req.tr_cd = tr_for(kind);
    req.tr_cont = "N";
    req.body_json = body;
    req.timeout_ms = 10000;

    ls_http_resp_t resp;
    ls_http_rc_t rc = ls_http_post(&req, &resp);
    if (rc != LS_HTTP_OK) {
        snprintf(errbuf, errlen, "%.80s %.40s", resp.err_detail, resp.rsp_msg);
        ls_http_resp_free(&resp);
        return rc;
    }
    rc = ls_chart_parse_page(resp.body.data, resp.body.len, kind, instrument_id, source_id,
                             (uint32_t)ncnt * 60u, out, out_cap, page, errbuf, errlen);
    ls_http_resp_free(&resp);
    return rc;
}
