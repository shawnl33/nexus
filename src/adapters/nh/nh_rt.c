#include "adapters/nh/nh_rt.h"

#include <curl/curl.h>
#include <libwebsockets.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/model/civil_time.h"
#include "yyjson.h"

#define NH_RT_SUBS 16
#define NH_RT_QUEUE 256
#define NH_RT_RX 16384

typedef struct {
    char sym[40];
    uint64_t instrument_id;
    bool sent;
} nh_sub_t;

struct nh_rt {
    nh_auth_t *auth;
    struct lws_context *ctx;
    struct lws *wsi;
    char key[128];
    char host[64];
    char path[512];
    bool need_init;
    nh_sub_t subs[NH_RT_SUBS];
    int nsubs;
    nh_rt_tick_t queue[NH_RT_QUEUE];
    int qh;
    int qt;
    char rx[NH_RT_RX];
    size_t rx_len;
    bool ready;
    bool want_connect;
    int64_t next_try_us;
    int closes;
    lws_sorted_usec_list_t sul;
    char err[128];
};

static int64_t now_us(void) {
    return (int64_t)time(0) * 1000000LL;
}

/* 접속 경로에 access_key가 들어 있다. lws 기본 로그가 그 경로를 찍지 않게 한다. */
static void nh_lws_log(int level, const char *line) {
    (void)level;
    if (line == 0 || strstr(line, "access_key") != 0 || strstr(line, "session_token") != 0) {
        return;
    }
    fputs(line, stderr);
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *ud) {
    char **buf = (char **)ud;
    size_t add = size * nmemb;
    size_t old = *buf != 0 ? strlen(*buf) : 0;
    char *grown = (char *)realloc(*buf, old + add + 1);
    if (grown == 0) {
        return 0;
    }
    memcpy(grown + old, ptr, add);
    grown[old + add] = 0;
    *buf = grown;
    return add;
}

static bool fetch_access_key(nh_rt_t *rt) {
    const char *token = 0;
    if (!nh_auth_ensure(rt->auth, &token)) {
        snprintf(rt->err, sizeof(rt->err), "NH token unavailable");
        return false;
    }
    char url[200];
    snprintf(url, sizeof(url), "%s/permission/v1/web-socket-key", rt->auth->base);
    char auth[2200];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
    CURL *curl = curl_easy_init();
    if (curl == 0) {
        return false;
    }
    struct curl_slist *hdr = 0;
    hdr = curl_slist_append(hdr, auth);
    hdr = curl_slist_append(hdr, "Accept: application/json");
    char *body = 0;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    CURLcode cc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(curl);
    memset(auth, 0, sizeof(auth));
    bool ok = false;
    if (cc == CURLE_OK && status == 200 && body != 0) {
        yyjson_doc *doc = yyjson_read(body, strlen(body), 0);
        const char *key = doc != 0 ? yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(doc), "access_key")) : 0;
        if (key != 0 && key[0] != 0) {
            snprintf(rt->key, sizeof(rt->key), "%s", key);
            ok = true;
        }
        if (doc != 0) {
            yyjson_doc_free(doc);
        }
    }
    free(body);
    if (!ok) {
        snprintf(rt->err, sizeof(rt->err), "NH websocket key HTTP %ld", status);
    }
    return ok;
}

static int find_sub(nh_rt_t *rt, const char *sym) {
    for (int i = 0; i < rt->nsubs; i++) {
        if (strcmp(rt->subs[i].sym, sym) == 0) {
            return i;
        }
    }
    return -1;
}

static void enqueue(nh_rt_t *rt, const nh_rt_tick_t *tk) {
    int next = (rt->qt + 1) % NH_RT_QUEUE;
    if (next == rt->qh) {
        rt->qh = (rt->qh + 1) % NH_RT_QUEUE;
    }
    rt->queue[rt->qt] = *tk;
    rt->qt = next;
}

static bool kst_us(const char *date, const char *hhmmss, int64_t *out) {
    if (date == 0 || hhmmss == 0 || strlen(date) < 8 || strlen(hhmmss) < 6) {
        return false;
    }
    tr_civil_t c;
    memset(&c, 0, sizeof(c));
    char y[5] = {date[0], date[1], date[2], date[3], 0};
    char mo[3] = {date[4], date[5], 0};
    char d[3] = {date[6], date[7], 0};
    char hh[3] = {hhmmss[0], hhmmss[1], 0};
    char mm[3] = {hhmmss[2], hhmmss[3], 0};
    char ss[3] = {hhmmss[4], hhmmss[5], 0};
    c.year = atoi(y);
    c.month = (unsigned)atoi(mo);
    c.day = (unsigned)atoi(d);
    c.hour = (unsigned)atoi(hh);
    c.min = (unsigned)atoi(mm);
    c.sec = (unsigned)atoi(ss);
    tr_time_us_t us = 0;
    if (!tr_time_us_from_civil(&c, 540, &us)) {
        return false;
    }
    *out = (int64_t)us;
    return true;
}

static bool read_num(yyjson_val *v, double *out) {
    if (yyjson_is_str(v)) {
        *out = atof(yyjson_get_str(v));
        return true;
    }
    if (yyjson_is_num(v)) {
        *out = yyjson_get_num(v);
        return true;
    }
    return false;
}

static const char *read_text(yyjson_val *v) {
    return yyjson_is_str(v) ? yyjson_get_str(v) : 0;
}

static bool send_text(struct lws *wsi, const char *json);

static void handle_text(nh_rt_t *rt, const char *text, size_t len) {
    yyjson_doc *doc = yyjson_read(text, len, 0);
    if (doc == 0) {
        return;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    const char *code = yyjson_get_str(yyjson_obj_get(root, "code"));
    const char *msg = yyjson_get_str(yyjson_obj_get(root, "msg"));
    if (code != 0 && strcmp(code, "FA") != 0 && strstr(code, "eyJ") == 0) {
        char brief[80];
        snprintf(brief, sizeof(brief), "%s", msg != 0 ? msg : "");
        if (strstr(brief, "eyJ") == 0 && strstr(brief, "access_key") == 0) {
            fprintf(stderr, "NH realtime msg %s %s\n", code, brief);
        }
    }
    if (code != 0 && msg != 0 && strcmp(code, "pingpong") == 0 && strcmp(msg, "pingpong") == 0) {
        if (rt->wsi != 0) {
            send_text(rt->wsi, text);
        }
        yyjson_doc_free(doc);
        return;
    }
    if ((code != 0 && strcmp(code, "INIT") == 0) || (msg != 0 && strstr(msg, "INIT_SUCCESS") != 0)) {
        rt->ready = true;
        rt->closes = 0;
        for (int i = 0; i < rt->nsubs; i++) {
            rt->subs[i].sent = false;
        }
        if (rt->wsi != 0) {
            lws_callback_on_writable(rt->wsi);
        }
        yyjson_doc_free(doc);
        return;
    }
    yyjson_val *header = yyjson_obj_get(root, "header");
    yyjson_val *body = yyjson_obj_get(root, "body");
    const char *action = header != 0 ? yyjson_get_str(yyjson_obj_get(header, "action_code")) : 0;
    if (action == 0) {
        action = code; /* 체결 푸시는 루트 code가 FA인 경우도 있다 */
    }
    if (body != 0 && action != 0 && strcmp(action, "FA") == 0) {
        const char *sym = yyjson_get_str(yyjson_obj_get(body, "sym"));
        if (sym == 0 && header != 0) {
            sym = yyjson_get_str(yyjson_obj_get(header, "action_sym"));
        }
        int idx = sym != 0 ? find_sub(rt, sym) : -1;
        double px = 0.0;
        if (idx >= 0 && read_num(yyjson_obj_get(body, "last_pric"), &px)) {
            nh_rt_tick_t tk;
            memset(&tk, 0, sizeof(tk));
            snprintf(tk.sym, sizeof(tk.sym), "%s", sym);
            tk.instrument_id = rt->subs[idx].instrument_id;
            tk.price_raw = (int64_t)llround(px * 100.0);
            double qty = 0.0;
            if (!read_num(yyjson_obj_get(body, "exec_qty"), &qty) || qty <= 0.0) {
                qty = 1.0;
            }
            tk.qty = (int64_t)llround(qty);
            if (tk.qty <= 0) {
                tk.qty = 1;
            }
            const char *date = read_text(yyjson_obj_get(body, "ko_trd_dt"));
            const char *tm = read_text(yyjson_obj_get(body, "ko_exec_tm"));
            if (!kst_us(date, tm, &tk.event_time_us)) {
                date = read_text(yyjson_obj_get(body, "biz_dt"));
                tm = read_text(yyjson_obj_get(body, "exch_tm"));
                if (!kst_us(date, tm, &tk.event_time_us)) {
                    tk.event_time_us = now_us();
                }
            }
            enqueue(rt, &tk);
        }
    }
    yyjson_doc_free(doc);
}

static bool send_text(struct lws *wsi, const char *json) {
    size_t n = strlen(json);
    unsigned char *buf = (unsigned char *)malloc(LWS_PRE + n);
    if (buf == 0) {
        return false;
    }
    memcpy(buf + LWS_PRE, json, n);
    int wr = lws_write(wsi, buf + LWS_PRE, n, LWS_WRITE_TEXT);
    free(buf);
    return wr >= (int)n;
}

static void send_subs(nh_rt_t *rt) {
    if (!rt->ready || rt->wsi == 0) {
        return;
    }
    const char *token = 0;
    if (!nh_auth_ensure(rt->auth, &token)) {
        return;
    }
    (void)token;
    for (int i = 0; i < rt->nsubs; i++) {
        if (rt->subs[i].sent) {
            continue;
        }
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "{\"header\":{\"api_ver\":\"1\",\"action\":\"A\"},"
                 "\"body\":{\"action_code\":\"FA\",\"action_sym\":\"%s\"}}",
                 rt->subs[i].sym);
        if (send_text(rt->wsi, msg)) {
            rt->subs[i].sent = true;
        }
        for (int j = i + 1; j < rt->nsubs; j++) {
            if (!rt->subs[j].sent) {
                lws_callback_on_writable(rt->wsi);
                break;
            }
        }
        break;
    }
}

static int callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    (void)user;
    nh_rt_t *rt = wsi != 0 ? (nh_rt_t *)lws_context_user(lws_get_context(wsi)) : 0;
    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        if (rt == 0) {
            break;
        }
        rt->ready = false;
        rt->need_init = true;
        rt->rx_len = 0;
        lws_callback_on_writable(wsi);
        break;
    case LWS_CALLBACK_CLIENT_RECEIVE:
        if (rt == 0 || in == 0) {
            break;
        }
        if (rt->rx_len + len >= sizeof(rt->rx)) {
            rt->rx_len = 0;
            break;
        }
        memcpy(rt->rx + rt->rx_len, in, len);
        rt->rx_len += len;
        if (lws_is_final_fragment(wsi)) {
            rt->rx[rt->rx_len] = 0;
            handle_text(rt, rt->rx, rt->rx_len);
            rt->rx_len = 0;
        }
        break;
    case LWS_CALLBACK_CLIENT_WRITEABLE:
        if (rt == 0) {
            break;
        }
        if (rt->need_init) {
            const char *token = 0;
            rt->need_init = false;
            if (nh_auth_ensure(rt->auth, &token)) {
                char msg[2200];
                int n = snprintf(msg, sizeof(msg),
                                 "{\"header\":{\"api_ver\":\"1\",\"action\":\"0\"},"
                                 "\"body\":{\"action_code\":\"session_init\",\"session_token\":\"%s\"}}",
                                 token);
                if (n > 0 && n < (int)sizeof(msg) && send_text(wsi, msg)) {
                    if (rt->closes < 3) {
                        fprintf(stderr, "NH realtime session\n");
                    }
                } else if (rt->closes < 3) {
                    fprintf(stderr, "NH realtime session send failed\n");
                }
                memset(msg, 0, sizeof(msg));
            }
            break;
        }
        send_subs(rt);
        break;
    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
        if (rt != 0 && rt->closes < 3 && in != 0 && len >= 2) {
            const unsigned char *b = (const unsigned char *)in;
            unsigned code_n = ((unsigned)b[0] << 8) | b[1];
            char why[80];
            size_t n = len - 2;
            if (n >= sizeof(why)) {
                n = sizeof(why) - 1;
            }
            memcpy(why, b + 2, n);
            why[n] = 0;
            if (strstr(why, "access_key") == 0 && strstr(why, "eyJ") == 0) {
                fprintf(stderr, "NH realtime peer close %u %s\n", code_n, why);
            } else {
                fprintf(stderr, "NH realtime peer close %u\n", code_n);
            }
        }
        break;
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        if (rt != 0) {
            const char *why = in != 0 ? (const char *)in : "";
            rt->wsi = 0;
            rt->ready = false;
            rt->need_init = false;
            rt->closes++;
            rt->next_try_us = now_us() + (rt->closes < 3 ? 5000000LL : 30000000LL);
            if (rt->closes <= 3) {
                if (strstr(why, "access_key") == 0 && strstr(why, "session_token") == 0) {
                    fprintf(stderr, "NH realtime error: %s\n", why);
                } else {
                    fprintf(stderr, "NH realtime error\n");
                }
            }
        }
        break;
    case LWS_CALLBACK_CLIENT_CLOSED:
        if (rt != 0) {
            rt->wsi = 0;
            rt->ready = false;
            rt->need_init = false;
            rt->closes++;
            rt->next_try_us = now_us() + (rt->closes < 3 ? 5000000LL : 30000000LL);
            if (rt->closes <= 3) {
                fprintf(stderr, "NH realtime closed\n");
            }
        }
        break;
    default:
        break;
    }
    return 0;
}

static const struct lws_protocols k_protocols[] = {
    {"nh-rt", callback, 0, NH_RT_RX, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0},
};

static void copy_host(char *dst, size_t n, const char *base) {
    const char *p = base != 0 ? base : "";
    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    }
    size_t i = 0;
    while (p[i] != 0 && p[i] != '/' && p[i] != ':' && i + 1 < n) {
        dst[i] = p[i];
        i++;
    }
    dst[i] = 0;
    if (dst[0] == 0) {
        snprintf(dst, n, "api.futures.co.kr");
    }
}

static void start_connect(nh_rt_t *rt) {
    if (!fetch_access_key(rt)) {
        rt->next_try_us = now_us() + 5000000LL;
        fprintf(stderr, "NH realtime: %s\n", rt->err);
        return;
    }
    copy_host(rt->host, sizeof(rt->host), rt->auth->base);
    /* access_key에 +, /, = 가 있으면 쿼리가 깨져 서버가 바로 끊는다. */
    snprintf(rt->path, sizeof(rt->path), "/trade/ws-stream?access_key=");
    {
        size_t o = strlen(rt->path);
        for (const unsigned char *p = (const unsigned char *)rt->key; *p != 0 && o + 4 < sizeof(rt->path); p++) {
            if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
                *p == '-' || *p == '_' || *p == '.' || *p == '~') {
                rt->path[o++] = (char)*p;
            } else {
                o += (size_t)snprintf(rt->path + o, sizeof(rt->path) - o, "%%%02X", *p);
            }
        }
        rt->path[o] = 0;
    }
    struct lws_client_connect_info cc;
    memset(&cc, 0, sizeof(cc));
    cc.context = rt->ctx;
    cc.address = rt->host;
    cc.port = 443;
    cc.path = rt->path;
    cc.host = rt->host;
    cc.origin = rt->host;
    /* 원격 서브프로토콜은 비운다. 로컬 콜백만 nh-rt에 묶는다. */
    cc.protocol = 0;
    cc.local_protocol_name = k_protocols[0].name;
    cc.ssl_connection = LCCSCF_USE_SSL;
    cc.userdata = rt;
    rt->wsi = lws_client_connect_via_info(&cc);
    if (rt->wsi == 0) {
        rt->next_try_us = now_us() + 3000000LL;
    }
}

nh_rt_t *nh_rt_open(nh_auth_t *auth, char *err, size_t err_cap) {
    if (auth == 0) {
        return 0;
    }
    nh_rt_t *rt = (nh_rt_t *)calloc(1, sizeof(*rt));
    if (rt == 0) {
        return 0;
    }
    rt->auth = auth;
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = k_protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT | LWS_SERVER_OPTION_NO_LWS_SYSTEM_STATES;
    info.user = rt;
    lws_set_log_level(LLL_ERR | LLL_WARN, nh_lws_log);
    rt->ctx = lws_create_context(&info);
    if (rt->ctx == 0) {
        if (err != 0 && err_cap > 0) {
            snprintf(err, err_cap, "NH realtime context failed");
        }
        free(rt);
        return 0;
    }
    rt->want_connect = true;
    return rt;
}

void nh_rt_close(nh_rt_t *rt) {
    if (rt == 0) {
        return;
    }
    if (rt->ctx != 0) {
        lws_context_destroy(rt->ctx);
    }
    memset(rt->key, 0, sizeof(rt->key));
    memset(rt->path, 0, sizeof(rt->path));
    free(rt);
}

bool nh_rt_subscribe(nh_rt_t *rt, const char *sym, uint64_t instrument_id) {
    if (rt == 0 || sym == 0 || sym[0] == 0) {
        return false;
    }
    int idx = find_sub(rt, sym);
    if (idx >= 0) {
        rt->subs[idx].instrument_id = instrument_id;
        return true;
    }
    if (rt->nsubs >= NH_RT_SUBS) {
        return false;
    }
    nh_sub_t *s = &rt->subs[rt->nsubs++];
    memset(s, 0, sizeof(*s));
    snprintf(s->sym, sizeof(s->sym), "%s", sym);
    s->instrument_id = instrument_id;
    if (rt->ready && rt->wsi != 0) {
        lws_callback_on_writable(rt->wsi);
    }
    return true;
}

bool nh_rt_unsubscribe(nh_rt_t *rt, const char *sym) {
    if (rt == 0 || sym == 0) {
        return false;
    }
    int idx = find_sub(rt, sym);
    if (idx < 0) {
        return false;
    }
    if (rt->ready && rt->wsi != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "{\"header\":{\"api_ver\":\"1\",\"action\":\"D\"},"
                 "\"body\":{\"action_code\":\"FA\",\"action_sym\":\"%s\"}}",
                 rt->subs[idx].sym);
        send_text(rt->wsi, msg);
    }
    for (int i = idx; i < rt->nsubs - 1; i++) {
        rt->subs[i] = rt->subs[i + 1];
    }
    rt->nsubs--;
    return true;
}

static void sul_wake(lws_sorted_usec_list_t *sul);

int nh_rt_service(nh_rt_t *rt, int timeout_ms) {
    if (rt == 0 || rt->ctx == 0) {
        return -1;
    }
    if (rt->wsi == 0 && now_us() >= rt->next_try_us) {
        start_connect(rt);
    }
    /* lws_service의 timeout은 무시되는 빌드가 있다. LS 실시간과 같이 깨움 예약을 건다. */
    int wait = timeout_ms < 0 ? 0 : timeout_ms;
    lws_sul_schedule(rt->ctx, 0, &rt->sul, sul_wake, (lws_usec_t)wait * LWS_US_PER_MS);
    return lws_service(rt->ctx, wait);
}

static void sul_wake(lws_sorted_usec_list_t *sul) {
    (void)sul;
}

bool nh_rt_ready(const nh_rt_t *rt) {
    return rt != 0 && rt->ready;
}

bool nh_rt_next(nh_rt_t *rt, nh_rt_tick_t *out) {
    if (rt == 0 || out == 0 || rt->qh == rt->qt) {
        return false;
    }
    *out = rt->queue[rt->qh];
    rt->qh = (rt->qh + 1) % NH_RT_QUEUE;
    return true;
}
