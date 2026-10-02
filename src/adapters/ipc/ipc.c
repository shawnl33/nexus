#include "adapters/ipc/ipc.h"

#include <zmq.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct tr_ipc {
    void *ctx;
    void *router;
    void *pub;
    uint64_t engine_instance_id;
    uint64_t next_reply_seq;
    uint64_t pub_dropped;
    uint64_t cmds_processed;
};

tr_ipc_t *tr_ipc_open(const tr_ipc_config_t *cfg, char *errbuf, size_t errlen) {
    if (cfg == 0 || cfg->cmd_endpoint == 0 || cfg->pub_endpoint == 0) {
        return 0;
    }
    tr_ipc_t *ipc = (tr_ipc_t *)calloc(1, sizeof(tr_ipc_t));
    if (ipc == 0) {
        return 0;
    }
    ipc->ctx = zmq_ctx_new();
    ipc->router = zmq_socket(ipc->ctx, ZMQ_ROUTER);
    ipc->pub = zmq_socket(ipc->ctx, ZMQ_PUB);
    ipc->engine_instance_id = cfg->engine_instance_id;
    ipc->next_reply_seq = 1;

    int rc = 0;
    if (ipc->router == 0 || ipc->pub == 0) {
        rc = -1;
    }
    if (rc == 0 && cfg->pub_sndhwm > 0) {
        rc = zmq_setsockopt(ipc->pub, ZMQ_SNDHWM, &cfg->pub_sndhwm, sizeof(int));
    }
    int linger = 0;
    if (rc == 0) {
        rc = zmq_setsockopt(ipc->router, ZMQ_LINGER, &linger, sizeof(int));
    }
    if (rc == 0) {
        rc = zmq_setsockopt(ipc->pub, ZMQ_LINGER, &linger, sizeof(int));
    }
    if (rc == 0) {
        rc = zmq_bind(ipc->router, cfg->cmd_endpoint);
    }
    if (rc == 0) {
        rc = zmq_bind(ipc->pub, cfg->pub_endpoint);
    }
    if (rc != 0) {
        if (errbuf != 0 && errlen > 0) {
            snprintf(errbuf, errlen, "zmq: %s", zmq_strerror(zmq_errno()));
        }
        tr_ipc_close(ipc);
        return 0;
    }
    return ipc;
}

void tr_ipc_close(tr_ipc_t *ipc) {
    if (ipc == 0) {
        return;
    }
    if (ipc->router != 0) {
        zmq_close(ipc->router);
    }
    if (ipc->pub != 0) {
        zmq_close(ipc->pub);
    }
    if (ipc->ctx != 0) {
        zmq_ctx_term(ipc->ctx);
    }
    free(ipc);
}

static bool recv_full(void *sock, char *buf, size_t buflen, size_t *out_len) {
    int n = zmq_recv(sock, buf, buflen - 1, 0);
    if (n < 0) {
        return false;
    }
    if ((size_t)n >= buflen - 1) {
        /* 프레임이 버퍼보다 크다: 나머지를 버리고 오류 처리 */
        int64_t more = 0;
        size_t sz = sizeof(more);
        zmq_getsockopt(sock, ZMQ_RCVMORE, &more, &sz);
        while (more) {
            zmq_recv(sock, buf, buflen - 1, 0);
            sz = sizeof(more);
            zmq_getsockopt(sock, ZMQ_RCVMORE, &more, &sz);
        }
        return false;
    }
    buf[n] = 0;
    *out_len = (size_t)n;
    return true;
}

int tr_ipc_poll(tr_ipc_t *ipc, int timeout_ms, int max_cmds, tr_ipc_command_fn on_command, void *ctx) {
    if (ipc == 0) {
        return -1;
    }
    zmq_pollitem_t items[] = {{ipc->router, 0, ZMQ_POLLIN, 0}};
    int rc = zmq_poll(items, 1, timeout_ms);
    if (rc < 0) {
        return -1;
    }
    int processed = 0;
    while (processed < max_cmds) {
        if (processed == 0 && (items[0].revents & ZMQ_POLLIN) == 0) {
            break;
        }
        if (processed > 0) {
            rc = zmq_poll(items, 1, 0);
            if (rc <= 0 || (items[0].revents & ZMQ_POLLIN) == 0) {
                break;
            }
        }

        /* [routing id][empty][body] 3프레임 수신 */
        char idbuf[256];
        size_t idlen = 0;
        if (!recv_full(ipc->router, idbuf, sizeof(idbuf), &idlen)) {
            break;
        }
        char delim[8];
        size_t dlen = 0;
        recv_full(ipc->router, delim, sizeof(delim), &dlen);

        static char body[TR_IPC_MSG_MAX_BYTES];
        size_t blen = 0;
        bool body_ok = recv_full(ipc->router, body, sizeof(body), &blen);

        tr_ipc_msg_t msg;
        tr_ipc_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        bool parsed = body_ok && tr_ipc_msg_decode(body, blen, &msg);

        cmd.msg = msg;
        cmd.status = "rejected";
        cmd.error_code = "bad_message";
        cmd.payload_json = 0;

        if (parsed && on_command != 0) {
            cmd.msg = msg;
            cmd.status = "applied";
            cmd.error_code = "none";
            on_command(ctx, &cmd);
        }

        /* 응답: command_result */
        tr_ipc_msg_t reply;
        memset(&reply, 0, sizeof(reply));
        reply.protocol_version = TR_IPC_PROTOCOL_VERSION;
        reply.type = TR_IPC_MSG_COMMAND_RESULT;
        reply.engine_instance_id = ipc->engine_instance_id;
        reply.sequence = ipc->next_reply_seq++;
        reply.emitted_at_us = 0;
        snprintf(reply.request_id, sizeof(reply.request_id), "%s", cmd.msg.request_id);
        snprintf(reply.command_id, sizeof(reply.command_id), "%s", cmd.msg.command_id);
        snprintf(reply.status, sizeof(reply.status), "%s", cmd.status);
        snprintf(reply.error_code, sizeof(reply.error_code), "%s", cmd.error_code);
        if (cmd.payload_json != 0) {
            size_t plen = strlen(cmd.payload_json);
            if (plen >= sizeof(reply.payload)) {
                /* 잘라 보내면 JSON이 깨지고 클라이언트가 빈 차트로 리셋한다 */
                snprintf(reply.status, sizeof(reply.status), "%s", "rejected");
                snprintf(reply.error_code, sizeof(reply.error_code), "%s", "payload_too_large");
            } else {
                memcpy(reply.payload, cmd.payload_json, plen + 1);
            }
        }

        static char out[TR_IPC_MSG_MAX_BYTES];
        int olen = tr_ipc_msg_encode(&reply, out, sizeof(out));
        if (olen > 0) {
            zmq_send(ipc->router, idbuf, idlen, ZMQ_SNDMORE);
            zmq_send(ipc->router, "", 0, ZMQ_SNDMORE);
            zmq_send(ipc->router, out, (size_t)olen, 0);
        }
        ipc->cmds_processed++;
        processed++;
    }
    return processed;
}

bool tr_ipc_publish(tr_ipc_t *ipc, const char *stream_id, uint64_t sequence,
                    int64_t event_time_us, const char *payload_json) {
    if (ipc == 0 || stream_id == 0) {
        return false;
    }
    tr_ipc_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.protocol_version = TR_IPC_PROTOCOL_VERSION;
    msg.type = TR_IPC_MSG_STATUS;
    msg.engine_instance_id = ipc->engine_instance_id;
    msg.sequence = sequence;
    msg.event_time_us = event_time_us;
    msg.emitted_at_us = event_time_us;
    snprintf(msg.stream_id, sizeof(msg.stream_id), "%s", stream_id);
    if (payload_json != 0) {
        snprintf(msg.payload, sizeof(msg.payload), "%s", payload_json);
    }
    static char out[TR_IPC_MSG_MAX_BYTES];
    int olen = tr_ipc_msg_encode(&msg, out, sizeof(out));
    if (olen <= 0) {
        return false;
    }
    /* 토픽 프레임 + 본문 프레임 */
    int rc = zmq_send(ipc->pub, stream_id, strlen(stream_id), ZMQ_SNDMORE | ZMQ_DONTWAIT);
    if (rc < 0) {
        ipc->pub_dropped++;
        return false;
    }
    rc = zmq_send(ipc->pub, out, (size_t)olen, ZMQ_DONTWAIT);
    if (rc < 0) {
        ipc->pub_dropped++;
        return false;
    }
    return true;
}

uint64_t tr_ipc_pub_dropped(const tr_ipc_t *ipc) {
    return ipc != 0 ? ipc->pub_dropped : 0;
}

uint64_t tr_ipc_cmds_processed(const tr_ipc_t *ipc) {
    return ipc != 0 ? ipc->cmds_processed : 0;
}

void *tr_ipc_zmq_ctx(tr_ipc_t *ipc) {
    return ipc != 0 ? ipc->ctx : 0;
}

void tr_ipc_tracker_init(tr_ipc_tracker_t *t) {
    if (t != 0) {
        memset(t, 0, sizeof(*t));
    }
}

tr_ipc_track_t tr_ipc_tracker_update(tr_ipc_tracker_t *t, uint64_t engine_instance_id, uint64_t sequence) {
    if (t == 0) {
        return TR_IPC_TRACK_RESTART;
    }
    if (!t->initialized) {
        t->initialized = true;
        t->engine_instance_id = engine_instance_id;
        t->last_sequence = sequence;
        return TR_IPC_TRACK_OK;
    }
    if (engine_instance_id != t->engine_instance_id) {
        t->engine_instance_id = engine_instance_id;
        t->last_sequence = sequence;
        return TR_IPC_TRACK_RESTART;
    }
    if (sequence != t->last_sequence + 1) {
        tr_ipc_track_t r = sequence > t->last_sequence + 1 ? TR_IPC_TRACK_GAP : TR_IPC_TRACK_OK;
        if (sequence > t->last_sequence) {
            t->last_sequence = sequence;
        }
        return r;
    }
    t->last_sequence = sequence;
    return TR_IPC_TRACK_OK;
}
