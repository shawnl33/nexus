#ifndef _WIN32
#define _POSIX_C_SOURCE 199309L /* clock_gettime/nanosleep (throttle_1tps) */
#endif

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

/* 차트 TR은 초당 1건(1 TPS) 제한 — 연속 조회 시 서버가 HTTP 500으로 거절한다 (2026-09-28 실측).
 * 요청 간 최소 간격을 여기서 강제한다 (헤더 주석의 계약 구현). */
static int64_t g_last_req_us[5]; /* 0=t8412, 1=t8465, 2=t8461, 3=t8410, 4=t8466 */

static int64_t mono_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

static void sleep_us(int64_t us) {
#ifdef _WIN32
    Sleep((DWORD)(us / 1000));
#else
    struct timespec w;
    w.tv_sec = (time_t)(us / 1000000);
    w.tv_nsec = (long)(us % 1000000) * 1000L;
    nanosleep(&w, 0);
#endif
}

static void throttle_slot(int slot) {
    if (slot < 0 || slot >= (int)(sizeof(g_last_req_us) / sizeof(g_last_req_us[0]))) {
        return;
    }
    int64_t now = mono_us();
    int64_t last = g_last_req_us[slot];
    const int64_t min_gap_us = 1100000; /* 1 TPS + 10% 여유 */
    if (last != 0 && now - last < min_gap_us) {
        sleep_us(min_gap_us - (now - last));
        now = mono_us();
    }
    g_last_req_us[slot] = now;
}

static const char *path_for(ls_chart_kind_t kind) {
    switch (kind) {
    case LS_CHART_STOCK_MIN:
    case LS_CHART_STOCK_DAY: return "https://openapi.ls-sec.co.kr:8080/stock/chart";
    case LS_CHART_FUT_MIN:
    case LS_CHART_FUT_DAY: return "https://openapi.ls-sec.co.kr:8080/futureoption/chart";
    }
    return 0;
}

static const char *tr_for(ls_chart_kind_t kind) {
    switch (kind) {
    case LS_CHART_STOCK_MIN: return "t8412";
    case LS_CHART_FUT_MIN: return "t8465";
    case LS_CHART_STOCK_DAY: return "t8410";
    case LS_CHART_FUT_DAY: return "t8466";
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
    if (kind != LS_CHART_STOCK_MIN && kind != LS_CHART_FUT_MIN) {
        snprintf(errbuf, errlen, "parse_page is for minute TRs only");
        return LS_HTTP_PARSE_ERR;
    }
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
                          int32_t ncnt, int32_t qrycnt, const char *edate, const char *etime,
                          const char *cts_date, const char *cts_time,
                          uint64_t instrument_id, uint64_t source_id,
                          tr_candle_t *out, size_t out_cap, ls_chart_page_t *page,
                          char *errbuf, size_t errlen) {
    if (shcode == 0 || out == 0 || page == 0 || ncnt <= 0 || qrycnt <= 0) {
        return LS_HTTP_PARSE_ERR;
    }
    if (kind != LS_CHART_STOCK_MIN && kind != LS_CHART_FUT_MIN) {
        snprintf(errbuf, errlen, "fetch_minute is for minute TRs only");
        return LS_HTTP_PARSE_ERR;
    }
    const char *token;
    if (!ls_auth_ensure(auth, &token)) {
        snprintf(errbuf, errlen, "%.120s", auth->last_error);
        return LS_HTTP_TRANSPORT_ERR;
    }

    const char *inblock = tr_for(kind);
    char body[1024];
    /* 연속 조회는 InBlock cts가 아니라 edate/etime을 이전 페이지 cts 값으로 옮기는
     * 방식만 서버가 받아들인다 (2026-09-28 t8465 실측, docs/ls_api_mapping.md §3) */
    snprintf(body, sizeof(body),
        "{\"%sInBlock\":{\"shcode\":\"%s\",\"ncnt\":%d,\"qrycnt\":%d,"
        "\"nday\":\"0\",\"sdate\":\" \",\"stime\":\" \",\"edate\":\"%s\",\"etime\":\"%s\","
        "\"cts_date\":\"%s\",\"cts_time\":\"%s\",\"comp_yn\":\"N\"}}",
        inblock, shcode, (int)ncnt, (int)qrycnt,
        edate != 0 && edate[0] > ' ' ? edate : "99999999",
        etime != 0 && etime[0] > ' ' ? etime : " ",
        cts_date != 0 && cts_date[0] > ' ' ? cts_date : " ",
        cts_time != 0 && cts_time[0] > ' ' ? cts_time : " ");

    ls_http_req_t req = {0};
    req.url = path_for(kind);
    req.token = token;
    req.tr_cd = tr_for(kind);
    req.tr_cont = "N";
    req.body_json = body;
    req.timeout_ms = 10000;

    throttle_slot((int)kind);
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

/* ---------- KRX야간파생 (t8461) ---------- */

static bool is_weekday(int64_t days) {
    unsigned wd = tr_weekday_from_days(days);
    return wd >= 1 && wd <= 5;
}

int64_t ls_fut_night_session_day(tr_time_us_t now_us) {
    int64_t days;
    uint32_t min;
    tr_local_day_and_min(now_us, KST_OFFSET_MIN, &days, &min);
    int64_t d = days;
    if (min < 18u * 60u) {
        d -= 1; /* 오늘 18:00 세션이 아직 안 열림 (자정~17:59, 주간 세션 포함) */
    }
    while (!is_weekday(d)) {
        d -= 1;
    }
    return d;
}

/* HHMMSS(고정 6자리) → 초. 형식 이상이면 -1. */
static int64_t hhmmss_to_sec(const char *s) {
    if (s == 0 || strlen(s) < 6) {
        return -1;
    }
    char hh[3] = {s[0], s[1], 0};
    char mm[3] = {s[2], s[3], 0};
    char ss[3] = {s[4], s[5], 0};
    return (int64_t)atoi(hh) * 3600 + atoi(mm) * 60 + atoi(ss);
}

/* trading_days(오름차순)에서 d 이하의 가장 최근 거래일. 없으면 d 그대로. */
static int64_t latest_trading_day(const int64_t *tdays, size_t n, int64_t d) {
    int64_t best = d;
    for (size_t i = 0; i < n; i++) {
        if (tdays[i] <= d) {
            best = tdays[i];
        } else {
            break;
        }
    }
    return best;
}

int ls_chart_parse_fut_night(const char *body, size_t body_len,
                             const int64_t *trading_days, size_t n_trading_days,
                             int64_t newest_session_day,
                             uint64_t instrument_id, uint64_t source_id,
                             tr_candle_t *out, size_t out_cap, char *errbuf, size_t errlen) {
    yyjson_doc *doc = yyjson_read((char *)body, body_len, 0);
    if (doc == 0) {
        snprintf(errbuf, errlen, "night chart response is not JSON");
        return -1;
    }
    yyjson_val *arr = yyjson_obj_get(yyjson_doc_get_root(doc), "t8461OutBlock1");
    if (!yyjson_is_arr(arr)) {
        yyjson_doc_free(doc);
        return 0;
    }
    size_t rows = yyjson_arr_size(arr);
    size_t n = rows < out_cap ? rows : out_cap; /* 초과 시 최신 n개만 */
    /* 시작 기준일: newest_session_day 이하의 가장 최근 거래일 (휴일 앵커 보정) */
    int64_t sess = latest_trading_day(trading_days, n_trading_days, newest_session_day);
    int prev_evening = -1; /* 1=저녁(18:00~), 0=아침(~05:00) */
    size_t idx, max;
    yyjson_val *row;
    yyjson_arr_foreach(arr, idx, max, row) {
        if (idx >= n) {
            break;
        }
        int64_t sec = hhmmss_to_sec(yyjson_get_str(yyjson_obj_get(row, "chetime")));
        int evening = sec >= 18 * 3600 ? 1 : 0;
        if (sec < 0) {
            continue;
        }
        /* E→M 전이(뒤로 걸을 때)만 기준일을 직전 거래일로 옮긴다.
         * M→E 전이는 같은 세션의 저녁 부분이다. */
        if (prev_evening == 1 && !evening) {
            sess = latest_trading_day(trading_days, n_trading_days, sess - 1);
        }
        int64_t bar_day = evening ? sess : sess + 1;
        prev_evening = evening;

        tr_time_us_t midnight;
        tr_local_midnight(bar_day, KST_OFFSET_MIN, &midnight);
        tr_candle_t c;
        memset(&c, 0, sizeof(c));
        c.instrument_id = instrument_id;
        c.timeframe_sec = 60;
        c.open_time_us = midnight + sec * TR_US_PER_SEC;
        c.close_time_us = c.open_time_us + 60 * TR_US_PER_SEC;
        if (!parse_price_scaled(yyjson_obj_get(row, "open"), &c.open) ||
            !parse_price_scaled(yyjson_obj_get(row, "high"), &c.high) ||
            !parse_price_scaled(yyjson_obj_get(row, "low"), &c.low) ||
            !parse_price_scaled(yyjson_obj_get(row, "price"), &c.close)) {
            continue;
        }
        c.volume = parse_volume(yyjson_obj_get(row, "cvolume"));
        c.state = TR_CANDLE_CLOSED;
        c.revision = 0;
        c.source_id = source_id;
        c.quality = TR_QUALITY_NONE;
        out[n - 1 - idx] = c; /* 입력은 내림차순 — 뒤에서부터 써서 오름차순으로 */
    }
    yyjson_doc_free(doc);
    /* continue로 건너뛴 행이 있으면 오름차순이 깨진다 — 최종 확인 */
    for (size_t i = 1; i < n; i++) {
        if (out[i].open_time_us != 0 && out[i - 1].open_time_us != 0 &&
            out[i].open_time_us <= out[i - 1].open_time_us) {
            snprintf(errbuf, errlen, "night bars out of order after date assignment");
            return -1;
        }
    }
    return (int)n;
}

int ls_chart_fetch_fut_night(ls_auth_t *auth, const char *focode, int32_t cnt,
                             const int64_t *trading_days, size_t n_trading_days,
                             uint64_t instrument_id, uint64_t source_id,
                             tr_candle_t *out, size_t out_cap, size_t *out_count,
                             char *errbuf, size_t errlen) {
    if (focode == 0 || out == 0 || out_count == 0 || cnt <= 0 || trading_days == 0) {
        return LS_HTTP_PARSE_ERR;
    }
    if (cnt > 999) {
        cnt = 999; /* 서버 상한 (2026-09-28 실측, 1000 이상 IGW40011) */
    }
    const char *token;
    if (!ls_auth_ensure(auth, &token)) {
        snprintf(errbuf, errlen, "%.120s", auth->last_error);
        return LS_HTTP_TRANSPORT_ERR;
    }
    char body[512];
    snprintf(body, sizeof(body),
             "{\"t8461InBlock\":{\"focode\":\"%s\",\"cgubun\":\"B\",\"bgubun\":\"1\",\"cnt\":%d}}",
             focode, (int)cnt);
    ls_http_req_t req = {0};
    req.url = "https://openapi.ls-sec.co.kr:8080/futureoption/chart";
    req.token = token;
    req.tr_cd = "t8461";
    req.tr_cont = "N";
    req.body_json = body;
    req.timeout_ms = 10000;

    throttle_slot(2);
    ls_http_resp_t resp;
    ls_http_rc_t rc = ls_http_post(&req, &resp);
    if (rc != LS_HTTP_OK) {
        snprintf(errbuf, errlen, "%.80s %.40s", resp.err_detail, resp.rsp_msg);
        ls_http_resp_free(&resp);
        return rc;
    }
    int n = ls_chart_parse_fut_night(resp.body.data, resp.body.len,
                                     trading_days, n_trading_days,
                                     ls_fut_night_session_day((tr_time_us_t)time(0) * TR_US_PER_SEC),
                                     instrument_id, source_id, out, out_cap, errbuf, errlen);
    ls_http_resp_free(&resp);
    if (n < 0) {
        return LS_HTTP_PARSE_ERR;
    }
    *out_count = (size_t)n;
    return n > 0 ? LS_HTTP_OK : LS_CHART_EMPTY;
}

/* ---------- 일봉 (t8410 주식 / t8466 선물 — t8466은 2026-09-29 실측 완료, t8410은 미검증) ---------- */

/* date "20260928" (KST 날짜) → days since epoch */
static bool parse_date_day(const char *date, int64_t *out) {
    if (date == 0 || strlen(date) != 8) {
        return false;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%.4s", date);
    int y = atoi(buf);
    snprintf(buf, sizeof(buf), "%.2s", date + 4);
    unsigned mo = (unsigned)atoi(buf);
    snprintf(buf, sizeof(buf), "%.2s", date + 6);
    unsigned d = (unsigned)atoi(buf);
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31) {
        return false;
    }
    *out = tr_days_from_civil(y, mo, d);
    return true;
}

static int daily_bar_cmp(const void *a, const void *b) {
    int64_t da = ((const ls_daily_bar_t *)a)->day;
    int64_t db = ((const ls_daily_bar_t *)b)->day;
    return (da > db) - (da < db);
}

int ls_chart_parse_daily(const char *body, size_t body_len, ls_chart_kind_t kind,
                         ls_daily_bar_t *out, size_t out_cap, size_t *out_count,
                         char *errbuf, size_t errlen) {
    if (out == 0 || out_count == 0) {
        return LS_HTTP_PARSE_ERR;
    }
    *out_count = 0;
    if (kind != LS_CHART_STOCK_DAY && kind != LS_CHART_FUT_DAY) {
        snprintf(errbuf, errlen, "parse_daily is for daily TRs only");
        return LS_HTTP_PARSE_ERR;
    }
    yyjson_doc *doc = yyjson_read((char *)body, body_len, 0);
    if (doc == 0) {
        snprintf(errbuf, errlen, "daily chart response is not JSON");
        return LS_HTTP_PARSE_ERR;
    }
    char ob1[24];
    snprintf(ob1, sizeof(ob1), "%sOutBlock1", tr_for(kind));
    yyjson_val *bars = yyjson_obj_get(yyjson_doc_get_root(doc), ob1);
    if (!yyjson_is_arr(bars)) {
        /* 성공이지만 데이터 없음: 오류가 아니다 (분봉 파서와 같은 규칙) */
        yyjson_doc_free(doc);
        return LS_CHART_EMPTY;
    }
    size_t n = 0;
    size_t idx, max;
    yyjson_val *bar;
    yyjson_arr_foreach(bars, idx, max, bar) {
        if (n >= out_cap) {
            break;
        }
        ls_daily_bar_t *b = &out[n];
        if (!parse_date_day(yyjson_get_str(yyjson_obj_get(bar, "date")), &b->day) ||
            !parse_price_scaled(yyjson_obj_get(bar, "high"), &b->high) ||
            !parse_price_scaled(yyjson_obj_get(bar, "low"), &b->low)) {
            continue;
        }
        n++;
    }
    yyjson_doc_free(doc);

    /* t8466은 실측으로 오름차순(과거→최신, 당일 진행 중 일봉 포함)을 확인했다
     * (2026-09-29). t8410은 미검증 — 두 kind 모두 거래일 오름차순 정렬을 유지하고
     * 중복 거래일은 거부한다 (분봉 파서의 중복 거부와 같은 규칙) */
    qsort(out, n, sizeof(out[0]), daily_bar_cmp);
    for (size_t i = 1; i < n; i++) {
        if (out[i].day == out[i - 1].day) {
            snprintf(errbuf, errlen, "duplicate daily bar date");
            return LS_HTTP_PARSE_ERR;
        }
    }
    *out_count = n;
    return n > 0 ? LS_HTTP_OK : LS_CHART_EMPTY;
}

int ls_chart_fetch_daily(ls_auth_t *auth, ls_chart_kind_t kind, const char *shcode,
                         int32_t qrycnt, const char *edate,
                         ls_daily_bar_t *out, size_t out_cap, size_t *out_count,
                         char *errbuf, size_t errlen) {
    if (shcode == 0 || out == 0 || out_count == 0 || qrycnt <= 0) {
        return LS_HTTP_PARSE_ERR;
    }
    if (kind != LS_CHART_STOCK_DAY && kind != LS_CHART_FUT_DAY) {
        snprintf(errbuf, errlen, "fetch_daily is for daily TRs only");
        return LS_HTTP_PARSE_ERR;
    }
    const char *token;
    if (!ls_auth_ensure(auth, &token)) {
        snprintf(errbuf, errlen, "%.120s", auth->last_error);
        return LS_HTTP_TRANSPORT_ERR;
    }
    const char *ed = edate != 0 && edate[0] > ' ' ? edate : "99999999";
    char body[512];
    if (kind == LS_CHART_STOCK_DAY) {
        /* t8410은 수정주가 여부(sujung) 필드가 있다. 프라임한 과거 일봉이 분봉으로
         * 완성되는 최근 세션(현재 가격 스케일)과 이어져야 하므로 수정주가를 적용한다 */
        snprintf(body, sizeof(body),
                 "{\"t8410InBlock\":{\"shcode\":\"%s\",\"gubun\":\"2\",\"qrycnt\":%d,"
                 "\"sdate\":\" \",\"edate\":\"%s\",\"cts_date\":\" \",\"comp_yn\":\"N\",\"sujung\":\"Y\"}}",
                 shcode, (int)qrycnt, ed);
    } else {
        snprintf(body, sizeof(body),
                 "{\"t8466InBlock\":{\"shcode\":\"%s\",\"gubun\":\"2\",\"qrycnt\":%d,"
                 "\"sdate\":\" \",\"edate\":\"%s\",\"cts_date\":\" \",\"comp_yn\":\"N\"}}",
                 shcode, (int)qrycnt, ed);
    }

    ls_http_req_t req = {0};
    req.url = path_for(kind);
    req.token = token;
    req.tr_cd = tr_for(kind);
    req.tr_cont = "N";
    req.body_json = body;
    req.timeout_ms = 10000;

    throttle_slot(kind == LS_CHART_FUT_DAY ? 4 : 3);
    ls_http_resp_t resp;
    ls_http_rc_t rc = ls_http_post(&req, &resp);
    if (rc != LS_HTTP_OK) {
        snprintf(errbuf, errlen, "%.80s %.40s", resp.err_detail, resp.rsp_msg);
        ls_http_resp_free(&resp);
        return rc;
    }
    rc = ls_chart_parse_daily(resp.body.data, resp.body.len, kind, out, out_cap, out_count,
                              errbuf, errlen);
    ls_http_resp_free(&resp);
    return rc;
}
