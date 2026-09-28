#ifndef TR_IPC_CLIENT_H
#define TR_IPC_CLIENT_H

/* IPC 클라이언트 (CLI/Node 측). DEALER 소켓으로 명령을 본내고 결과를 받는다 (계획서 §16, §17).
 *
 * - 명령 결과는 '수신됨(accepted)', '엔진에 적용됨(applied)', '거절됨(rejected)'을 구분한다.
 * - 같은 command_id와 다른 내용은 엔진 측에서 충돌로 거절한다 (이 클라이언트는 전달만 한다).
 * - 연결 불가·타임아웃·거절을 호출자가 구분할 수 있게 반환 코드를 나눈다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adapters/ipc/ipc_msg.h"

typedef struct tr_ipc_client tr_ipc_client_t;

typedef enum {
    TR_IPC_CALL_OK = 0,        /* applied */
    TR_IPC_CALL_ACCEPTED,      /* accepted (아직 적용 아님) */
    TR_IPC_CALL_REJECTED,      /* 엔진이 거절 */
    TR_IPC_CALL_TIMEOUT,       /* 응답 없음 */
    TR_IPC_CALL_CONN_ERR,      /* 연결/전송 오류 */
    TR_IPC_CALL_BAD_REPLY      /* 잘못된 응답 형식 */
} tr_ipc_call_rc_t;

tr_ipc_client_t *tr_ipc_client_connect(const char *cmd_endpoint, int timeout_ms);

/* 기존 ZeroMQ 컨텍스트를 공유하는 연결 (inproc 테스트용). ctx는 소유하지 않는다. */
tr_ipc_client_t *tr_ipc_client_connect_ctx(void *shared_ctx, const char *cmd_endpoint, int timeout_ms);
void tr_ipc_client_close(tr_ipc_client_t *c);

/* command_type은 payload 안의 "type" 필드로 전달된다. payload_json은 NULL 가능.
 * 응답은 out에 채운다 (payload는 JSON 텍스트). */
tr_ipc_call_rc_t tr_ipc_client_call(tr_ipc_client_t *c,
                                    const char *command_id, const char *command_type,
                                    const char *payload_json, tr_ipc_msg_t *out);

/* call을 송신/수신으로 분리한 형태. 송신 후 엔진 폴 등 다른 작업을 끼울 수 있다. */
tr_ipc_call_rc_t tr_ipc_client_send(tr_ipc_client_t *c,
                                    const char *command_id, const char *command_type,
                                    const char *payload_json);
tr_ipc_call_rc_t tr_ipc_client_recv(tr_ipc_client_t *c, tr_ipc_msg_t *out);

/* 스냅숏/상태 구독 (PUB/SUB). */
void *tr_ipc_client_subscribe(tr_ipc_client_t *c, const char *pub_endpoint, const char *topic);
int tr_ipc_client_recv_status(void *sub, tr_ipc_msg_t *out, int timeout_ms);
void tr_ipc_client_close_sub(tr_ipc_client_t *c, void *sub);

#endif
