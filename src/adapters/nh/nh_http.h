#ifndef NH_HTTP_H
#define NH_HTTP_H

#include <stddef.h>

typedef struct {
    char *data;
    size_t len;
} nh_buf_t;

typedef struct {
    int rc; /* 0 전송 성공. 본문 해석은 호출자 */
    long http_status;
    nh_buf_t body;
    char err[128];
} nh_http_resp_t;

/* JSON POST. token이 있으면 Authorization: Bearer. 토큰은 로그에 넣지 않는다. */
int nh_http_post_json(const char *url, const char *bearer, const char *json, long timeout_ms,
                      nh_http_resp_t *resp);
int nh_http_post_form(const char *url, const char *form, long timeout_ms, nh_http_resp_t *resp);
void nh_http_resp_free(nh_http_resp_t *resp);

#endif
