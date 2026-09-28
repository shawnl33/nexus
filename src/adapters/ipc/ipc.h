#ifndef TR_IPC_H
#define TR_IPC_H

/* IPC 어댑터: ZeroMQ (계획서 §16).
 *
 * - 명령/응답: ROUTER(엔진) ↔ DEALER(CLI/Node).
 * - 표시용 상태 스트림: PUB(엔진) → SUB. 느린 구독자 때문에 메시지가 버려질 수 있으므로
 *   주문의 유일한 기록이나 신뢰성 있는 명령 경로로 사용하지 않는다.
 * - core 루프는 수신자 응답을 무기한 기다리지 않는다: 송신은 비차단, 큐 포화 시
 *   표시용 메시지는 버리고 카운트한다 (조용히 버리지 않고 통계에 남긴다).
 * - 소켓 소유 스레드는 엔진 루프 하나다. 같은 소켓을 여러 스레드에서 공유하지 않는다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adapters/ipc/ipc_msg.h"

typedef struct tr_ipc tr_ipc_t;

typedef struct {
    const char *cmd_endpoint;     /* 예: "tcp://127.0.0.1:5555", "inproc://cmd" */
    const char *pub_endpoint;
    uint64_t engine_instance_id;
    int pub_sndhwm;               /* PUB 송신 상한. 포화 시 최신 표시 메시지를 버림 */
} tr_ipc_config_t;

/* 명령 핸들러. cmd->msg에 파싱된 명령이 들어오고, 핸들러가 status/error_code/payload를 채운다. */
typedef struct {
    tr_ipc_msg_t msg;             /* 파싱된 명령 (payload는 JSON 텍스트) */
    const char *status;           /* "accepted" | "applied" | "rejected" */
    const char *error_code;
    const char *payload_json;     /* NULL 가능 */
} tr_ipc_command_t;

typedef void (*tr_ipc_command_fn)(void *ctx, tr_ipc_command_t *cmd);

tr_ipc_t *tr_ipc_open(const tr_ipc_config_t *cfg, char *errbuf, size_t errlen);
void tr_ipc_close(tr_ipc_t *ipc);

/* 대기 중인 명령을 최대 max_cmds개 처리한다. timeout_ms 동안 기다릴 수 있다. */
int tr_ipc_poll(tr_ipc_t *ipc, int timeout_ms, int max_cmds, tr_ipc_command_fn on_command, void *ctx);

/* 상태 스트림 발행 (비차단). 포화로 버렸으면 false를 반환하고 통계에 기록한다. */
bool tr_ipc_publish(tr_ipc_t *ipc, const char *stream_id, uint64_t sequence,
                    int64_t event_time_us, const char *payload_json);

/* 관측 통계 */
uint64_t tr_ipc_pub_dropped(const tr_ipc_t *ipc);
uint64_t tr_ipc_cmds_processed(const tr_ipc_t *ipc);

/* 테스트용: 같은 inproc 엔드포인트에 접속할 컨텍스트 */
void *tr_ipc_zmq_ctx(tr_ipc_t *ipc);

/* 구독자 측 순번 추적 도우미: 스냅숏 재요청 판단에 사용한다. */
typedef enum {
    TR_IPC_TRACK_OK = 0,
    TR_IPC_TRACK_GAP,     /* 순번 공백: 스냅숏 필요 */
    TR_IPC_TRACK_RESTART  /* 엔진 실행 ID 변경: 스냅숏 재구독 필요 */
} tr_ipc_track_t;

typedef struct {
    uint64_t engine_instance_id;
    uint64_t last_sequence;
    bool initialized;
} tr_ipc_tracker_t;

void tr_ipc_tracker_init(tr_ipc_tracker_t *t);
tr_ipc_track_t tr_ipc_tracker_update(tr_ipc_tracker_t *t, uint64_t engine_instance_id, uint64_t sequence);

#endif
