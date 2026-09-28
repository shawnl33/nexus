#ifndef TR_LS_AUTH_H
#define TR_LS_AUTH_H

/* LS 인증 (계획서 §15, docs/ls_api_mapping.md §2).
 *
 * - 키는 환경변수 LS_APP_KEY / LS_SECRET_KEY에서만 로딩한다. 로그·오류 메시지에 포함하지 않는다.
 * - 토큰 유효기간은 가변이므로 expires_in을 저장해 만료 전에 갱신한다.
 * - 인증정보가 없는 환경에서도 replay/backtest와 단위 검증은 동작해야 한다
 *   (이 모듈은 실패 상태를 보고할 뿐 엔진 전체를 막지 않는다).
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char token[512];
    int64_t expires_at_us;      /* 갱신 시점 계산용 (시스템 시계) */
    int64_t (*now_us_fn)(void); /* 시계 주입 (테스트 가능) */
    bool has_token;
    char last_error[128];
} ls_auth_t;

/* now_us_fn: UTC epoch µs를 돌려주는 함수. NULL이면 낮 기본 시계. */
bool ls_auth_init(ls_auth_t *a, int64_t (*now_us_fn)(void));

/* 유효한 토큰을 out에 복사한다. 만료가 가까우면 갱신한다. 실패 시 false + last_error. */
bool ls_auth_ensure(ls_auth_t *a, const char **token_out);

/* 강제 갱신. */
bool ls_auth_refresh(ls_auth_t *a);

/* 환경변수에 키가 있는지 (키 값 자체는 노출하지 않음). */
bool ls_auth_keys_present(void);

/* .env 백업 경로: LS_APP_KEY/LS_SECRET_KEY가 환경변수에 없으면 KEY=VALUE 파일에서 읽는다.
 * 환경변수가 항상 우선한다. path가 NULL이면 "./.env" (현재 디렉터리 기준).
 * `#` 주석, `export ` 접두사, 값의 따옴표를 허용한다. 읽은 키 수(0~2)를 돌려준다. */
int ls_auth_load_dotenv(const char *path);

#endif
