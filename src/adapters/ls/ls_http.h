#ifndef TR_LS_HTTP_H
#define TR_LS_HTTP_H

/* LS 어댑터 HTTP 계층 (계획서 §15, docs/ls_api_mapping.md §1).
 *
 * - libcurl로 POST + JSON 본문을 본낸다. LS 전용 HTTP 코드를 직접 구현하지 않는다 (계획서 §2).
 * - HTTP 상태 / API 본문 오류(rsp_cd) / 전송 오류 / 파싱 오류를 구분한다.
 * - 토큰은 Authorization 헤더로만 전달하고, 오류 메시지에 토큰·키를 포함하지 않는다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    LS_HTTP_OK = 0,
    LS_HTTP_TRANSPORT_ERR, /* DNS/연결/TLS/타임아웃 등 */
    LS_HTTP_STATUS_ERR,    /* HTTP 상태 != 200 */
    LS_HTTP_API_ERR,       /* rsp_cd != "00000" */
    LS_HTTP_PARSE_ERR      /* 응답이 JSON이 아니거나 필수 필드 없음 */
} ls_http_rc_t;

typedef struct {
    char *data; /* 호출자가 ls_http_resp_free로 해제 */
    size_t len;
} ls_buf_t;

typedef struct {
    const char *url;         /* 전체 URL */
    const char *token;       /* Bearer 토큰 (NULL 가능) */
    const char *tr_cd;       /* LS 거래코드 */
    const char *tr_cont;     /* "N" 또는 "Y" */
    const char *tr_cont_key; /* NULL 가능 */
    const char *body_json;   /* POST할 JSON 본문 */
    long timeout_ms;
} ls_http_req_t;

typedef struct {
    ls_http_rc_t rc;
    long http_status;
    ls_buf_t body;
    char rsp_cd[8];
    char rsp_msg[128];
    char err_detail[128]; /* curl 오류 문자열 등 (키·토큰 미포함) */
} ls_http_resp_t;

ls_http_rc_t ls_http_post(const ls_http_req_t *req, ls_http_resp_t *resp);
void ls_http_resp_free(ls_http_resp_t *resp);

/* form-urlencoded POST (토큰 발급용). tr_cd 헤더 없이 본낸다. */
ls_http_rc_t ls_http_post_form(const char *url, const char *form_body, long timeout_ms, ls_http_resp_t *resp);

#endif
