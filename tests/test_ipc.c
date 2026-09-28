/* IPC 테스트 (계획서 §26: 중복 명령, 같은 ID·다른 내용, 순번 공백·재시작, 잘못된 메시지) */

#include "test_util.h"

#include <string.h>
#include <zmq.h>

#include "adapters/ipc/ipc.h"

#define CMD_EP "inproc://test-cmd"
#define PUB_EP "inproc://test-pub"

static tr_ipc_t *open_engine(void) {
    tr_ipc_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cmd_endpoint = CMD_EP;
    cfg.pub_endpoint = PUB_EP;
    cfg.engine_instance_id = 42;
    cfg.pub_sndhwm = 16;
    tr_ipc_t *ipc = tr_ipc_open(&cfg, 0, 0);
    TR_CHECK(ipc != 0);
    return ipc;
}

static void make_cmd_msg(tr_ipc_msg_t *m, const char *req, const char *cmd_id) {
    memset(m, 0, sizeof(*m));
    m->protocol_version = TR_IPC_PROTOCOL_VERSION;
    m->type = TR_IPC_MSG_COMMAND;
    m->engine_instance_id = 1;
    m->sequence = 1;
    snprintf(m->request_id, sizeof(m->request_id), "%s", req);
    snprintf(m->command_id, sizeof(m->command_id), "%s", cmd_id);
}

static int send_command(void *dealer, const char *json) {
    zmq_send(dealer, "", 0, ZMQ_SNDMORE);
    return zmq_send(dealer, json, strlen(json), 0);
}

static bool recv_reply(void *dealer, tr_ipc_msg_t *out) {
    char buf[8192];
    int n = zmq_recv(dealer, buf, 0, 0);       /* empty delimiter */
    if (n < 0) {
        return false;
    }
    n = zmq_recv(dealer, buf, sizeof(buf) - 1, 0);
    if (n < 0) {
        return false;
    }
    buf[n] = 0;
    return tr_ipc_msg_decode(buf, (size_t)n, out);
}

/* ---------- 메시지 계약 단위 테스트 ---------- */

static void test_msg_roundtrip(void) {
    tr_ipc_msg_t m;
    memset(&m, 0, sizeof(m));
    m.protocol_version = TR_IPC_PROTOCOL_VERSION;
    m.type = TR_IPC_MSG_COMMAND;
    m.engine_instance_id = 9000000000000000000ULL; /* JS 안전 정수 범위 밖 */
    m.sequence = 7;
    m.event_time_us = 1700000000123456;
    m.emitted_at_us = 1700000000123457;
    m.instrument_id = 123456789012345ULL;
    m.timeframe = 60;
    m.config_version = 3;
    m.generation = 2;
    snprintf(m.request_id, sizeof(m.request_id), "req-1");
    snprintf(m.command_id, sizeof(m.command_id), "cmd-1");
    snprintf(m.stream_id, sizeof(m.stream_id), "display");
    snprintf(m.status, sizeof(m.status), "applied");
    snprintf(m.error_code, sizeof(m.error_code), "none");
    snprintf(m.payload, sizeof(m.payload), "{\"a\":1,\"b\":[1,2,3]}");

    char buf[8192];
    int n = tr_ipc_msg_encode(&m, buf, sizeof(buf));
    TR_CHECK(n > 0);
    /* int64는 문자열이어야 한다 */
    TR_CHECK(strstr(buf, "\"9000000000000000000\"") != 0);

    tr_ipc_msg_t d;
    TR_CHECK(tr_ipc_msg_decode(buf, (size_t)n, &d));
    TR_CHECK(d.protocol_version == 1 && d.type == TR_IPC_MSG_COMMAND);
    TR_CHECK(d.engine_instance_id == m.engine_instance_id);
    TR_CHECK(d.sequence == 7 && d.event_time_us == m.event_time_us);
    TR_CHECK(d.instrument_id == m.instrument_id);
    TR_CHECK(strcmp(d.request_id, "req-1") == 0);
    TR_CHECK(strcmp(d.status, "applied") == 0);
    TR_CHECK(strstr(d.payload, "\"a\":1") != 0);
}

static void test_msg_rejects(void) {
    tr_ipc_msg_t d;
    /* JSON 오류 */
    TR_CHECK(!tr_ipc_msg_decode("{not json", 9, &d));
    /* 크기 초과 */
    TR_CHECK(!tr_ipc_msg_decode("{}", 2u * 1024u * 1024u, &d));
    /* 버전 불일치 */
    const char *bad_ver = "{\"protocol_version\":99,\"message_type\":\"command\",\"engine_instance_id\":\"1\",\"sequence\":\"1\",\"event_time\":\"0\",\"emitted_at\":\"0\"}";
    TR_CHECK(!tr_ipc_msg_decode(bad_ver, strlen(bad_ver), &d));
    /* 필수 필드 누락 */
    TR_CHECK(!tr_ipc_msg_decode("{\"protocol_version\":1}", 22, &d));
    /* int64 오버플로 문자열 */
    int64_t v;
    TR_CHECK(!tr_ipc_str_to_i64("99999999999999999999999999", &v));
    uint64_t uv;
    TR_CHECK(tr_ipc_str_to_u64("18446744073709551615", &uv));
    TR_CHECK(uv == 18446744073709551615ULL);
}

/* ---------- ROUTER/DEALER 명령 왕복 ---------- */

typedef struct {
    int calls;
    const char *status;
    const char *error_code;
    const char *payload;
} handler_stub_t;

static void stub_handler(void *ctx, tr_ipc_command_t *cmd) {
    handler_stub_t *h = (handler_stub_t *)ctx;
    h->calls++;
    cmd->status = h->status;
    cmd->error_code = h->error_code;
    cmd->payload_json = h->payload;
}

static void test_command_roundtrip(void) {
    tr_ipc_t *ipc = open_engine();
    void *dealer = zmq_socket(tr_ipc_zmq_ctx(ipc), ZMQ_DEALER);
    TR_CHECK(dealer != 0);
    TR_CHECK(zmq_connect(dealer, CMD_EP) == 0);

    handler_stub_t h = {0, "applied", "none", "{\"pong\":true}"};
    tr_ipc_msg_t m;
    make_cmd_msg(&m, "req-42", "cmd-42");
    char buf[4096];
    int n = tr_ipc_msg_encode(&m, buf, sizeof(buf));
    TR_CHECK(n > 0);
    TR_CHECK(send_command(dealer, buf) > 0);
    TR_CHECK(tr_ipc_poll(ipc, 1000, 4, stub_handler, &h) == 1);
    TR_CHECK(h.calls == 1);

    tr_ipc_msg_t reply;
    TR_CHECK(recv_reply(dealer, &reply));
    TR_CHECK(reply.type == TR_IPC_MSG_COMMAND_RESULT);
    TR_CHECK(strcmp(reply.request_id, "req-42") == 0);
    TR_CHECK(strcmp(reply.command_id, "cmd-42") == 0);
    TR_CHECK(strcmp(reply.status, "applied") == 0);
    TR_CHECK(strcmp(reply.error_code, "none") == 0);
    TR_CHECK(strstr(reply.payload, "pong") != 0);

    zmq_close(dealer);
    tr_ipc_close(ipc);
}

static void test_malformed_command_rejected(void) {
    tr_ipc_t *ipc = open_engine();
    void *dealer = zmq_socket(tr_ipc_zmq_ctx(ipc), ZMQ_DEALER);
    zmq_connect(dealer, CMD_EP);

    handler_stub_t h = {0, "applied", "none", 0};
    TR_CHECK(send_command(dealer, "{\"broken\":") > 0);
    TR_CHECK(tr_ipc_poll(ipc, 1000, 4, stub_handler, &h) == 1);
    TR_CHECK(h.calls == 0); /* 핸들러까지 오지 않고 거절 */

    tr_ipc_msg_t reply;
    TR_CHECK(recv_reply(dealer, &reply));
    TR_CHECK(strcmp(reply.status, "rejected") == 0);
    TR_CHECK(strcmp(reply.error_code, "bad_message") == 0);

    zmq_close(dealer);
    tr_ipc_close(ipc);
}

/* ---------- PUB/SUB 상태 스트림 + 순번 추적 ---------- */

static bool recv_status(void *sub, tr_ipc_msg_t *out) {
    char topic[64];
    int n = zmq_recv(sub, topic, sizeof(topic) - 1, 0);
    if (n < 0) {
        return false;
    }
    char buf[8192];
    n = zmq_recv(sub, buf, sizeof(buf) - 1, 0);
    if (n < 0) {
        return false;
    }
    buf[n] = 0;
    return tr_ipc_msg_decode(buf, (size_t)n, out);
}

static void test_pubsub_and_tracker(void) {
    tr_ipc_t *ipc = open_engine();
    void *sub = zmq_socket(tr_ipc_zmq_ctx(ipc), ZMQ_SUB);
    TR_CHECK(zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "display", 7) == 0);
    TR_CHECK(zmq_connect(sub, PUB_EP) == 0);

    /* slow joiner 방지: 구독이 붙을 시간을 준다 */
    tr_ipc_poll(ipc, 100, 1, 0, 0);

    tr_ipc_tracker_t tracker;
    tr_ipc_tracker_init(&tracker);

    TR_CHECK(tr_ipc_publish(ipc, "display", 1, 1000, "{\"v\":1}"));
    TR_CHECK(tr_ipc_publish(ipc, "display", 2, 2000, "{\"v\":2}"));

    tr_ipc_msg_t m;
    TR_CHECK(recv_status(sub, &m));
    TR_CHECK(m.type == TR_IPC_MSG_STATUS && m.sequence == 1);
    TR_CHECK(tr_ipc_tracker_update(&tracker, m.engine_instance_id, m.sequence) == TR_IPC_TRACK_OK);
    TR_CHECK(recv_status(sub, &m));
    TR_CHECK(m.sequence == 2);
    TR_CHECK(tr_ipc_tracker_update(&tracker, m.engine_instance_id, m.sequence) == TR_IPC_TRACK_OK);

    /* 순번 공백: 4가 오면 3이 없으므로 GAP */
    TR_CHECK(tr_ipc_tracker_update(&tracker, 42, 4) == TR_IPC_TRACK_GAP);
    /* 엔진 재시작: 다른 실행 ID → RESTART */
    TR_CHECK(tr_ipc_tracker_update(&tracker, 43, 1) == TR_IPC_TRACK_RESTART);
    /* 재시작 후 정상 연속 */
    TR_CHECK(tr_ipc_tracker_update(&tracker, 43, 2) == TR_IPC_TRACK_OK);

    zmq_close(sub);
    tr_ipc_close(ipc);
}

int main(void) {
    test_msg_roundtrip();
    test_msg_rejects();
    test_command_roundtrip();
    test_malformed_command_rejected();
    test_pubsub_and_tracker();
    TR_TEST_SUMMARY();
}
