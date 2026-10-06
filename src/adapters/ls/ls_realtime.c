#include "adapters/ls/ls_realtime.h"

#include <libwebsockets.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/model/civil_time.h"
#include "yyjson.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <mbedtls/x509_crt.h>
#endif

#define LS_RT_DEFAULT_URL "wss://openapi.ls-sec.co.kr:9443/websocket"
#define LS_RT_MAX_SUBS 16
#define LS_RT_RX_BUF (64 * 1024)

typedef struct {
    char tr_cd[8];
    char tr_key[16];
    uint64_t instrument_id;
    bool sent;
} ls_rt_sub_t;

struct tr_ls_rt {
    ls_rt_config_t cfg;
    struct lws_context *ctx;
    struct lws *wsi;
    ls_rt_state_t state;
    ls_rt_sub_t subs[LS_RT_MAX_SUBS];
    int n_subs;
    /* 입력 큐 (단일 스레드: service 호출자와 next 호출자가 같음) */
    ls_rt_event_t *queue;
    size_t q_head, q_count;
    uint64_t q_dropped;
    uint64_t reconnects;
    int64_t next_retry_us;
    int retry_ms;
    int64_t established_at_us; /* 세션 수립 시각(µs). 0이면 미수립 */
    int consec_short;          /* 연속 단기 세션 수 (건강한 세션 이후 단절 시 리셋) */
    int64_t last_reauth_us;    /* 마지막 토큰 강제 재발급 시도 시각(µs). 0이면 미시도 */
    char rx_buf[LS_RT_RX_BUF];
    size_t rx_len;
    bool sub_sent;
    int sub_sent_idx;
    /* lws 4.x는 lws_service의 timeout 인자를 무시한다(lib/plat/unix/unix-service.c:
     * 양수이면 LWS_POLL_WAIT_LIMIT으로 강제). poll 대기 상한은 sul 스케줄러로만
     * 제한할 수 있어, service 호출마다 wake sul을 걸어 timeout을 보장한다. */
    lws_sorted_usec_list_t sul_wake;
    uint64_t dbg_rx_frames; /* LS_RT_DEBUG=1일 때 수신 프레임 카운트 */
};

static int64_t default_now_us(void) {
    return (int64_t)time(0) * TR_US_PER_SEC;
}

static int64_t now_us(tr_ls_rt_t *rt) {
    return rt->cfg.now_us_fn != 0 ? rt->cfg.now_us_fn() : default_now_us();
}

/* ---------- 파싱 ---------- */

static int64_t parse_i64(yyjson_val *v) {
    if (yyjson_is_num(v)) {
        return yyjson_get_sint(v);
    }
    if (yyjson_is_str(v)) {
        return atoll(yyjson_get_str(v));
    }
    return 0;
}

static tr_price_t parse_price(yyjson_val *v) {
    if (yyjson_is_num(v)) {
        return (tr_price_t)llround(yyjson_get_num(v) * 100.0);
    }
    if (yyjson_is_str(v)) {
        return (tr_price_t)llround(atof(yyjson_get_str(v)) * 100.0);
    }
    return 0;
}

/* HHMMSS(문자열, KST) → 당일 epoch µs. 없으면 수신 시각. */
static int64_t parse_chetime(yyjson_val *v, int64_t recv_time_us) {
    if (!yyjson_is_str(v)) {
        return recv_time_us;
    }
    const char *s = yyjson_get_str(v);
    if (strlen(s) < 6) {
        return recv_time_us;
    }
    int64_t days;
    tr_local_day_and_min(recv_time_us, 540, &days, 0);
    tr_time_us_t midnight;
    tr_local_midnight(days, 540, &midnight);
    char hh[3] = {s[0], s[1], 0};
    char mm[3] = {s[2], s[3], 0};
    char ss[3] = {s[4], s[5], 0};
    int64_t us = (int64_t)atoi(hh) * 3600 + (int64_t)atoi(mm) * 60 + atoi(ss);
    return midnight + us * TR_US_PER_SEC;
}

/* YYYYMMDD + HHMMSS(둘 다 KST) → epoch µs. 형식 이상이면 수신 시각.
 * 해외선물 체결(OVC)의 kordate/kortm에 쓴다 — 현지 시각(trdtm/ovsdate)이 아니라
 * 한국 시각 필드가 따로 온다 (2026-10-01 실측, docs/ls_api_mapping.md §4). */
static int64_t parse_kor_datetime(yyjson_val *date_v, yyjson_val *tm_v, int64_t recv_time_us) {
    if (!yyjson_is_str(date_v) || !yyjson_is_str(tm_v)) {
        return recv_time_us;
    }
    const char *d = yyjson_get_str(date_v);
    const char *s = yyjson_get_str(tm_v);
    if (strlen(d) != 8 || strlen(s) < 6) {
        return recv_time_us;
    }
    char ybuf[5] = {d[0], d[1], d[2], d[3], 0};
    char mobuf[3] = {d[4], d[5], 0};
    char dbuf[3] = {d[6], d[7], 0};
    char hh[3] = {s[0], s[1], 0};
    char mm[3] = {s[2], s[3], 0};
    char ss[3] = {s[4], s[5], 0};
    tr_civil_t c;
    c.year = atoi(ybuf);
    c.month = (unsigned)atoi(mobuf);
    c.day = (unsigned)atoi(dbuf);
    c.hour = (unsigned)atoi(hh);
    c.min = (unsigned)atoi(mm);
    c.sec = (unsigned)atoi(ss);
    tr_time_us_t out;
    if (!tr_time_us_from_civil(&c, 540, &out)) {
        return recv_time_us;
    }
    return out;
}

bool tr_ls_rt_parse_message(const char *body, size_t len, uint64_t instrument_id,
                            int64_t recv_time_us, ls_rt_event_t *out) {
    memset(out, 0, sizeof(*out));
    yyjson_doc *doc = yyjson_read((char *)body, len, 0);
    if (doc == 0) {
        return false;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *header = yyjson_obj_get(root, "header");
    yyjson_val *b = yyjson_obj_get(root, "body");
    if (!yyjson_is_obj(header) || !yyjson_is_obj(b)) {
        yyjson_doc_free(doc);
        return false;
    }
    yyjson_val *tr_cd = yyjson_obj_get(header, "tr_cd");
    if (!yyjson_is_str(tr_cd)) {
        yyjson_doc_free(doc);
        return false;
    }
    snprintf(out->tr_cd, sizeof(out->tr_cd), "%.7s", yyjson_get_str(tr_cd));
    out->instrument_id = instrument_id;
    out->recv_time_us = recv_time_us;

    if (strcmp(out->tr_cd, "S3_") == 0 || strcmp(out->tr_cd, "K3_") == 0 ||
        strcmp(out->tr_cd, "FC9") == 0 || strcmp(out->tr_cd, "DC0") == 0 ||
        strcmp(out->tr_cd, "OC0") == 0 || strcmp(out->tr_cd, "US3") == 0) {
        out->kind = LS_RT_TICK;
        out->price = parse_price(yyjson_obj_get(b, "price"));
        out->qty = parse_i64(yyjson_obj_get(b, "cvolume"));
        out->volume_meaning = TR_TICK_VOLUME_PER_TRADE; /* cvolume = 개별 체결량 */
        out->event_time_us = parse_chetime(yyjson_obj_get(b, "chetime"), recv_time_us);
    } else if (strcmp(out->tr_cd, "OVC") == 0 || strcmp(out->tr_cd, "WOC") == 0) {
        /* 해외선물 체결 (2026-10-01 ESZ26 실측): curpr=현재가, trdq=개별 체결량,
         * 시각은 현지(trdtm)가 아니라 한국(kordate+kortm KST)을 그대로 쓴다.
         * trdq = 개별 체결량, totq = 누적 (PER_TRADE 매핑) */
        out->kind = LS_RT_TICK;
        out->price = parse_price(yyjson_obj_get(b, "curpr"));
        out->qty = parse_i64(yyjson_obj_get(b, "trdq"));
        out->volume_meaning = TR_TICK_VOLUME_PER_TRADE;
        out->event_time_us = parse_kor_datetime(yyjson_obj_get(b, "kordate"),
                                                yyjson_obj_get(b, "kortm"), recv_time_us);
    } else if (strcmp(out->tr_cd, "UH1") == 0) {
        out->kind = LS_RT_ORDERBOOK;
        out->event_time_us = parse_chetime(yyjson_obj_get(b, "hotime"), recv_time_us);
        /* 통합(KRX+NXT) 호가: 총잔량·단계별 잔량은 unt_ 접두사 (공식 명세, docs/ls_api_mapping.md §4) */
        out->bid_total = parse_i64(yyjson_obj_get(b, "unt_totbidrem"));
        out->ask_total = parse_i64(yyjson_obj_get(b, "unt_totofferrem"));
        out->level_count = 0;
        for (int i = 1; i <= 5; i++) {
            char key[24];
            snprintf(key, sizeof(key), "bidho%d", i);
            yyjson_val *bp = yyjson_obj_get(b, key);
            snprintf(key, sizeof(key), "unt_bidrem%d", i);
            yyjson_val *bv = yyjson_obj_get(b, key);
            snprintf(key, sizeof(key), "offerho%d", i);
            yyjson_val *ap = yyjson_obj_get(b, key);
            snprintf(key, sizeof(key), "unt_offerrem%d", i);
            yyjson_val *av = yyjson_obj_get(b, key);
            if (bp == 0 || ap == 0) {
                break;
            }
            out->levels[i - 1].price = parse_price(bp);
            out->levels[i - 1].qty = parse_i64(bv);
            out->levels[5 + i - 1].price = parse_price(ap);
            out->levels[5 + i - 1].qty = parse_i64(av);
            out->level_count = i;
        }
    } else if (strcmp(out->tr_cd, "H1_") == 0 || strcmp(out->tr_cd, "HA_") == 0 ||
               strcmp(out->tr_cd, "FH9") == 0 || strcmp(out->tr_cd, "DH0") == 0 ||
               strcmp(out->tr_cd, "OH0") == 0 ||
               strcmp(out->tr_cd, "OVH") == 0 || strcmp(out->tr_cd, "WOH") == 0) {
        out->kind = LS_RT_ORDERBOOK;
        /* 해외선물(OVH/WOH)의 hotime은 거래소 현지 시각이다 (2026-10-01 ESZ26 실측:
         * KST 22:33 수신 메시지의 hotime "083301" — 시카고 현지). 한국 날짜 필드가
         * 없어 epoch으로 못 옮기므로 수신 시각을 쓴다. 국내 채널의 hotime은 KST다 */
        bool ovs = strcmp(out->tr_cd, "OVH") == 0 || strcmp(out->tr_cd, "WOH") == 0;
        out->event_time_us =
            ovs ? recv_time_us : parse_chetime(yyjson_obj_get(b, "hotime"), recv_time_us);
        /* 실제 필드명(2026-09-28 H1_ 실측): totbidrem=총매수잔량, totofferrem=총매도잔량.
           Bids/Asks(예스랭귀지 매수/매도잔량)에 각각 대응한다 (docs/ls_api_mapping.md §4). */
        out->bid_total = parse_i64(yyjson_obj_get(b, "totbidrem"));
        out->ask_total = parse_i64(yyjson_obj_get(b, "totofferrem"));
        out->level_count = 0;
        /* 우선호가 1~5단계: bidhoN/bidremN(매수), offerhoN/offerremN(매도) */
        for (int i = 1; i <= 5; i++) {
            char key[16];
            snprintf(key, sizeof(key), "bidho%d", i);
            yyjson_val *bp = yyjson_obj_get(b, key);
            snprintf(key, sizeof(key), "bidrem%d", i);
            yyjson_val *bv = yyjson_obj_get(b, key);
            snprintf(key, sizeof(key), "offerho%d", i);
            yyjson_val *ap = yyjson_obj_get(b, key);
            snprintf(key, sizeof(key), "offerrem%d", i);
            yyjson_val *av = yyjson_obj_get(b, key);
            if (bp == 0 || ap == 0) {
                break;
            }
            out->levels[i - 1].price = parse_price(bp);
            out->levels[i - 1].qty = parse_i64(bv);
            out->levels[5 + i - 1].price = parse_price(ap);
            out->levels[5 + i - 1].qty = parse_i64(av);
            out->level_count = i;
        }
    } else {
        yyjson_doc_free(doc);
        return false; /* 미지원 채널 */
    }
    yyjson_doc_free(doc);
    return true;
}

/* ---------- 큐 ---------- */

static void queue_push(tr_ls_rt_t *rt, const ls_rt_event_t *ev) {
    if (rt->q_count == rt->cfg.queue_capacity) {
        rt->q_dropped++; /* 조용히 버리지 않고 카운트 */
        return;
    }
    size_t tail = (rt->q_head + rt->q_count) % rt->cfg.queue_capacity;
    rt->queue[tail] = *ev;
    rt->q_count++;
}

/* ---------- lws 콜백 ---------- */

int ls_rt_next_retry_ms(int64_t survived_ms, int current_retry_ms, int min_ms, int max_ms) {
    if (survived_ms >= LS_RT_HEALTHY_MS) {
        return min_ms; /* 건강한 세션 이후 단절: 빠른 복구를 위해 리셋 */
    }
    if (current_retry_ms < min_ms) {
        return min_ms;
    }
    if (current_retry_ms > max_ms) {
        return max_ms;
    }
    return current_retry_ms;
}

void ls_rt_note_session_end(bool healthy, int *consec_short) {
    if (healthy) {
        *consec_short = 0;
    } else {
        (*consec_short)++;
    }
}

bool ls_rt_should_reauth(int consec_short, int64_t last_reauth_us, int64_t now_us) {
    if (consec_short < LS_RT_REAUTH_THRESHOLD) {
        return false;
    }
    /* 미시도(0)이면 즉시 허용. 시계 역행(음수 차)은 쿨다운 유지로 처리한다 */
    return last_reauth_us <= 0 || now_us - last_reauth_us >= LS_RT_REAUTH_COOLDOWN_US;
}

size_t ls_rt_format_conn_error(const void *in, size_t len, char *out, size_t cap) {
    const char *prefix = "ls-rt: connection error";
    if (out == 0 || cap == 0) {
        return 0;
    }
    size_t n = 0;
    for (const char *p = prefix; *p != '\0' && n + 1 < cap; p++) {
        out[n++] = *p;
    }
    const unsigned char *s = (const unsigned char *)in;
    bool any = false;
    if (s != 0) {
        for (size_t i = 0; i < len; i++) {
            if (s[i] != 0) {
                any = true;
                break;
            }
        }
    }
    if (any) {
        if (n + 1 < cap) {
            out[n++] = ':';
        }
        if (n + 1 < cap) {
            out[n++] = ' ';
        }
        for (size_t i = 0; i < len && n + 1 < cap; i++) {
            unsigned char c = s[i];
            if (c == 0) {
                break;
            }
            if (c == '\n' || c == '\r') {
                c = ' ';
            }
            out[n++] = (char)c;
        }
    }
    if (n + 1 < cap) {
        out[n++] = '\n';
    }
    out[n] = '\0';
    return n;
}

static char pem_b64_digit(unsigned v) {
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    return tab[v & 63u];
}

bool ls_rt_append_pem_cert(char **buf, size_t *len, size_t *cap,
                           const unsigned char *der, size_t der_len) {
    static const char begin[] = "-----BEGIN CERTIFICATE-----\n";
    static const char end[] = "-----END CERTIFICATE-----\n";
    if (buf == 0 || len == 0 || cap == 0 || der == 0 || der_len == 0) {
        return false;
    }
    size_t b64_len = 4u * ((der_len + 2u) / 3u);
    size_t breaks = (b64_len + 63u) / 64u; /* 각 줄 끝의 \n */
    size_t need = (sizeof(begin) - 1u) + b64_len + breaks + (sizeof(end) - 1u);
    if (*len > SIZE_MAX - need) {
        return false;
    }
    size_t want = *len + need + 1u;
    if (*cap < want) {
        size_t ncap = *cap == 0 ? 256u : *cap;
        while (ncap < want) {
            if (ncap > SIZE_MAX / 2u) {
                return false;
            }
            ncap *= 2u;
        }
        char *grown = (char *)realloc(*buf, ncap);
        if (grown == 0) {
            return false;
        }
        *buf = grown;
        *cap = ncap;
    }
    char *dst = *buf + *len;
    memcpy(dst, begin, sizeof(begin) - 1u);
    dst += sizeof(begin) - 1u;
    size_t col = 0;
    size_t i = 0;
    while (i < der_len) {
        unsigned v = ((unsigned)der[i]) << 16;
        int remain = 1;
        if (i + 1 < der_len) {
            v |= ((unsigned)der[i + 1]) << 8;
            remain = 2;
        }
        if (i + 2 < der_len) {
            v |= (unsigned)der[i + 2];
            remain = 3;
        }
        char chunk[4];
        chunk[0] = pem_b64_digit(v >> 18);
        chunk[1] = pem_b64_digit(v >> 12);
        chunk[2] = remain >= 2 ? pem_b64_digit(v >> 6) : '=';
        chunk[3] = remain == 3 ? pem_b64_digit(v) : '=';
        for (int k = 0; k < 4; k++) {
            *dst++ = chunk[k];
            col++;
            if (col == 64) {
                *dst++ = '\n';
                col = 0;
            }
        }
        i += 3;
    }
    if (col != 0) {
        *dst++ = '\n';
    }
    memcpy(dst, end, sizeof(end) - 1u);
    dst += sizeof(end) - 1u;
    *dst = '\0';
    *len = (size_t)(dst - *buf);
    return true;
}

bool ls_rt_sub_ack_rejected(const char *body, size_t len) {
    yyjson_doc *doc = yyjson_read((char *)body, len, 0);
    if (doc == 0) {
        return false;
    }
    yyjson_val *header = yyjson_obj_get(yyjson_doc_get_root(doc), "header");
    yyjson_val *rsp_cd = yyjson_obj_get(header, "rsp_cd");
    bool rejected = yyjson_is_str(rsp_cd) && strcmp(yyjson_get_str(rsp_cd), "00000") != 0;
    yyjson_doc_free(doc);
    return rejected;
}

int ls_rt_key_pad_width(const char *tr_cd) {
    if (tr_cd == 0) {
        return 0;
    }
    /* 통합 채널은 10자리 고정 (단축코드 7 + 공백 3, 공식 명세) */
    if (strcmp(tr_cd, "US3") == 0 || strcmp(tr_cd, "UH1") == 0) {
        return 10;
    }
    /* 해외선물 채널은 8자리 고정 — 심볼 우측 공백 패딩 (미패딩 시 rsp_cd 10009,
     * 2026-10-01 ESZ26 실측) */
    if (strcmp(tr_cd, "OVC") == 0 || strcmp(tr_cd, "OVH") == 0 ||
        strcmp(tr_cd, "WOC") == 0 || strcmp(tr_cd, "WOH") == 0) {
        return 8;
    }
    return 0;
}

/* 구독 ACK 거절 로그. 토큰 무효 등 서버 거절 원인(rsp_cd/rsp_msg)이 로그에 바로 보이게 한다.
 * (2026-10-01 사건: 구독 거절이 28분간 반복됐지만 파싱하지 않아 원인이 보이지 않았다.) */
static void log_sub_ack_reject(const char *body, size_t len) {
    yyjson_doc *doc = yyjson_read((char *)body, len, 0);
    if (doc == 0) {
        return;
    }
    yyjson_val *header = yyjson_obj_get(yyjson_doc_get_root(doc), "header");
    const char *tr_cd = yyjson_get_str(yyjson_obj_get(header, "tr_cd"));
    const char *rsp_cd = yyjson_get_str(yyjson_obj_get(header, "rsp_cd"));
    const char *rsp_msg = yyjson_get_str(yyjson_obj_get(header, "rsp_msg"));
    if (rsp_cd != 0) {
        fprintf(stderr, "ls-rt: subscription rejected tr_cd=%s rsp_cd=%s rsp_msg=%s\n",
                tr_cd != 0 ? tr_cd : "?", rsp_cd, rsp_msg != 0 ? rsp_msg : "");
    }
    yyjson_doc_free(doc);
}

/* tr_key 비교: 통합 채널은 서버가 공백 패딩을 붙이므로 후행 공백을 무시한다 */
static bool key_equal(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    while (la > 0 && a[la - 1] == ' ') {
        la--;
    }
    while (lb > 0 && b[lb - 1] == ' ') {
        lb--;
    }
    return la == lb && strncmp(a, b, la) == 0;
}

static uint64_t find_instrument(tr_ls_rt_t *rt, const char *tr_cd, const char *tr_key) {
    for (int i = 0; i < rt->n_subs; i++) {
        if (strcmp(rt->subs[i].tr_cd, tr_cd) == 0 && key_equal(rt->subs[i].tr_key, tr_key)) {
            return rt->subs[i].instrument_id;
        }
    }
    return 0;
}

/* tr_type "3"=실시간 시세 등록, "4"=해제 (공식 명세, docs/ls_api_mapping.md §4) */
static void send_tr(tr_ls_rt_t *rt, const char *tr_type, const char *tr_cd, const char *tr_key) {
    const char *token = 0;
    if (!ls_auth_ensure(rt->cfg.auth, &token)) {
        rt->state = LS_RT_FAILED;
        return;
    }
    char buf[768];
    int n = snprintf(buf + LWS_PRE, sizeof(buf) - LWS_PRE,
        "{\"header\":{\"token\":\"%s\",\"tr_type\":\"%s\"},\"body\":{\"tr_cd\":\"%s\",\"tr_key\":\"%s\"}}",
        token, tr_type, tr_cd, tr_key);
    if (n > 0) {
        lws_write(rt->wsi, (unsigned char *)buf + LWS_PRE, (size_t)n, LWS_WRITE_TEXT);
    }
}

static void send_next_sub(tr_ls_rt_t *rt) {
    /* 아직 전송되지 않은 구독을 본낸다. 재연결 시에는 전부 미전송으로 되돌려 다시 본낸다. */
    for (; rt->sub_sent_idx < rt->n_subs; rt->sub_sent_idx++) {
        ls_rt_sub_t *s = &rt->subs[rt->sub_sent_idx];
        if (s->sent) {
            continue;
        }
        send_tr(rt, "3", s->tr_cd, s->tr_key);
        s->sent = true;
    }
}

static int callback_ls_rt(struct lws *wsi, enum lws_callback_reasons reason,
                          void *user, void *in, size_t len) {
    (void)user;
    tr_ls_rt_t *rt = (tr_ls_rt_t *)lws_context_user(lws_get_context(wsi));
    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        rt->state = LS_RT_READY;
        /* 백오프는 여기서 리셋하지 않는다 — 수립 직후 단절(수십 ms)이 반복되면
         * 리셋이 지수 백오프를 무력화한다. 리셋은 CLOSED/CONNECTION_ERROR에서
         * 세션 유지 시간을 보고 결정한다 (ls_rt_next_retry_ms). */
        rt->established_at_us = now_us(rt);
        /* 재연결: 모든 구독을 미전송으로 되돌리고 다시 본낸다 */
        for (int i = 0; i < rt->n_subs; i++) {
            rt->subs[i].sent = false;
        }
        rt->sub_sent_idx = 0;
        send_next_sub(rt);
        break;
    case LWS_CALLBACK_CLIENT_RECEIVE: {
        if (getenv("LS_RT_DEBUG") != 0) {
            rt->dbg_rx_frames++;
            if (rt->dbg_rx_frames <= 6 || rt->dbg_rx_frames % 500 == 0) {
                fprintf(stderr, "rt-debug: frame #%llu len=%zu final=%d head=%.100s\n",
                        (unsigned long long)rt->dbg_rx_frames, len,
                        lws_is_final_fragment(wsi) ? 1 : 0, (const char *)in);
            }
        }
        if (rt->sub_sent_idx < rt->n_subs) {
            /* 구독 ACK — 거절(rsp_cd != "00000")이면 원인을 로그에 남기고,
             * 정상이면 조용히 남은 구독을 본낸다 */
            if (ls_rt_sub_ack_rejected((const char *)in, len)) {
                log_sub_ack_reject((const char *)in, len);
            }
            send_next_sub(rt);
            break;
        }
        /* 메시지 조립 (fragment 대응) */
        if (rt->rx_len + len > sizeof(rt->rx_buf) - 1) {
            rt->rx_len = 0; /* 상한 초과 메시지는 폐기하고 카운트 */
            rt->q_dropped++;
            break;
        }
        memcpy(rt->rx_buf + rt->rx_len, in, len);
        rt->rx_len += len;
        if (!lws_is_final_fragment(wsi)) {
            break;
        }
        rt->rx_buf[rt->rx_len] = 0;
        rt->rx_len = 0;
        /* header의 tr_cd/tr_key로 구독 맵을 찾는다 */
        yyjson_doc *doc = yyjson_read(rt->rx_buf, strlen(rt->rx_buf), 0);
        if (doc == 0) {
            break;
        }
        yyjson_val *root = yyjson_doc_get_root(doc);
        yyjson_val *header = yyjson_obj_get(root, "header");
        const char *tr_cd = yyjson_get_str(yyjson_obj_get(header, "tr_cd"));
        const char *tr_key = yyjson_get_str(yyjson_obj_get(header, "tr_key"));
        uint64_t inst = find_instrument(rt, tr_cd != 0 ? tr_cd : "", tr_key != 0 ? tr_key : "");
        yyjson_doc_free(doc);

        ls_rt_event_t ev;
        if (tr_ls_rt_parse_message(rt->rx_buf, strlen(rt->rx_buf), inst, now_us(rt), &ev)) {
            queue_push(rt, &ev);
        } else if (getenv("LS_RT_DEBUG") != 0) {
            fprintf(stderr, "rt-debug: drop inst=%llu msg=%.120s\n",
                    (unsigned long long)inst, rt->rx_buf);
        }
        break;
    }
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
    case LWS_CALLBACK_CLIENT_CLOSED:
        if (reason == LWS_CALLBACK_CLIENT_CONNECTION_ERROR) {
            char line[512];
            ls_rt_format_conn_error(in, len, line, sizeof(line));
            fputs(line, stderr);
        }
        rt->wsi = 0;
        if (rt->state != LS_RT_FAILED) {
            rt->state = LS_RT_RECONNECTING;
            rt->reconnects++;
            int64_t now = now_us(rt);
            int64_t survived_ms = -1; /* 수립 없이 실패 */
            if (rt->established_at_us > 0) {
                survived_ms = (now - rt->established_at_us) / 1000;
                rt->established_at_us = 0;
            }
            int next_ms = ls_rt_next_retry_ms(survived_ms, rt->retry_ms,
                                              rt->cfg.reconnect_min_ms, rt->cfg.reconnect_max_ms);
            if (survived_ms >= 0 && survived_ms < LS_RT_HEALTHY_MS) {
                fprintf(stderr, "ls-rt: short session (%lldms, then dropped) - keeping backoff %dms\n",
                        (long long)survived_ms, next_ms);
            }
            /* 연속 단기 세션은 서버 측 토큰 무효화 가능성 — 강제 재발급 후 재접속한다.
             * ls_auth_ensure는 명목 유효기간만 보기 때문에 여기서 갱신해야 자가치유된다.
             * 쿨다운으로 발급 API 해머를 막는다 (거절 원인이 토큰이 아닐 수 있으므로). */
            ls_rt_note_session_end(survived_ms >= LS_RT_HEALTHY_MS, &rt->consec_short);
            if (ls_rt_should_reauth(rt->consec_short, rt->last_reauth_us, now)) {
                rt->last_reauth_us = now;
                if (ls_auth_refresh(rt->cfg.auth)) {
                    fprintf(stderr, "ls-rt: %d short sessions in a row - forcing token refresh, then retry\n",
                            rt->consec_short);
                    rt->consec_short = 0;
                } else {
                    fprintf(stderr, "ls-rt: %d short sessions in a row - token refresh failed, keeping the counter\n",
                            rt->consec_short);
                }
            }
            rt->retry_ms = next_ms;
            rt->next_retry_us = now + (int64_t)rt->retry_ms * 1000;
            if (rt->retry_ms < rt->cfg.reconnect_max_ms) {
                rt->retry_ms *= 2;
            }
        }
        break;
    default:
        break;
    }
    return 0;
}

static const struct lws_protocols protocols[] = {
    {"ls-rt", callback_ls_rt, 0, 0, 0, 0, 0},
    {NULL, NULL, 0, 0, 0, 0, 0},
};

/* ---------- 수명 ---------- */

#ifdef _WIN32
/* lws 4.3.5의 MbedTLS 래퍼는 CA를 넘기지 않으면 신뢰 앵커가 없다.
 * 한 장이라도 파싱에 실패하면 이미 읽은 체인 전체를 버린다 (x509_pm_load).
 * 그래서 Windows ROOT에서 MbedTLS가 받아들이는 인증서만 PEM으로 모은다. */
static bool ls_rt_der_acceptable(const unsigned char *der, size_t der_len) {
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    int rc = mbedtls_x509_crt_parse_der(&crt, der, der_len);
    mbedtls_x509_crt_free(&crt);
    return rc == 0;
}

static char *ls_rt_windows_root_pem(size_t *out_len, int *out_count) {
    *out_len = 0;
    *out_count = 0;
    HCERTSTORE store = CertOpenSystemStoreA(0, "ROOT");
    if (store == 0) {
        return 0;
    }
    char *buf = 0;
    size_t len = 0;
    size_t cap = 0;
    PCCERT_CONTEXT ctx = 0;
    while ((ctx = CertEnumCertificatesInStore(store, ctx)) != 0) {
        if (ctx->pbCertEncoded == 0 || ctx->cbCertEncoded == 0) {
            continue;
        }
        if (!ls_rt_der_acceptable(ctx->pbCertEncoded, ctx->cbCertEncoded)) {
            continue;
        }
        if (!ls_rt_append_pem_cert(&buf, &len, &cap, ctx->pbCertEncoded, ctx->cbCertEncoded)) {
            break;
        }
        (*out_count)++;
    }
    CertCloseStore(store, 0);
    *out_len = len;
    return buf;
}
#endif

tr_ls_rt_t *tr_ls_rt_open(const ls_rt_config_t *cfg, char *errbuf, size_t errlen) {
    if (cfg == 0 || cfg->auth == 0 || cfg->queue_capacity == 0) {
        return 0;
    }
    tr_ls_rt_t *rt = (tr_ls_rt_t *)calloc(1, sizeof(tr_ls_rt_t));
    if (rt == 0) {
        return 0;
    }
    rt->cfg = *cfg;
    rt->state = LS_RT_CONNECTING;
    rt->retry_ms = cfg->reconnect_min_ms > 0 ? cfg->reconnect_min_ms : 1000;
    rt->queue = (ls_rt_event_t *)calloc(cfg->queue_capacity, sizeof(ls_rt_event_t));
    if (rt->queue == 0) {
        free(rt);
        return 0;
    }

    struct lws_context_creation_info info = {0};
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT | LWS_SERVER_OPTION_NO_LWS_SYSTEM_STATES;
    info.user = rt;
#ifdef _WIN32
    size_t ca_len = 0;
    int ca_count = 0;
    char *ca_pem = ls_rt_windows_root_pem(&ca_len, &ca_count);
    if (ca_count > 0 && ca_pem != 0 && ca_len <= 0xffffffffu) {
        fprintf(stderr, "ls-rt: loaded %d Windows root CAs\n", ca_count);
        info.client_ssl_ca_mem = ca_pem;
        info.client_ssl_ca_mem_len = (unsigned int)ca_len;
    } else {
        fprintf(stderr, "ls-rt: failed to read Windows root CAs\n");
    }
#endif
    rt->ctx = lws_create_context(&info);
#ifdef _WIN32
    free(ca_pem);
#endif
    if (rt->ctx == 0) {
        if (errbuf != 0 && errlen > 0) {
            snprintf(errbuf, errlen, "lws_create_context failed");
        }
        free(rt->queue);
        free(rt);
        return 0;
    }
    rt->next_retry_us = 0;
    return rt;
}

void tr_ls_rt_close(tr_ls_rt_t *rt) {
    if (rt == 0) {
        return;
    }
    if (rt->ctx != 0) {
        lws_sul_cancel(&rt->sul_wake);
        lws_context_destroy(rt->ctx);
    }
    free(rt->queue);
    free(rt);
}

bool tr_ls_rt_subscribe(tr_ls_rt_t *rt, const char *tr_cd, const char *tr_key, uint64_t instrument_id) {
    if (rt == 0 || tr_cd == 0 || tr_key == 0 || rt->n_subs >= LS_RT_MAX_SUBS) {
        return false;
    }
    ls_rt_sub_t *s = &rt->subs[rt->n_subs++];
    snprintf(s->tr_cd, sizeof(s->tr_cd), "%.7s", tr_cd);
    /* 채널별 tr_key 고정 길이 패딩 (통합 10자리, 해외선물 8자리 — ls_rt_key_pad_width) */
    int pad = ls_rt_key_pad_width(tr_cd);
    if (pad == 10) {
        snprintf(s->tr_key, sizeof(s->tr_key), "%-10.10s", tr_key);
    } else if (pad == 8) {
        snprintf(s->tr_key, sizeof(s->tr_key), "%-8.8s", tr_key);
    } else {
        snprintf(s->tr_key, sizeof(s->tr_key), "%.15s", tr_key);
    }
    s->instrument_id = instrument_id;
    s->sent = false;
    /* 연결된 상태에서의 사후 구독은 즉시 전송한다 */
    if (rt->state == LS_RT_READY && rt->wsi != 0) {
        send_next_sub(rt);
    }
    return true;
}

bool tr_ls_rt_unsubscribe(tr_ls_rt_t *rt, const char *tr_cd, const char *tr_key) {
    if (rt == 0 || tr_cd == 0 || tr_key == 0) {
        return false;
    }
    int found = -1;
    for (int i = 0; i < rt->n_subs; i++) {
        if (strcmp(rt->subs[i].tr_cd, tr_cd) == 0 && key_equal(rt->subs[i].tr_key, tr_key)) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        return false;
    }
    /* 전송은 READY 상태일 때만. 목록에서도 제거해 재연결 시 복원되지 않게 한다.
     * 해지 키는 등록 시와 같은 형태(패딩 포함)여야 하므로 저장된 키를 그대로 본낸다 */
    if (rt->state == LS_RT_READY && rt->wsi != 0) {
        send_tr(rt, "4", tr_cd, rt->subs[found].tr_key);
    }
    for (int i = found; i < rt->n_subs - 1; i++) {
        rt->subs[i] = rt->subs[i + 1];
    }
    rt->n_subs--;
    if (rt->sub_sent_idx > rt->n_subs) {
        rt->sub_sent_idx = rt->n_subs;
    }
    return true;
}

static void start_connect(tr_ls_rt_t *rt) {
    const char *url = rt->cfg.url != 0 ? rt->cfg.url : LS_RT_DEFAULT_URL;
    char address[128], path[128];
    int port = 0;
    const char *p = url;
    if (strncmp(p, "wss://", 6) == 0) {
        p += 6;
        port = 443;
    } else if (strncmp(p, "ws://", 5) == 0) {
        p += 5;
        port = 80;
    }
    const char *slash = strchr(p, '/');
    size_t host_len = slash != 0 ? (size_t)(slash - p) : strlen(p);
    if (host_len >= sizeof(address)) {
        rt->state = LS_RT_FAILED;
        return;
    }
    memcpy(address, p, host_len);
    address[host_len] = 0;
    /* 포트 명시가 있으면 분리 */
    char *colon = strchr(address, ':');
    if (colon != 0) {
        *colon = 0;
        port = atoi(colon + 1);
    }
    snprintf(path, sizeof(path), "%s", slash != 0 ? slash : "/");

    struct lws_client_connect_info cc = {0};
    cc.context = rt->ctx;
    cc.address = address;
    cc.port = port;
    cc.path = path;
    cc.host = address;
    cc.origin = address;
    cc.protocol = protocols[0].name;
    cc.ssl_connection = (port == 443 || strncmp(url, "wss://", 6) == 0) ? LCCSCF_USE_SSL : 0;
    cc.userdata = rt;

    rt->state = LS_RT_CONNECTING;
    rt->wsi = lws_client_connect_via_info(&cc);
    if (rt->wsi == 0) {
        rt->state = LS_RT_RECONNECTING;
        rt->next_retry_us = now_us(rt) + (int64_t)rt->retry_ms * 1000;
    }
}

static void sul_wake_cb(lws_sorted_usec_list_t *sul) {
    (void)sul; /* 깨우는 것 자체가 목적 — 콜백 본문 없음 */
}

/* lws_service 호출 전에 timeout 시점의 wake sul을 건다 (timeout 인자 무시 우회) */
static int service_bounded(tr_ls_rt_t *rt, int timeout_ms) {
    lws_sul_schedule(rt->ctx, 0, &rt->sul_wake, sul_wake_cb,
                     (lws_usec_t)(timeout_ms < 0 ? 0 : timeout_ms) * LWS_US_PER_MS);
    return lws_service(rt->ctx, timeout_ms);
}

int tr_ls_rt_service(tr_ls_rt_t *rt, int timeout_ms) {
    if (rt == 0) {
        return -1;
    }
    if (rt->wsi == 0 && rt->state != LS_RT_FAILED) {
        if (rt->state == LS_RT_RECONNECTING && now_us(rt) < rt->next_retry_us) {
            service_bounded(rt, timeout_ms < 50 ? timeout_ms : 50);
            return 0;
        }
        if (rt->state == LS_RT_CONNECTING || rt->state == LS_RT_RECONNECTING) {
            start_connect(rt);
        }
    }
    int rc = service_bounded(rt, timeout_ms);
    if (rc < 0) {
        rt->state = LS_RT_FAILED;
    }
    return rc;
}

bool tr_ls_rt_next(tr_ls_rt_t *rt, ls_rt_event_t *out) {
    if (rt == 0 || out == 0 || rt->q_count == 0) {
        return false;
    }
    *out = rt->queue[rt->q_head];
    rt->q_head = (rt->q_head + 1) % rt->cfg.queue_capacity;
    rt->q_count--;
    return true;
}

ls_rt_state_t tr_ls_rt_state(tr_ls_rt_t *rt) {
    return rt != 0 ? rt->state : LS_RT_FAILED;
}

uint64_t tr_ls_rt_queue_dropped(tr_ls_rt_t *rt) {
    return rt != 0 ? rt->q_dropped : 0;
}

uint64_t tr_ls_rt_reconnect_count(tr_ls_rt_t *rt) {
    return rt != 0 ? rt->reconnects : 0;
}
