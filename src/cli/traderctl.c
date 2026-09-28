/* traderctl — 엔진 관리 CLI (계획서 §17).
 *
 * 별도 프로세스로 동작하며 IPC 클라이언트로 엔진에 명령을 본낸다.
 * CLI 종료가 엔진 종료로 이어지지 않는다.
 *
 * 종료 코드: 0 성공(applied/accepted), 2 사용법 오류, 3 연결 오류, 4 타임아웃, 5 엔진 거절.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adapters/ipc/ipc_client.h"

#define TRADERCTL_VERSION "0.1.0"

static void print_usage(const char *prog) {
    printf("traderctl %s — C Trading Engine 관리 CLI\n", TRADERCTL_VERSION);
    printf("\nUsage: %s [options] <command> [args]\n", prog);
    printf("\nOptions:\n");
    printf("  --endpoint EP   엔진 명령 엔드포인트 (기본 tcp://127.0.0.1:5555)\n");
    printf("  --json          결과를 JSON으로 출력\n");
    printf("  --timeout MS    응답 대기 시간 (기본 3000ms)\n");
    printf("  -h, --help      도움말\n");
    printf("  -v, --version   버전\n");
    printf("\nCommands:\n");
    printf("  status                  엔진 모드·연결·복구·제한 상태\n");
    printf("  engine stop             엔진 정상 종료 요청\n");
    printf("  market                  종목·데이터 조회\n");
    printf("  indicator               지표 인스턴스·설정 조회\n");
    printf("  strategy list           전략 목록\n");
    printf("  strategy start <id>     전략 시작\n");
    printf("  strategy stop <id>      전략 중지 (신규 판단 중지. 청산·취소와 결합하지 않음)\n");
    printf("  risk                    리스크 제한 조회\n");
    printf("  orders                  주문 조회\n");
    printf("  orders cancel <id>      주문 취소\n");
    printf("  positions               계좌·포지션 조회\n");
    printf("  shell                   대화형 관리 프롬프트\n");
    printf("\nExit codes: 0 성공, 2 사용법 오류, 3 연결 오류, 4 타임아웃, 5 엔진 거절\n");
}

typedef struct {
    const char *endpoint;
    int timeout_ms;
    bool json;
} opts_t;

typedef struct {
    const char *usage;
    const char *command_type;
    const char *payload_fmt; /* NULL이면 payload 없음. 인자 1개 포맷 */
} cmd_spec_t;

static const cmd_spec_t *find_command(int argc, char **argv, int *consumed, const char **arg_out) {
    static const struct {
        const char *w1, *w2;
        const char *type, *fmt;
        int takes_arg;
    } MAP[] = {
        {"status", 0, "status", 0, 0},
        {"engine", "stop", "engine.stop", 0, 0},
        {"market", 0, "market.instruments", 0, 0},
        {"indicator", 0, "indicator.list", 0, 0},
        {"strategy", "list", "strategy.list", 0, 0},
        {"strategy", "start", "strategy.start", "{\"strategy_id\":\"%s\"}", 1},
        {"strategy", "stop", "strategy.stop", "{\"strategy_id\":\"%s\"}", 1},
        {"risk", 0, "risk.limits", 0, 0},
        {"orders", 0, "orders.list", 0, 0},
        {"orders", "cancel", "orders.cancel", "{\"order_id\":\"%s\"}", 1},
        {"positions", 0, "positions.list", 0, 0},
    };
    *consumed = 0;
    *arg_out = 0;
    if (argc < 1) {
        return 0;
    }
    for (size_t i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++) {
        if (strcmp(argv[0], MAP[i].w1) != 0) {
            continue;
        }
        if (MAP[i].w2 != 0) {
            if (argc < 2 || strcmp(argv[1], MAP[i].w2) != 0) {
                continue;
            }
            *consumed = 2;
        } else {
            *consumed = 1;
        }
        if (MAP[i].takes_arg) {
            if (argc < *consumed + 1) {
                return 0; /* 인자 부족 */
            }
            *arg_out = argv[*consumed];
            (*consumed)++;
        }
        static cmd_spec_t spec;
        spec.usage = MAP[i].w1;
        spec.command_type = MAP[i].type;
        spec.payload_fmt = MAP[i].fmt;
        return &spec;
    }
    return 0;
}

static uint64_t g_cmd_seq = 1;

static int run_command(const opts_t *opts, const cmd_spec_t *spec, const char *arg) {
    char payload[1024];
    const char *payload_json = 0;
    if (spec->payload_fmt != 0) {
        snprintf(payload, sizeof(payload), spec->payload_fmt, arg);
        payload_json = payload;
    }
    char cmd_id[64];
    snprintf(cmd_id, sizeof(cmd_id), "traderctl-%llu", (unsigned long long)g_cmd_seq++);

    tr_ipc_client_t *c = tr_ipc_client_connect(opts->endpoint, opts->timeout_ms);
    if (c == 0) {
        fprintf(stderr, "error: 엔진 연결 실패 (%s)\n", opts->endpoint);
        return 3;
    }
    tr_ipc_msg_t reply;
    tr_ipc_call_rc_t rc = tr_ipc_client_call(c, cmd_id, spec->command_type, payload_json, &reply);
    tr_ipc_client_close(c);

    if (opts->json) {
        if (rc == TR_IPC_CALL_OK || rc == TR_IPC_CALL_ACCEPTED || rc == TR_IPC_CALL_REJECTED) {
            printf("{\"status\":\"%s\",\"error_code\":\"%s\",\"payload\":%s}\n",
                   reply.status, reply.error_code, reply.payload[0] ? reply.payload : "null");
        } else {
            printf("{\"status\":\"error\",\"error_code\":\"%s\"}\n",
                   rc == TR_IPC_CALL_TIMEOUT ? "timeout" : "connection_error");
        }
    } else {
        switch (rc) {
        case TR_IPC_CALL_OK:
        case TR_IPC_CALL_ACCEPTED:
            printf("%s\n", rc == TR_IPC_CALL_OK ? "applied" : "accepted");
            if (reply.payload[0]) {
                printf("%s\n", reply.payload);
            }
            break;
        case TR_IPC_CALL_REJECTED:
            printf("rejected: %s\n", reply.error_code);
            break;
        case TR_IPC_CALL_TIMEOUT:
            fprintf(stderr, "error: 응답 타임아웃\n");
            break;
        default:
            fprintf(stderr, "error: 명령 전송 실패\n");
            break;
        }
    }

    switch (rc) {
    case TR_IPC_CALL_OK:
    case TR_IPC_CALL_ACCEPTED:
        return 0;
    case TR_IPC_CALL_REJECTED:
        return 5;
    case TR_IPC_CALL_TIMEOUT:
        return 4;
    default:
        return 3;
    }
}

static int run_shell(const opts_t *opts) {
    printf("traderctl shell — 'help' 또는 'quit'\n");
    char line[512];
    for (;;) {
        printf("traderctl> ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == 0) {
            break;
        }
        char *argv[8];
        int argc = 0;
        char *tok = strtok(line, " \t\r\n");
        while (tok != 0 && argc < 8) {
            argv[argc++] = tok;
            tok = strtok(0, " \t\r\n");
        }
        if (argc == 0) {
            continue;
        }
        if (strcmp(argv[0], "quit") == 0 || strcmp(argv[0], "exit") == 0) {
            break;
        }
        if (strcmp(argv[0], "help") == 0) {
            print_usage("traderctl");
            continue;
        }
        int consumed = 0;
        const char *arg = 0;
        const cmd_spec_t *spec = find_command(argc, argv, &consumed, &arg);
        if (spec == 0) {
            printf("알 수 없는 명령: %s (help 참조)\n", argv[0]);
            continue;
        }
        run_command(opts, spec, arg);
    }
    return 0;
}

int main(int argc, char **argv) {
    opts_t opts = {"tcp://127.0.0.1:5555", 3000, false};
    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "--endpoint") == 0 && i + 1 < argc) {
            opts.endpoint = argv[++i];
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            opts.timeout_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--json") == 0) {
            opts.json = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("traderctl %s\n", TRADERCTL_VERSION);
            return 0;
        } else {
            break;
        }
    }
    if (i >= argc) {
        print_usage(argv[0]);
        return 2;
    }
    if (strcmp(argv[i], "shell") == 0) {
        return run_shell(&opts);
    }

    int consumed = 0;
    const char *arg = 0;
    const cmd_spec_t *spec = find_command(argc - i, argv + i, &consumed, &arg);
    if (spec == 0) {
        fprintf(stderr, "error: 알 수 없거나 인자가 부족한 명령입니다 (try --help)\n");
        return 2;
    }
    if (i + consumed != argc) {
        fprintf(stderr, "error: 불필요한 인자가 있습니다\n");
        return 2;
    }
    return run_command(&opts, spec, arg);
}
