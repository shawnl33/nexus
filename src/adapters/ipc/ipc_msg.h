#ifndef TR_IPC_MSG_H
#define TR_IPC_MSG_H

/* C ↔ Node/CLI 공통 메시지 계약 (계획서 §16).
 *
 * - JSON 직렬화. protocol_version 1.
 * - int64(시각·순번·종목 ID 등 JavaScript 안전 정수 범위를 넘을 수 있는 값)는 문자열로 전송한다.
 * - NaN/Infinity는 본지 않는다. 수치 유효성은 별도 필드로 표현한다.
 * - 전체 메시지 상한 1 MiB, payload 상한 64 KiB. 초과는 거절한다.
 * - 알 수 없는 필드는 무시하고, 알 수 없는 protocol_version은 거절한다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TR_IPC_PROTOCOL_VERSION 1u
#define TR_IPC_MSG_MAX_BYTES (1024u * 1024u)
#define TR_IPC_PAYLOAD_MAX (64u * 1024u)
#define TR_IPC_ID_MAX 64

typedef enum {
    TR_IPC_MSG_COMMAND = 0,
    TR_IPC_MSG_COMMAND_RESULT,
    TR_IPC_MSG_STATUS,
    TR_IPC_MSG_SNAPSHOT,
    TR_IPC_MSG_HEARTBEAT
} tr_ipc_msg_type_t;

typedef struct {
    uint32_t protocol_version;
    tr_ipc_msg_type_t type;
    uint64_t engine_instance_id;
    char request_id[TR_IPC_ID_MAX];
    char command_id[TR_IPC_ID_MAX];
    char stream_id[32];
    uint64_t sequence;
    int64_t event_time_us;
    int64_t emitted_at_us;
    uint64_t instrument_id;  /* 없으면 0 */
    uint32_t timeframe;      /* 없으면 0 */
    uint32_t config_version;
    uint32_t generation;
    char status[16];         /* accepted / applied / rejected */
    char error_code[32];
    char payload[TR_IPC_PAYLOAD_MAX]; /* 원본 JSON 텍스트. 없으면 빈 문자열 */
} tr_ipc_msg_t;

/* 인코드. 출력 길이(널 제외) 또는 -1. */
int tr_ipc_msg_encode(const tr_ipc_msg_t *m, char *out, size_t outlen);

/* 디코드. 크기 초과·JSON 오류·버전 불일치·필수 필드 누락 시 false. */
bool tr_ipc_msg_decode(const char *data, size_t len, tr_ipc_msg_t *out);

/* int64 ↔ 문자열 (overflow 시 false). */
bool tr_ipc_i64_to_str(int64_t v, char *out, size_t outlen);
bool tr_ipc_str_to_i64(const char *s, int64_t *out);
bool tr_ipc_u64_to_str(uint64_t v, char *out, size_t outlen);
bool tr_ipc_str_to_u64(const char *s, uint64_t *out);

const char *tr_ipc_msg_type_str(tr_ipc_msg_type_t t);

#endif
