#ifndef NH_AUTH_H
#define NH_AUTH_H

/* NH선물 접근토큰. docs/nh-futures-rest-api.md.
 * 키 값과 토큰은 로그에 남기지 않는다. 주문은 이 모듈이 보내지 않는다. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char base[160];
    char app_env[40];
    char secret_env[40];
    char token[2048];
    int64_t expires_at_us;
    bool has_token;
    char last_error[128];
} nh_auth_t;

bool nh_auth_init(nh_auth_t *a, const char *rest_base, const char *app_env, const char *secret_env);
bool nh_auth_refresh(nh_auth_t *a);
/* 만료 5분 전이면 갱신한다. out은 토큰 포인터. 로그에 찍지 않는다. */
bool nh_auth_ensure(nh_auth_t *a, const char **out_token);

#endif
