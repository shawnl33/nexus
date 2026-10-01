#include "adapters/ipc/ipc_client.h"

#include <zmq.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct tr_ipc_client {
    void *ctx;
    bool owns_ctx;
    void *dealer;
    int timeout_ms;
    uint64_t next_seq;
};

tr_ipc_client_t *tr_ipc_client_connect(const char *cmd_endpoint, int timeout_ms) {
    return tr_ipc_client_connect_ctx(0, cmd_endpoint, timeout_ms);
}

tr_ipc_client_t *tr_ipc_client_connect_ctx(void *shared_ctx, const char *cmd_endpoint, int timeout_ms) {
    if (cmd_endpoint == 0) {
        return 0;
    }
    tr_ipc_client_t *c = (tr_ipc_client_t *)calloc(1, sizeof(tr_ipc_client_t));
    if (c == 0) {
        return 0;
    }
    c->ctx = shared_ctx != 0 ? shared_ctx : zmq_ctx_new();
    c->owns_ctx = shared_ctx == 0;
    c->dealer = zmq_socket(c->ctx, ZMQ_DEALER);
    c->timeout_ms = timeout_ms > 0 ? timeout_ms : 3000;
    c->next_seq = 1;
    if (c->ctx == 0 || c->dealer == 0 || zmq_connect(c->dealer, cmd_endpoint) != 0) {
        tr_ipc_client_close(c);
        return 0;
    }
    /* 기본 LINGER(-1)에서는 peer 없이 본낸 메시지가 남아 있으면 close가 무한 블록된다.
     * CLI 클라이언트는 타임아웃 후 미전송분을 버리는 게 맞다 (2026-10-01 status 행 사건). */
    {
        int linger = 0;
        zmq_setsockopt(c->dealer, ZMQ_LINGER, &linger, sizeof(linger));
    }
    return c;
}

void tr_ipc_client_close(tr_ipc_client_t *c) {
    if (c == 0) {
        return;
    }
    if (c->dealer != 0) {
        zmq_close(c->dealer);
    }
    if (c->ctx != 0 && c->owns_ctx) {
        zmq_ctx_term(c->ctx);
    }
    free(c);
}

tr_ipc_call_rc_t tr_ipc_client_send(tr_ipc_client_t *c,
                                    const char *command_id, const char *command_type,
                                    const char *payload_json) {
    if (c == 0 || command_id == 0 || command_type == 0) {
        return TR_IPC_CALL_CONN_ERR;
    }
    tr_ipc_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.protocol_version = TR_IPC_PROTOCOL_VERSION;
    msg.type = TR_IPC_MSG_COMMAND;
    msg.sequence = c->next_seq++;
    snprintf(msg.request_id, sizeof(msg.request_id), "cli-%llu", (unsigned long long)(msg.sequence));
    snprintf(msg.command_id, sizeof(msg.command_id), "%s", command_id);
    if (payload_json != 0) {
        char wrapped[TR_IPC_PAYLOAD_MAX];
        snprintf(wrapped, sizeof(wrapped), "{\"type\":\"%s\",\"data\":%s}", command_type, payload_json);
        snprintf(msg.payload, sizeof(msg.payload), "%s", wrapped);
    } else {
        snprintf(msg.payload, sizeof(msg.payload), "{\"type\":\"%s\"}", command_type);
    }

    static char buf[TR_IPC_MSG_MAX_BYTES];
    int n = tr_ipc_msg_encode(&msg, buf, sizeof(buf));
    if (n <= 0) {
        return TR_IPC_CALL_CONN_ERR;
    }
    if (zmq_send(c->dealer, "", 0, ZMQ_SNDMORE) < 0 || zmq_send(c->dealer, buf, (size_t)n, 0) < 0) {
        return TR_IPC_CALL_CONN_ERR;
    }
    return TR_IPC_CALL_ACCEPTED;
}

tr_ipc_call_rc_t tr_ipc_client_recv(tr_ipc_client_t *c, tr_ipc_msg_t *out) {
    if (c == 0 || out == 0) {
        return TR_IPC_CALL_CONN_ERR;
    }
    static char buf[TR_IPC_MSG_MAX_BYTES];
    zmq_pollitem_t items[] = {{c->dealer, 0, ZMQ_POLLIN, 0}};
    int rc = zmq_poll(items, 1, c->timeout_ms);
    if (rc < 0) {
        return TR_IPC_CALL_CONN_ERR;
    }
    if (rc == 0) {
        return TR_IPC_CALL_TIMEOUT;
    }
    char delim[8];
    if (zmq_recv(c->dealer, delim, sizeof(delim), 0) < 0) {
        return TR_IPC_CALL_CONN_ERR;
    }
    int n = zmq_recv(c->dealer, buf, sizeof(buf) - 1, 0);
    if (n < 0) {
        return TR_IPC_CALL_CONN_ERR;
    }
    if (!tr_ipc_msg_decode(buf, (size_t)n, out)) {
        return TR_IPC_CALL_BAD_REPLY;
    }
    if (strcmp(out->status, "applied") == 0) {
        return TR_IPC_CALL_OK;
    }
    if (strcmp(out->status, "accepted") == 0) {
        return TR_IPC_CALL_ACCEPTED;
    }
    return TR_IPC_CALL_REJECTED;
}

tr_ipc_call_rc_t tr_ipc_client_call(tr_ipc_client_t *c,
                                    const char *command_id, const char *command_type,
                                    const char *payload_json, tr_ipc_msg_t *out) {
    tr_ipc_call_rc_t rc = tr_ipc_client_send(c, command_id, command_type, payload_json);
    if (rc != TR_IPC_CALL_ACCEPTED) {
        return rc;
    }
    return tr_ipc_client_recv(c, out);
}

void *tr_ipc_client_subscribe(tr_ipc_client_t *c, const char *pub_endpoint, const char *topic) {
    if (c == 0 || pub_endpoint == 0) {
        return 0;
    }
    void *sub = zmq_socket(c->ctx, ZMQ_SUB);
    if (sub == 0) {
        return 0;
    }
    const char *t = topic != 0 ? topic : "";
    if (zmq_setsockopt(sub, ZMQ_SUBSCRIBE, t, strlen(t)) != 0 || zmq_connect(sub, pub_endpoint) != 0) {
        zmq_close(sub);
        return 0;
    }
    return sub;
}

int tr_ipc_client_recv_status(void *sub, tr_ipc_msg_t *out, int timeout_ms) {
    if (sub == 0 || out == 0) {
        return -1;
    }
    zmq_pollitem_t items[] = {{sub, 0, ZMQ_POLLIN, 0}};
    int rc = zmq_poll(items, 1, timeout_ms);
    if (rc <= 0) {
        return rc;
    }
    char topic[64];
    if (zmq_recv(sub, topic, sizeof(topic) - 1, 0) < 0) {
        return -1;
    }
    static char buf[TR_IPC_MSG_MAX_BYTES];
    int n = zmq_recv(sub, buf, sizeof(buf) - 1, 0);
    if (n < 0) {
        return -1;
    }
    return tr_ipc_msg_decode(buf, (size_t)n, out) ? 1 : -1;
}

void tr_ipc_client_close_sub(tr_ipc_client_t *c, void *sub) {
    (void)c;
    if (sub != 0) {
        zmq_close(sub);
    }
}
