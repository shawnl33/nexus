/* IPC 클라이언트 테스트: 명령 왕복, 결과 구분(applied/accepted/rejected), 타임아웃, payload 래핑 */

#include "test_util.h"

#include <string.h>

#include "adapters/ipc/ipc.h"
#include "adapters/ipc/ipc_client.h"

#define CMD_EP "inproc://cli-test-cmd"
#define PUB_EP "inproc://cli-test-pub"

static tr_ipc_t *open_engine(void) {
    tr_ipc_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cmd_endpoint = CMD_EP;
    cfg.pub_endpoint = PUB_EP;
    cfg.engine_instance_id = 7;
    cfg.pub_sndhwm = 16;
    tr_ipc_t *ipc = tr_ipc_open(&cfg, 0, 0);
    TR_CHECK(ipc != 0);
    return ipc;
}

typedef struct {
    const char *status;
    const char *error_code;
    char seen_payload[512];
} stub_t;

static void stub_handler(void *ctx, tr_ipc_command_t *cmd) {
    stub_t *h = (stub_t *)ctx;
    snprintf(h->seen_payload, sizeof(h->seen_payload), "%.500s", cmd->msg.payload);
    cmd->status = h->status;
    cmd->error_code = h->error_code;
    cmd->payload_json = "{\"echo\":true}";
}

/* 단일 스레드: client send → engine poll → client recv */
static tr_ipc_call_rc_t drive_call(tr_ipc_t *engine, stub_t *h, tr_ipc_client_t *c,
                                   const char *cmd_id, const char *type, const char *payload,
                                   tr_ipc_msg_t *out) {
    TR_CHECK(tr_ipc_client_send(c, cmd_id, type, payload) == TR_IPC_CALL_ACCEPTED);
    TR_CHECK(tr_ipc_poll(engine, 2000, 1, stub_handler, h) == 1);
    return tr_ipc_client_recv(c, out);
}

static void test_applied_with_payload_wrap(void) {
    tr_ipc_t *engine = open_engine();
    tr_ipc_client_t *c = tr_ipc_client_connect_ctx(tr_ipc_zmq_ctx(engine), CMD_EP, 2000);
    TR_CHECK(c != 0);

    stub_t h = {"applied", "none", {0}};
    tr_ipc_msg_t reply;
    TR_CHECK(drive_call(engine, &h, c, "cmd-1", "strategy.start", "{\"strategy_id\":\"s1\"}", &reply) == TR_IPC_CALL_OK);
    /* 클라이언트가 command_type을 payload의 type 필드로 래핑한다 */
    TR_CHECK(strstr(h.seen_payload, "\"type\":\"strategy.start\"") != 0);
    TR_CHECK(strstr(h.seen_payload, "\"strategy_id\":\"s1\"") != 0);
    TR_CHECK(strcmp(reply.command_id, "cmd-1") == 0);
    TR_CHECK(strstr(reply.payload, "echo") != 0);

    tr_ipc_client_close(c);
    tr_ipc_close(engine);
}

static void test_rejected(void) {
    tr_ipc_t *engine = open_engine();
    tr_ipc_client_t *c = tr_ipc_client_connect_ctx(tr_ipc_zmq_ctx(engine), CMD_EP, 2000);

    stub_t h = {"rejected", "duplicate_command", {0}};
    tr_ipc_msg_t reply;
    TR_CHECK(drive_call(engine, &h, c, "cmd-2", "orders.cancel", 0, &reply) == TR_IPC_CALL_REJECTED);
    TR_CHECK(strcmp(reply.error_code, "duplicate_command") == 0);

    tr_ipc_client_close(c);
    tr_ipc_close(engine);
}

static void test_timeout_without_engine(void) {
    /* 바인드된 엔진 없음: 연결은 지연되고 호출은 타임아웃 */
    tr_ipc_client_t *c = tr_ipc_client_connect("inproc://cli-test-no-server", 100);
    TR_CHECK(c != 0);
    tr_ipc_msg_t reply;
    TR_CHECK(tr_ipc_client_call(c, "cmd-3", "status", 0, &reply) == TR_IPC_CALL_TIMEOUT);
    tr_ipc_client_close(c);
}

static void test_subscribe_status(void) {
    tr_ipc_t *engine = open_engine();
    tr_ipc_client_t *c = tr_ipc_client_connect_ctx(tr_ipc_zmq_ctx(engine), CMD_EP, 2000);
    void *sub = tr_ipc_client_subscribe(c, PUB_EP, "display");
    TR_CHECK(sub != 0);

    tr_ipc_poll(engine, 100, 1, 0, 0); /* slow joiner 방지 */
    TR_CHECK(tr_ipc_publish(engine, "display", 1, 1000, "{\"engine\":\"ok\"}"));

    tr_ipc_msg_t m;
    TR_CHECK(tr_ipc_client_recv_status(sub, &m, 2000) == 1);
    TR_CHECK(m.sequence == 1 && m.engine_instance_id == 7);

    tr_ipc_client_close_sub(c, sub);
    tr_ipc_client_close(c);
    tr_ipc_close(engine);
}

int main(void) {
    test_applied_with_payload_wrap();
    test_rejected();
    test_timeout_without_engine();
    test_subscribe_status();
    TR_TEST_SUMMARY();
}
