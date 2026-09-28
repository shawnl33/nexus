#include "adapters/ipc/ipc_msg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "yyjson.h"

bool tr_ipc_i64_to_str(int64_t v, char *out, size_t outlen) {
    int n = snprintf(out, outlen, "%lld", (long long)v);
    return n > 0 && (size_t)n < outlen;
}

bool tr_ipc_u64_to_str(uint64_t v, char *out, size_t outlen) {
    int n = snprintf(out, outlen, "%llu", (unsigned long long)v);
    return n > 0 && (size_t)n < outlen;
}

static bool parse_digits(const char *s, bool is_signed, int64_t *out_s, uint64_t *out_u) {
    if (s == 0 || *s == 0) {
        return false;
    }
    char *end = 0;
    errno = 0;
    if (is_signed) {
        long long v = strtoll(s, &end, 10);
        if (errno == ERANGE || end == s || *end != 0) {
            return false;
        }
        *out_s = (int64_t)v;
        return true;
    }
    unsigned long long v = strtoull(s, &end, 10);
    if (errno == ERANGE || end == s || *end != 0) {
        return false;
    }
    *out_u = (uint64_t)v;
    return true;
}

bool tr_ipc_str_to_i64(const char *s, int64_t *out) {
    return s != 0 && out != 0 && parse_digits(s, true, out, 0);
}

bool tr_ipc_str_to_u64(const char *s, uint64_t *out) {
    return s != 0 && out != 0 && parse_digits(s, false, 0, out);
}

const char *tr_ipc_msg_type_str(tr_ipc_msg_type_t t) {
    switch (t) {
    case TR_IPC_MSG_COMMAND: return "command";
    case TR_IPC_MSG_COMMAND_RESULT: return "command_result";
    case TR_IPC_MSG_STATUS: return "status";
    case TR_IPC_MSG_SNAPSHOT: return "snapshot";
    case TR_IPC_MSG_HEARTBEAT: return "heartbeat";
    }
    return "unknown";
}

static bool type_from_str(const char *s, tr_ipc_msg_type_t *out) {
    for (int i = 0; i <= (int)TR_IPC_MSG_HEARTBEAT; i++) {
        if (strcmp(s, tr_ipc_msg_type_str((tr_ipc_msg_type_t)i)) == 0) {
            *out = (tr_ipc_msg_type_t)i;
            return true;
        }
    }
    return false;
}

int tr_ipc_msg_encode(const tr_ipc_msg_t *m, char *out, size_t outlen) {
    if (m == 0 || out == 0 || outlen == 0) {
        return -1;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(0);
    if (doc == 0) {
        return -1;
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    char num[24];

    yyjson_mut_obj_add_uint(doc, root, "protocol_version", m->protocol_version);
    yyjson_mut_obj_add_strcpy(doc, root, "message_type", tr_ipc_msg_type_str(m->type));
    tr_ipc_u64_to_str(m->engine_instance_id, num, sizeof(num));
    yyjson_mut_obj_add_strcpy(doc, root, "engine_instance_id", num);
    yyjson_mut_obj_add_strcpy(doc, root, "request_id", m->request_id);
    yyjson_mut_obj_add_strcpy(doc, root, "command_id", m->command_id);
    yyjson_mut_obj_add_strcpy(doc, root, "stream_id", m->stream_id);
    tr_ipc_u64_to_str(m->sequence, num, sizeof(num));
    yyjson_mut_obj_add_strcpy(doc, root, "sequence", num);
    tr_ipc_i64_to_str(m->event_time_us, num, sizeof(num));
    yyjson_mut_obj_add_strcpy(doc, root, "event_time", num);
    tr_ipc_i64_to_str(m->emitted_at_us, num, sizeof(num));
    yyjson_mut_obj_add_strcpy(doc, root, "emitted_at", num);
    tr_ipc_u64_to_str(m->instrument_id, num, sizeof(num));
    yyjson_mut_obj_add_strcpy(doc, root, "instrument_id", num);
    yyjson_mut_obj_add_uint(doc, root, "timeframe", m->timeframe);
    yyjson_mut_obj_add_uint(doc, root, "config_version", m->config_version);
    yyjson_mut_obj_add_uint(doc, root, "generation", m->generation);
    yyjson_mut_obj_add_strcpy(doc, root, "status", m->status);
    yyjson_mut_obj_add_strcpy(doc, root, "error_code", m->error_code);
    if (m->payload[0] != 0) {
        yyjson_doc *payload = yyjson_read(m->payload, strlen(m->payload), 0);
        if (payload != 0) {
            yyjson_mut_val *p = yyjson_val_mut_copy(doc, yyjson_doc_get_root(payload));
            if (p != 0) {
                yyjson_mut_obj_add_val(doc, root, "payload", p);
            }
            yyjson_doc_free(payload);
        } else {
            yyjson_mut_obj_add_strcpy(doc, root, "payload_raw", m->payload);
        }
    }

    size_t len = 0;
    char *json = yyjson_mut_write(doc, 0, &len);
    yyjson_mut_doc_free(doc);
    if (json == 0 || len >= outlen) {
        free(json);
        return -1;
    }
    memcpy(out, json, len);
    out[len] = 0;
    free(json);
    return (int)len;
}

static bool copy_str(char *dst, size_t dstlen, yyjson_val *v) {
    if (!yyjson_is_str(v)) {
        return false;
    }
    const char *s = yyjson_get_str(v);
    size_t n = strlen(s);
    if (n >= dstlen) {
        return false;
    }
    memcpy(dst, s, n + 1);
    return true;
}

static bool copy_str_opt(char *dst, size_t dstlen, yyjson_val *obj, const char *key) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    if (v == 0 || yyjson_is_null(v)) {
        dst[0] = 0;
        return true;
    }
    return copy_str(dst, dstlen, v);
}

static bool u64_from_str_field(uint64_t *out, yyjson_val *obj, const char *key) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    if (!yyjson_is_str(v)) {
        return false;
    }
    return tr_ipc_str_to_u64(yyjson_get_str(v), out);
}

static bool i64_from_str_field(int64_t *out, yyjson_val *obj, const char *key) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    if (!yyjson_is_str(v)) {
        return false;
    }
    return tr_ipc_str_to_i64(yyjson_get_str(v), out);
}

static bool u32_from_field(uint32_t *out, yyjson_val *obj, const char *key) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    if (v == 0 || yyjson_is_null(v)) {
        *out = 0;
        return true;
    }
    if (!yyjson_is_uint(v)) {
        return false;
    }
    *out = (uint32_t)yyjson_get_uint(v);
    return true;
}

bool tr_ipc_msg_decode(const char *data, size_t len, tr_ipc_msg_t *out) {
    if (data == 0 || out == 0 || len == 0 || len > TR_IPC_MSG_MAX_BYTES) {
        return false;
    }
    yyjson_doc *doc = yyjson_read_opts((char *)data, len, YYJSON_READ_NOFLAG, 0, 0);
    if (doc == 0) {
        return false;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return false;
    }

    memset(out, 0, sizeof(*out));
    bool ok = true;

    ok = ok && u32_from_field(&out->protocol_version, root, "protocol_version");
    ok = ok && out->protocol_version == TR_IPC_PROTOCOL_VERSION;

    yyjson_val *type_v = yyjson_obj_get(root, "message_type");
    ok = ok && yyjson_is_str(type_v) && type_from_str(yyjson_get_str(type_v), &out->type);

    ok = ok && u64_from_str_field(&out->engine_instance_id, root, "engine_instance_id");
    ok = ok && u64_from_str_field(&out->sequence, root, "sequence");
    ok = ok && i64_from_str_field(&out->event_time_us, root, "event_time");
    ok = ok && i64_from_str_field(&out->emitted_at_us, root, "emitted_at");

    ok = ok && copy_str_opt(out->request_id, sizeof(out->request_id), root, "request_id");
    ok = ok && copy_str_opt(out->command_id, sizeof(out->command_id), root, "command_id");
    ok = ok && copy_str_opt(out->stream_id, sizeof(out->stream_id), root, "stream_id");

    yyjson_val *inst = yyjson_obj_get(root, "instrument_id");
    if (inst != 0 && !yyjson_is_null(inst)) {
        ok = ok && yyjson_is_str(inst) && tr_ipc_str_to_u64(yyjson_get_str(inst), &out->instrument_id);
    }
    ok = ok && u32_from_field(&out->timeframe, root, "timeframe");
    ok = ok && u32_from_field(&out->config_version, root, "config_version");
    ok = ok && u32_from_field(&out->generation, root, "generation");
    ok = ok && copy_str_opt(out->status, sizeof(out->status), root, "status");
    ok = ok && copy_str_opt(out->error_code, sizeof(out->error_code), root, "error_code");

    out->payload[0] = 0;
    yyjson_val *payload = yyjson_obj_get(root, "payload");
    if (payload != 0 && !yyjson_is_null(payload)) {
        size_t plen = 0;
        char *pjson = yyjson_val_write(payload, 0, &plen);
        if (pjson != 0 && plen < sizeof(out->payload)) {
            memcpy(out->payload, pjson, plen + 1);
        } else if (pjson != 0) {
            ok = false; /* payload 상한 초과 */
        }
        free(pjson);
    } else {
        copy_str_opt(out->payload, sizeof(out->payload), root, "payload_raw");
    }

    yyjson_doc_free(doc);
    return ok;
}
