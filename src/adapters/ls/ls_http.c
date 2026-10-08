#include "adapters/ls/ls_http.h"

#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yyjson.h"

static char g_rest_base[160] = "https://openapi.ls-sec.co.kr:8080";
static char g_ws_url[160] = "wss://openapi.ls-sec.co.kr:9443/websocket";

static void copy_endpoint(char *dst, size_t n, const char *src) {
    snprintf(dst, n, "%s", src);
    size_t len = strlen(dst);
    while (len > 0 && dst[len - 1] == '/') {
        dst[--len] = '\0';
    }
}

void ls_endpoints_set(const char *rest_base, const char *ws_url) {
    if (rest_base != 0 && rest_base[0] != '\0') {
        copy_endpoint(g_rest_base, sizeof(g_rest_base), rest_base);
    }
    if (ws_url != 0 && ws_url[0] != '\0') {
        copy_endpoint(g_ws_url, sizeof(g_ws_url), ws_url);
    }
}

const char *ls_rest_base(void) {
    return g_rest_base;
}

const char *ls_ws_url(void) {
    return g_ws_url;
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    ls_buf_t *buf = (ls_buf_t *)userdata;
    size_t add = size * nmemb;
    char *grown = (char *)realloc(buf->data, buf->len + add + 1);
    if (grown == 0) {
        return 0;
    }
    buf->data = grown;
    memcpy(buf->data + buf->len, ptr, add);
    buf->len += add;
    buf->data[buf->len] = 0;
    return add;
}

ls_http_rc_t ls_http_post(const ls_http_req_t *req, ls_http_resp_t *resp) {
    memset(resp, 0, sizeof(*resp));
    if (req == 0 || req->url == 0 || req->body_json == 0) {
        resp->rc = LS_HTTP_PARSE_ERR;
        return resp->rc;
    }
    CURL *curl = curl_easy_init();
    if (curl == 0) {
        resp->rc = LS_HTTP_TRANSPORT_ERR;
        snprintf(resp->err_detail, sizeof(resp->err_detail), "curl_easy_init failed");
        return resp->rc;
    }

    struct curl_slist *headers = 0;
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
    char auth[600];
    if (req->token != 0 && req->token[0] != 0) {
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", req->token);
        headers = curl_slist_append(headers, auth);
    }
    char trcd[32];
    if (req->tr_cd != 0) {
        snprintf(trcd, sizeof(trcd), "tr_cd: %s", req->tr_cd);
        headers = curl_slist_append(headers, trcd);
    }
    if (req->tr_cont != 0) {
        char tc[16];
        snprintf(tc, sizeof(tc), "tr_cont: %s", req->tr_cont);
        headers = curl_slist_append(headers, tc);
    }
    if (req->tr_cont_key != 0 && req->tr_cont_key[0] != 0) {
        char tck[64];
        snprintf(tck, sizeof(tck), "tr_cont_key: %s", req->tr_cont_key);
        headers = curl_slist_append(headers, tck);
    }

    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = 0;
    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body_json);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, req->timeout_ms > 0 ? req->timeout_ms : 10000L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp->body);

    CURLcode cc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->http_status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (cc != CURLE_OK) {
        resp->rc = LS_HTTP_TRANSPORT_ERR;
        snprintf(resp->err_detail, sizeof(resp->err_detail), "%.120s", errbuf[0] ? errbuf : curl_easy_strerror(cc));
        return resp->rc;
    }
    if (resp->http_status != 200) {
        resp->rc = LS_HTTP_STATUS_ERR;
        snprintf(resp->err_detail, sizeof(resp->err_detail), "HTTP %ld", resp->http_status);
        return resp->rc;
    }

    /* rsp_cd 분류: 성공("00000") 외는 API 오류. JSON이 아니거나 필드가 없으면 PARSE 오류 */
    yyjson_doc *doc = yyjson_read(resp->body.data, resp->body.len, 0);
    if (doc == 0) {
        resp->rc = LS_HTTP_PARSE_ERR;
        snprintf(resp->err_detail, sizeof(resp->err_detail), "response is not JSON");
        return resp->rc;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *cd = yyjson_obj_get(root, "rsp_cd");
    yyjson_val *msg = yyjson_obj_get(root, "rsp_msg");
    if (!yyjson_is_str(cd)) {
        yyjson_doc_free(doc);
        resp->rc = LS_HTTP_PARSE_ERR;
        snprintf(resp->err_detail, sizeof(resp->err_detail), "no rsp_cd in response");
        return resp->rc;
    }
    snprintf(resp->rsp_cd, sizeof(resp->rsp_cd), "%.7s", yyjson_get_str(cd));
    if (yyjson_is_str(msg)) {
        snprintf(resp->rsp_msg, sizeof(resp->rsp_msg), "%.127s", yyjson_get_str(msg));
    }
    yyjson_doc_free(doc);

    if (strcmp(resp->rsp_cd, "00000") != 0) {
        resp->rc = LS_HTTP_API_ERR;
        return resp->rc;
    }
    resp->rc = LS_HTTP_OK;
    return resp->rc;
}

ls_http_rc_t ls_http_post_form(const char *url, const char *form_body, long timeout_ms, ls_http_resp_t *resp) {
    memset(resp, 0, sizeof(*resp));
    if (url == 0 || form_body == 0) {
        resp->rc = LS_HTTP_PARSE_ERR;
        return resp->rc;
    }
    CURL *curl = curl_easy_init();
    if (curl == 0) {
        resp->rc = LS_HTTP_TRANSPORT_ERR;
        return resp->rc;
    }
    struct curl_slist *headers = 0;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = 0;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms > 0 ? timeout_ms : 10000L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp->body);
    CURLcode cc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->http_status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (cc != CURLE_OK) {
        resp->rc = LS_HTTP_TRANSPORT_ERR;
        snprintf(resp->err_detail, sizeof(resp->err_detail), "%.120s", errbuf[0] ? errbuf : curl_easy_strerror(cc));
        return resp->rc;
    }
    if (resp->http_status != 200) {
        resp->rc = LS_HTTP_STATUS_ERR;
        snprintf(resp->err_detail, sizeof(resp->err_detail), "HTTP %ld", resp->http_status);
        return resp->rc;
    }
    resp->rc = LS_HTTP_OK;
    return resp->rc;
}

void ls_http_resp_free(ls_http_resp_t *resp) {
    if (resp != 0) {
        free(resp->body.data);
        resp->body.data = 0;
        resp->body.len = 0;
    }
}
