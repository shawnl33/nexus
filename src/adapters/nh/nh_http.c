#include "adapters/nh/nh_http.h"

#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    nh_buf_t *buf = (nh_buf_t *)userdata;
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

static int post(const char *url, const char *bearer, const char *body, const char *content_type,
                long timeout_ms, nh_http_resp_t *resp) {
    memset(resp, 0, sizeof(*resp));
    if (url == 0 || body == 0) {
        resp->rc = -1;
        return -1;
    }
    CURL *curl = curl_easy_init();
    if (curl == 0) {
        resp->rc = -1;
        snprintf(resp->err, sizeof(resp->err), "curl init failed");
        return -1;
    }
    struct curl_slist *headers = 0;
    char ctype[80];
    snprintf(ctype, sizeof(ctype), "Content-Type: %s", content_type);
    headers = curl_slist_append(headers, ctype);
    headers = curl_slist_append(headers, "Accept: application/json");
    char auth[2200];
    if (bearer != 0 && bearer[0] != '\0') {
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", bearer);
        headers = curl_slist_append(headers, auth);
    }
    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = 0;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms > 0 ? timeout_ms : 20000L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp->body);
    CURLcode cc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->http_status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (cc != CURLE_OK) {
        resp->rc = -1;
        snprintf(resp->err, sizeof(resp->err), "%.120s", errbuf[0] ? errbuf : curl_easy_strerror(cc));
        return -1;
    }
    resp->rc = 0;
    return 0;
}

int nh_http_post_json(const char *url, const char *bearer, const char *json, long timeout_ms,
                      nh_http_resp_t *resp) {
    return post(url, bearer, json, "application/json", timeout_ms, resp);
}

int nh_http_post_form(const char *url, const char *form, long timeout_ms, nh_http_resp_t *resp) {
    return post(url, 0, form, "application/x-www-form-urlencoded", timeout_ms, resp);
}

void nh_http_resp_free(nh_http_resp_t *resp) {
    if (resp == 0) {
        return;
    }
    free(resp->body.data);
    resp->body.data = 0;
    resp->body.len = 0;
}
