#ifndef TR_BROKER_PROFILE_H
#define TR_BROKER_PROFILE_H

/* 증권사 접속 프로필. 한 프로필은 브로커 하나와 실전/모의 하나다.
 * 주소와 키 이름만 다르다. 시세를 붙일 수 있는 프로필만 선택할 수 있다.
 */

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *id;          /* "ls-live" */
    const char *label;       /* "LS 실전" */
    const char *broker;      /* "ls" | "nh" */
    const char *env;         /* "live" | "paper" */
    const char *rest_base;   /* 끝 슬래시 없음 */
    const char *ws_url;
    const char *app_key_env; /* 값 자체가 아니라 환경변수 이름 */
    const char *secret_env;
    bool market_data;        /* 이 프로세스가 시세·과거봉을 이 프로필로 받을 수 있는가 */
} tr_broker_profile_t;

size_t tr_broker_profile_count(void);
const tr_broker_profile_t *tr_broker_profile_at(size_t index);
const tr_broker_profile_t *tr_broker_profile_find(const char *id);

/* 키가 둘 다 있고 시세 어댑터가 있는 프로필만 쓸 수 있다. */
bool tr_broker_profile_usable(const tr_broker_profile_t *profile,
                              bool (*key_present)(const char *env_name, void *ctx), void *ctx);

/* saved_id가 아직 쓸 수 있으면 그 프로필.
 * 저장이 없으면 NH 실전, 그것도 없으면 모의, 그것도 없으면 첫 프로필.
 * 없으면 NULL. */
const tr_broker_profile_t *tr_broker_profile_choose(
    const char *saved_id, bool (*key_present)(const char *env_name, void *ctx), void *ctx);

#endif
