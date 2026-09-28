#include "adapters/ls/ls_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "adapters/ls/ls_http.h"
#include "core/model/civil_time.h"
#include "yyjson.h"

#define LS_TOKEN_URL "https://openapi.ls-sec.co.kr:8080/oauth2/token"
#define LS_REFRESH_MARGIN_US (300LL * TR_US_PER_SEC) /* 만료 5분 전 갱신 */

static int64_t default_now_us(void) {
    /* 시스템 시계는 인증 수명 관리에만 쓴다 (전략 논리 시간과 무관) */
    return (int64_t)time(0) * TR_US_PER_SEC;
}

bool ls_auth_init(ls_auth_t *a, int64_t (*now_us_fn)(void)) {
    if (a == 0) {
        return false;
    }
    memset(a, 0, sizeof(*a));
    a->now_us_fn = now_us_fn != 0 ? now_us_fn : default_now_us;
    return true;
}

bool ls_auth_keys_present(void) {
    const char *k = getenv("LS_APP_KEY");
    const char *s = getenv("LS_SECRET_KEY");
    return k != 0 && k[0] != 0 && s != 0 && s[0] != 0;
}

static void set_err(ls_auth_t *a, const char *detail) {
    snprintf(a->last_error, sizeof(a->last_error), "%.127s", detail);
}

bool ls_auth_refresh(ls_auth_t *a) {
    const char *appkey = getenv("LS_APP_KEY");
    const char *appsecret = getenv("LS_SECRET_KEY");
    if (appkey == 0 || appkey[0] == 0 || appsecret == 0 || appsecret[0] == 0) {
        set_err(a, "LS_APP_KEY/LS_SECRET_KEY not set");
        return false;
    }

    /* form-urlencoded 본문. 키는 요청에만 쓰이고 기록하지 않는다 */
    char body[1024];
    snprintf(body, sizeof(body),
             "grant_type=client_credentials&appkey=%s&appsecretkey=%s&scope=oob",
             appkey, appsecret);

    ls_http_resp_t resp;
    ls_http_rc_t rc = ls_http_post_form(LS_TOKEN_URL, body, 10000, &resp);
    if (rc != LS_HTTP_OK) {
        set_err(a, resp.err_detail[0] ? resp.err_detail : "token request failed");
        ls_http_resp_free(&resp);
        return false;
    }

    yyjson_doc *doc = yyjson_read(resp.body.data, resp.body.len, 0);
    if (doc == 0) {
        set_err(a, "token response is not JSON");
        ls_http_resp_free(&resp);
        return false;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *tok = yyjson_obj_get(root, "access_token");
    yyjson_val *exp = yyjson_obj_get(root, "expires_in");
    if (!yyjson_is_str(tok) || !yyjson_is_num(exp)) {
        yyjson_doc_free(doc);
        set_err(a, "token response missing fields");
        ls_http_resp_free(&resp);
        return false;
    }
    snprintf(a->token, sizeof(a->token), "%.511s", yyjson_get_str(tok));
    int64_t expires_in_s = yyjson_get_sint(exp);
    a->expires_at_us = a->now_us_fn() + expires_in_s * TR_US_PER_SEC;
    a->has_token = true;
    a->last_error[0] = 0;

    yyjson_doc_free(doc);
    ls_http_resp_free(&resp);
    return true;
}

bool ls_auth_ensure(ls_auth_t *a, const char **token_out) {
    if (a == 0 || token_out == 0) {
        return false;
    }
    if (!a->has_token || a->now_us_fn() >= a->expires_at_us - LS_REFRESH_MARGIN_US) {
        if (!ls_auth_refresh(a)) {
            return false;
        }
    }
    *token_out = a->token;
    return true;
}
