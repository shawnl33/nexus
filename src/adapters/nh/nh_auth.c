#include "adapters/nh/nh_auth.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "adapters/ls/ls_auth.h"
#include "adapters/nh/nh_http.h"
#include "core/model/time_us.h"
#include "yyjson.h"

#define NH_REFRESH_MARGIN_US (300LL * 1000000LL)

static int64_t now_us(void) {
    return (int64_t)time(0) * 1000000LL;
}

bool nh_auth_init(nh_auth_t *a, const char *rest_base, const char *app_env, const char *secret_env) {
    if (a == 0 || rest_base == 0 || app_env == 0 || secret_env == 0) {
        return false;
    }
    memset(a, 0, sizeof(*a));
    snprintf(a->base, sizeof(a->base), "%s", rest_base);
    snprintf(a->app_env, sizeof(a->app_env), "%s", app_env);
    snprintf(a->secret_env, sizeof(a->secret_env), "%s", secret_env);
    return true;
}

bool nh_auth_refresh(nh_auth_t *a) {
    if (a == 0) {
        return false;
    }
    char appkey[256];
    char secret[256];
    if (!ls_auth_copy_env(a->app_env, appkey, sizeof(appkey)) ||
        !ls_auth_copy_env(a->secret_env, secret, sizeof(secret))) {
        snprintf(a->last_error, sizeof(a->last_error), "NH app key not set");
        return false;
    }
    char body[700];
    snprintf(body, sizeof(body), "grant_type=client_credentials&appkey=%s&appsecret=%s", appkey,
             secret);
    memset(appkey, 0, sizeof(appkey));
    memset(secret, 0, sizeof(secret));
    char url[200];
    snprintf(url, sizeof(url), "%s/auth-service/v1/token", a->base);
    nh_http_resp_t resp;
    int posted = nh_http_post_form(url, body, 15000, &resp);
    memset(body, 0, sizeof(body));
    if (posted != 0 || resp.http_status != 200 ||
        resp.body.data == 0) {
        snprintf(a->last_error, sizeof(a->last_error), "NH token HTTP %ld", resp.http_status);
        nh_http_resp_free(&resp);
        return false;
    }
    yyjson_doc *doc = yyjson_read(resp.body.data, resp.body.len, 0);
    nh_http_resp_free(&resp);
    if (doc == 0) {
        snprintf(a->last_error, sizeof(a->last_error), "NH token is not JSON");
        return false;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    const char *tok = yyjson_get_str(yyjson_obj_get(root, "access_token"));
    yyjson_val *exp = yyjson_obj_get(root, "expires_in");
    if (tok == 0 || !yyjson_is_num(exp)) {
        yyjson_doc_free(doc);
        snprintf(a->last_error, sizeof(a->last_error), "NH token missing fields");
        return false;
    }
    snprintf(a->token, sizeof(a->token), "%s", tok);
    a->expires_at_us = now_us() + (int64_t)yyjson_get_sint(exp) * 1000000LL;
    a->has_token = true;
    a->last_error[0] = 0;
    yyjson_doc_free(doc);
    return true;
}

bool nh_auth_ensure(nh_auth_t *a, const char **out_token) {
    if (a == 0 || out_token == 0) {
        return false;
    }
    if (!a->has_token || now_us() >= a->expires_at_us - NH_REFRESH_MARGIN_US) {
        if (!nh_auth_refresh(a)) {
            return false;
        }
    }
    *out_token = a->token;
    return true;
}
