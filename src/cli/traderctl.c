/* traderctl — 엔진 관리 CLI (계획서 §17).
 *
 * 별도 프로세스로 동작하며 IPC 클라이언트로 엔진에 명령을 본낸다.
 * CLI 종료가 엔진 종료로 이어지지 않는다.
 *
 * 종료 코드: 0 성공(applied/accepted), 2 사용법 오류, 3 연결 오류, 4 타임아웃, 5 엔진 거절,
 *           6 재기동 실패(정지·기동 불가).
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601 /* QueryFullProcessImageName (pid 신원 확인) 때문에 Vista+ */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <signal.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

#include "adapters/ipc/ipc_client.h"
#include "yyjson.h"

#define TRADERCTL_VERSION "0.1.0"

static void print_usage(const char *prog) {
    printf("traderctl %s — Nexus Trading Engine 관리 CLI\n", TRADERCTL_VERSION);
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
    printf("  up                      이 터미널에서 엔진과 대시보드를 함께 실행\n");
    printf("                          --live SYM | --live-fut SYM | --replay FILE [--replay-delay MS]\n");
    printf("                          --web DIR (기본 ./web). Ctrl+C 한 번에 종료\n");
    printf("                          패키지가 없거나 불러오지 못하면 npm install 을 먼저 한다\n");
    printf("  engine restart          엔진 재기동 (소멸 확인 후 기동)\n");
    printf("                          --symbol X (기본 A016C000) --log PATH (기본 /tmp/engine-lived2.log)\n");
    printf("                          --wait SEC (기본 240) --pidfile PATH (기본 /tmp/trading-engine-<endpoint port>.pid)\n");
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
    printf("\nExit codes: 0 성공, 2 사용법 오류, 3 연결 오류, 4 타임아웃, 5 엔진 거절, 6 재기동 실패\n");
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
        /* 두 단어 명령을 먼저 매칭한다 (단어 1개 접두사 명령보다 우선) */
        {"engine", "stop", "engine.stop", 0, 0},
        {"market", "select", "market.select", "{\"shcode\":\"%s\"}", 1},
        {"market", "watch", "market.watch", "{\"shcode\":\"%s\"}", 1},
        {"market", "unwatch", "market.unwatch", "{\"shcode\":\"%s\"}", 1},
        {"strategy", "list", "strategy.list", 0, 0},
        {"strategy", "start", "strategy.start", "{\"strategy_id\":\"%s\"}", 1},
        {"strategy", "stop", "strategy.stop", "{\"strategy_id\":\"%s\"}", 1},
        {"orders", "cancel", "orders.cancel", "{\"order_id\":\"%s\"}", 1},
        {"status", 0, "status", 0, 0},
        {"market", 0, "market.instruments", 0, 0},
        {"indicator", 0, "indicator.list", 0, 0},
        {"risk", 0, "risk.limits", 0, 0},
        {"orders", 0, "orders.list", 0, 0},
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

/* ---------- engine restart ----------
 * 2026-10-01 stop 타임아웃 시 구 프로세스가 남아 새 엔진이 포트 충돌로 즉사한 사건 때문에
 * 프로세스 소멸을 확인한 뒤에만 기동한다. OS 분기는 이 블록 안의 #ifdef에 모은다. */

#define RESTART_STOP_WAIT_SEC 8 /* 정상/강제 종료 각 단계의 소멸 대기 */
#define RESTART_KILL_WAIT_SEC 3 /* SIGKILL 이후 소멸 대기 */

typedef struct {
    const char *symbol;
    const char *log_path;
    const char *pidfile;
    int wait_sec;
} restart_opts_t;

static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, 0);
#endif
}

static unsigned long now_ms(void) {
#ifdef _WIN32
    return (unsigned long)GetTickCount(); /* 49일 랩은 부호 없는 뺄셈으로 흡수 */
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000UL + (unsigned long)ts.tv_nsec / 1000000UL;
#endif
}

/* status 응답의 pid를 읽는다. 반환: 1 응답+pid, -1 응답은 왔으나 pid 없음(구버전 엔진), 0 응답 없음. */
static int query_status_pid(const char *endpoint, int timeout_ms, const char *cmd_id, long *pid_out) {
    tr_ipc_client_t *c = tr_ipc_client_connect(endpoint, timeout_ms);
    if (c == 0) {
        return 0;
    }
    tr_ipc_msg_t reply;
    tr_ipc_call_rc_t rc = tr_ipc_client_call(c, cmd_id, "status", 0, &reply);
    tr_ipc_client_close(c);
    if (rc != TR_IPC_CALL_OK && rc != TR_IPC_CALL_ACCEPTED) {
        return 0;
    }
    int result = -1;
    yyjson_doc *doc = yyjson_read(reply.payload, strlen(reply.payload), 0);
    if (doc != 0) {
        yyjson_val *pv = yyjson_obj_get(yyjson_doc_get_root(doc), "pid");
        if (yyjson_is_int(pv)) {
            *pid_out = (long)yyjson_get_sint(pv);
            result = 1;
        }
        yyjson_doc_free(doc);
    }
    return result;
}

static bool pid_alive(long pid) {
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
    if (h == NULL) {
        return GetLastError() == ERROR_ACCESS_DENIED; /* 못 열 뿐 존재하는 경우도 살아 있는 것 */
    }
    DWORD code = 0;
    BOOL ok = GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return ok && code == STILL_ACTIVE;
#else
    if (kill((pid_t)pid, 0) == 0) {
        return true;
    }
    return errno != ESRCH; /* EPERM 등은 프로세스가 존재한다는 뜻 */
#endif
}

static bool wait_gone(long pid, int seconds) {
    for (int i = 0; i < seconds * 10; i++) {
        if (!pid_alive(pid)) {
            return true;
        }
        sleep_ms(100);
    }
    return !pid_alive(pid);
}

/* 강제 종료: SIGTERM → 대기 → SIGKILL → 대기 (Windows: TerminateProcess → 대기). 죽었으면 true. */
static bool force_kill(long pid) {
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (h != NULL) {
        TerminateProcess(h, 1);
        CloseHandle(h);
    }
    return wait_gone(pid, RESTART_STOP_WAIT_SEC);
#else
    kill((pid_t)pid, SIGTERM);
    if (wait_gone(pid, RESTART_STOP_WAIT_SEC)) {
        return true;
    }
    kill((pid_t)pid, SIGKILL);
    return wait_gone(pid, RESTART_KILL_WAIT_SEC);
#endif
}

/* pid 파일을 읽는다. 반환: 1 유효한 pid 기록, 0 파일 없음/형식 오류. existed에 파일 존재 여부. */
static int read_pid_file(const char *path, long *pid_out, int *existed) {
    *existed = 0;
    FILE *f = fopen(path, "r");
    if (f == 0) {
        return 0;
    }
    *existed = 1;
    long v = -1;
    int ok = fscanf(f, "%ld", &v) == 1 && v > 0;
    fclose(f);
    if (!ok) {
        return 0;
    }
    *pid_out = v;
    return 1;
}

/* 내용이 기대 pid와 같을 때만 지운다 (그 사이 다른 엔진이 다시 쓴 파일을 지우지 않게) */
static void remove_pid_file_if(const char *path, long pid) {
    long v = -1;
    int existed = 0;
    if (read_pid_file(path, &v, &existed) == 1 && v == pid) {
        remove(path);
    }
}

/* 명령 엔드포인트("tcp://host:port")의 포트. 파싱 실패 시 5555. */
static int cmd_port_of(const char *ep) {
    const char *colon = strrchr(ep, ':');
    if (colon == 0 || colon[1] == '\0') {
        return 5555;
    }
    int p = atoi(colon + 1);
    return p > 0 ? p : 5555;
}

/* cmd endpoint의 port+1을 pub endpoint로 유도한다 (엔진 기본 5555/5556 관례) */
static void derive_pub_endpoint(const char *cmd_ep, char *out, size_t cap) {
    snprintf(out, cap, "%s", cmd_ep);
    char *colon = strrchr(out, ':');
    int port = colon != 0 ? atoi(colon + 1) : 0;
    if (colon == 0 || port <= 0 || port >= 65535) {
        snprintf(out, cap, "tcp://127.0.0.1:5556"); /* 형식이 이상하면 기본 */
        return;
    }
    snprintf(colon + 1, cap - (size_t)(colon + 1 - out), "%d", port + 1);
}

/* traderctl 자기 exe와 같은 디렉터리의 trading-engine 경로 */
static bool engine_path(char *out, size_t cap) {
    char dir[1024];
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, dir, (DWORD)sizeof(dir));
    if (n == 0 || n >= sizeof(dir)) {
        return false;
    }
    while (n > 0 && dir[n - 1] != '\\' && dir[n - 1] != '/') {
        n--;
    }
    dir[n] = '\0';
    return snprintf(out, cap, "%strading-engine.exe", dir) < (int)cap;
#else
    ssize_t n = readlink("/proc/self/exe", dir, sizeof(dir) - 1);
    if (n <= 0 || (size_t)n >= sizeof(dir) - 1) {
        return false;
    }
    dir[n] = '\0';
    while (n > 0 && dir[n - 1] != '/') {
        n--;
    }
    dir[n] = '\0';
    return snprintf(out, cap, "%strading-engine", dir) < (int)cap;
#endif
}

/* stale pid 재사용 방어: pid 파일의 프로세스가 우리 엔진 바이너리와 같은 이미지인지 본다.
 * 반환: 1 일치(신호 가능), 0 불일치(신호 금지), -1 프로세스는 있는데 확인 불가(신호 금지). */
static int pid_is_our_engine(long pid, const char *exe_path) {
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
    if (h == NULL) {
        return pid_alive(pid) ? -1 : 0;
    }
    char img[1100];
    DWORD n = (DWORD)sizeof(img);
    BOOL ok = QueryFullProcessImageNameA(h, 0, img, &n);
    CloseHandle(h);
    if (!ok) {
        return -1;
    }
    return _stricmp(img, exe_path) == 0 ? 1 : 0;
#else
    char linkpath[64];
    snprintf(linkpath, sizeof(linkpath), "/proc/%ld/exe", pid);
    char img[1100];
    ssize_t n = readlink(linkpath, img, sizeof(img) - 1);
    if (n <= 0) {
        return pid_alive(pid) ? -1 : 0; /* 막 사라진 것과 확인 불가를 구분 */
    }
    img[n] = '\0';
    return strcmp(img, exe_path) == 0 ? 1 : 0;
#endif
}

#ifdef _WIN32
static HANDLE g_child_proc; /* 기동한 자식의 조기 종료 감지용 */
#endif

static long spawn_engine(const char *exe, const char *symbol, const char *log_path, const char *pidfile,
                         const char *cmd_ep, const char *pub_ep, char *err, size_t errcap) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hlog = CreateFileA(log_path, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hlog == INVALID_HANDLE_VALUE) {
        snprintf(err, errcap, "로그 파일 열기 실패: %s", log_path);
        return -1;
    }
    char cmdline[2000]; /* CreateProcess가 덮어쓸 수 있어 쓰기 가능 배열이어야 한다 */
    snprintf(cmdline, sizeof(cmdline), "\"%s\" --live-fut %s --pidfile \"%s\" --cmd-endpoint %s --pub-endpoint %s",
             exe, symbol, pidfile, cmd_ep, pub_ep);
    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hlog; /* 엔진은 stdin을 읽지 않는다 */
    si.hStdOutput = hlog;
    si.hStdError = hlog;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    BOOL ok = CreateProcessA(exe, cmdline, NULL, NULL, TRUE,
                             DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                             NULL, NULL, &si, &pi);
    CloseHandle(hlog);
    if (!ok) {
        snprintf(err, errcap, "CreateProcess 실패: %lu", (unsigned long)GetLastError());
        return -1;
    }
    CloseHandle(pi.hThread);
    g_child_proc = pi.hProcess;
    return (long)pi.dwProcessId;
#else
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(err, errcap, "fork 실패: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        setsid(); /* 부모(터미널)에서 분리 */
        int fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        int nullfd = open("/dev/null", O_RDONLY);
        if (nullfd >= 0) {
            dup2(nullfd, STDIN_FILENO);
            close(nullfd);
        }
        execl(exe, exe, "--live-fut", symbol, "--pidfile", pidfile,
              "--cmd-endpoint", cmd_ep, "--pub-endpoint", pub_ep, (char *)0);
        fprintf(stderr, "exec 실패: %s\n", strerror(errno)); /* stderr는 로그로 리다이렉트됨 */
        _exit(127);
    }
    return (long)pid;
#endif
}

static bool child_exited(long pid) {
#ifdef _WIN32
    (void)pid;
    DWORD code = 0;
    if (g_child_proc == NULL || !GetExitCodeProcess(g_child_proc, &code)) {
        return false;
    }
    return code != STILL_ACTIVE;
#else
    int st = 0;
    return waitpid((pid_t)pid, &st, WNOHANG) == (pid_t)pid;
#endif
}

static void print_log_tail(const char *log_path) {
    FILE *f = fopen(log_path, "rb");
    if (f == 0) {
        return;
    }
    static char buf[4097];
    long from = 0;
    if (fseek(f, 0, SEEK_END) == 0) {
        long size = ftell(f);
        if (size > (long)sizeof(buf) - 1) {
            from = size - ((long)sizeof(buf) - 1);
        }
    }
    fseek(f, from, SEEK_SET);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    /* 마지막 5줄만 보여 준다 */
    int lines = 0;
    char *start = buf + n;
    while (start > buf && lines < 5) {
        start--;
        if (*start == '\n') {
            lines++;
        }
    }
    if (lines == 5) {
        start++; /* 여는 개행 다음부터 */
    }
    fprintf(stderr, "--- %s tail ---\n%s\n", log_path, start);
}

static volatile sig_atomic_t g_up_stop = 0;

#ifndef _WIN32
static void on_up_signal(int sig) {
    (void)sig;
    g_up_stop = 1;
}
#endif

#ifdef _WIN32
static BOOL WINAPI on_up_console(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        g_up_stop = 1;
        return TRUE;
    }
    return FALSE;
}

static HANDLE g_node_proc;
#endif
static long g_node_pid = -1;

static bool file_readable(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == 0) {
        return false;
    }
    fclose(f);
    return true;
}

static void dirname_inplace(char *p) {
    size_t n = strlen(p);
    while (n > 0 && p[n - 1] != '/' && p[n - 1] != '\\') {
        n--;
    }
    if (n > 0) {
        p[n - 1] = '\0';
    }
}

/* 대시보드 디렉터리. --web, 현재 디렉터리의 web/, 실행 파일 두 단계 위의 web/ 순. */
static bool find_web_dir(char *out, size_t cap, const char *override) {
    char js[1220];
    if (override != 0) {
        snprintf(js, sizeof(js), "%s/server.js", override);
        if (!file_readable(js)) {
            return false;
        }
        snprintf(out, cap, "%s", override);
        return true;
    }
    if (file_readable("web/server.js")) {
        snprintf(out, cap, "web");
        return true;
    }
    char exe[1100];
    if (!engine_path(exe, sizeof(exe))) {
        return false;
    }
    dirname_inplace(exe);
    dirname_inplace(exe);
    if (snprintf(js, sizeof(js), "%s/web/server.js", exe) >= (int)sizeof(js)) {
        return false;
    }
    if (!file_readable(js)) {
        return false;
    }
    if (snprintf(out, cap, "%s/web", exe) >= (int)cap) {
        return false;
    }
    return true;
}

/* 터미널에 붙은 자식. 로그를 파일로 빼거나 세션을 분리하지 않는다. */
static long spawn_attached(const char *exe, char *const argv[], const char *cwd,
                           char *err, size_t errcap, bool is_node) {
#ifdef _WIN32
    char cmdline[2000];
    size_t used = 0;
    cmdline[0] = '\0';
    for (int i = 0; argv[i] != 0; i++) {
        int n = snprintf(cmdline + used, sizeof(cmdline) - used, "%s\"%s\"", i ? " " : "", argv[i]);
        if (n < 0 || (size_t)n >= sizeof(cmdline) - used) {
            snprintf(err, errcap, "명령줄이 너무 깁니다");
            return -1;
        }
        used += (size_t)n;
    }
    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    BOOL ok = CreateProcessA(exe, cmdline, NULL, NULL, TRUE, 0, NULL, cwd, &si, &pi);
    if (!ok) {
        snprintf(err, errcap, "CreateProcess 실패: %lu", (unsigned long)GetLastError());
        return -1;
    }
    CloseHandle(pi.hThread);
    if (is_node) {
        g_node_proc = pi.hProcess;
        g_node_pid = (long)pi.dwProcessId;
    } else {
        g_child_proc = pi.hProcess;
    }
    return (long)pi.dwProcessId;
#else
    (void)is_node;
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(err, errcap, "fork 실패: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        if (cwd != 0 && chdir(cwd) != 0) {
            fprintf(stderr, "chdir 실패: %s\n", strerror(errno));
            _exit(127);
        }
        if (exe != 0) {
            execv(exe, argv);
        } else {
            execvp(argv[0], argv);
        }
        fprintf(stderr, "exec 실패: %s\n", strerror(errno));
        _exit(127);
    }
    if (is_node) {
        g_node_pid = (long)pid;
    }
    return (long)pid;
#endif
}

/* 자식이 끝날 때까지 기다린다. g_child_proc(엔진)은 건드리지 않는다.
 * 반환: 종료 코드, 띄우기 실패 -1, Ctrl+C -2. */
static int spawn_and_wait(char *const argv[], const char *cwd, char *err, size_t errcap) {
#ifdef _WIN32
    char cmdline[2000];
    size_t used = 0;
    cmdline[0] = '\0';
    for (int i = 0; argv[i] != 0; i++) {
        int n = snprintf(cmdline + used, sizeof(cmdline) - used, "%s\"%s\"", i ? " " : "", argv[i]);
        if (n < 0 || (size_t)n >= sizeof(cmdline) - used) {
            snprintf(err, errcap, "명령줄이 너무 깁니다");
            return -1;
        }
        used += (size_t)n;
    }
    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, cwd, &si, &pi)) {
        snprintf(err, errcap, "CreateProcess 실패: %lu", (unsigned long)GetLastError());
        return -1;
    }
    CloseHandle(pi.hThread);
    for (;;) {
        if (g_up_stop) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
            return -2;
        }
        if (WaitForSingleObject(pi.hProcess, 200) == WAIT_OBJECT_0) {
            break;
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
#else
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(err, errcap, "fork 실패: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        if (cwd != 0 && chdir(cwd) != 0) {
            fprintf(stderr, "chdir 실패: %s\n", strerror(errno));
            _exit(127);
        }
        execvp(argv[0], argv);
        fprintf(stderr, "exec 실패: %s\n", strerror(errno));
        _exit(127);
    }
    for (;;) {
        if (g_up_stop) {
            kill(pid, SIGTERM);
            waitpid(pid, 0, 0);
            return -2;
        }
        int st = 0;
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) {
            return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
        }
        sleep_ms(200);
    }
#endif
}

/* 0 이면 서버가 의존 패키지를 불러올 수 있다. 1 이면 없거나 깨진 설치. -1 이면 node 가 없다. */
static int web_modules_ok(const char *web) {
    const char *av[] = {
        "node", "--input-type=module", "-e",
        "import 'ws'; import 'zeromq';"
        "import {existsSync} from 'node:fs';"
        "if (!existsSync('node_modules/lightweight-charts/dist/lightweight-charts.standalone.production.js')) process.exit(1);",
        0};
    char err[256] = {0};
    int rc = spawn_and_wait((char *const *)av, web, err, sizeof(err));
    if (rc == -1 || rc == 127) {
        return -1;
    }
    if (rc == -2) {
        return -2;
    }
    return rc == 0 ? 0 : 1;
}

static int web_npm_install(const char *web) {
#ifdef _WIN32
    /* "npm install"을 한 칸으로 묶으면 cmd가 '"npm install' 이라는 프로그램으로 본다.
     * npm은 npm.cmd라 CreateProcess가 직접 실행하지 못하므로 cmd /c 로 나눈다. */
    const char *av[] = {"cmd.exe", "/d", "/c", "npm", "install", 0};
#else
    const char *av[] = {"npm", "install", 0};
#endif
    char err[256] = {0};
    return spawn_and_wait((char *const *)av, web, err, sizeof(err));
}

/* 패키지가 없거나 이 OS에서 불러오지 못하면 npm install 을 한 번 한다. */
static int ensure_web_modules(const char *web) {
    int ok = web_modules_ok(web);
    if (ok == 0) {
        return 0;
    }
    if (ok == -2) {
        return -2;
    }
    if (ok < 0) {
        fprintf(stderr, "error: node 를 찾지 못했습니다. Node.js 20 이상을 설치하세요\n");
        return 6;
    }
    printf("대시보드 패키지가 없거나 불러오지 못합니다. npm install 을 실행합니다\n");
    fflush(stdout);
    int rc = web_npm_install(web);
    if (rc == -2) {
        return -2;
    }
    if (rc != 0) {
        fprintf(stderr, "error: npm install 실패 (종료 %d)\n", rc);
        return 6;
    }
    ok = web_modules_ok(web);
    if (ok == -2) {
        return -2;
    }
    if (ok != 0) {
        fprintf(stderr, "error: npm install 후에도 대시보드 패키지를 불러오지 못합니다\n");
        return 6;
    }
    return 0;
}

/* node 가 listen 하기 전에 브라우저를 열면 실패 화면이 남는다. */
static bool port_open(int port) {
#ifdef _WIN32
    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) {
        return false;
    }
#else
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
#endif
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(0x7F000001);
    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
    return rc == 0;
}

static bool port_open_v6(int port) {
#ifdef _WIN32
    SOCKET fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) {
        return false;
    }
#else
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
#endif
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons((unsigned short)port);
    addr.sin6_addr = in6addr_loopback;
    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
    return rc == 0;
}

static void open_dashboard(const char *url) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
    pid_t p = fork();
    if (p == 0) {
        execlp("xdg-open", "xdg-open", url, (char *)0);
        _exit(127);
    }
#endif
}

static bool node_exited(void) {
    if (g_node_pid < 0) {
        return true;
    }
#ifdef _WIN32
    DWORD code = 0;
    if (g_node_proc == NULL || !GetExitCodeProcess(g_node_proc, &code)) {
        return false;
    }
    return code != STILL_ACTIVE;
#else
    int st = 0;
    pid_t r = waitpid((pid_t)g_node_pid, &st, WNOHANG);
    return r == (pid_t)g_node_pid;
#endif
}

/* 엔진(우리가 띄운 경우)과 대시보드를 이 터미널에서 함께 실행한다. */
static int run_up(const opts_t *opts, int argc, char **argv) {
    const char *mode = 0;
    const char *arg = 0;
    const char *replay_delay = 0;
    const char *web_override = 0;
    for (int j = 0; j < argc; j++) {
        if (strcmp(argv[j], "--live") == 0 && j + 1 < argc) {
            mode = "--live";
            arg = argv[++j];
        } else if (strcmp(argv[j], "--live-fut") == 0 && j + 1 < argc) {
            mode = "--live-fut";
            arg = argv[++j];
        } else if (strcmp(argv[j], "--replay") == 0 && j + 1 < argc) {
            mode = "--replay";
            arg = argv[++j];
        } else if (strcmp(argv[j], "--replay-delay") == 0 && j + 1 < argc) {
            replay_delay = argv[++j];
        } else if (strcmp(argv[j], "--web") == 0 && j + 1 < argc) {
            web_override = argv[++j];
        } else {
            fprintf(stderr, "error: up 인자 오류: %s\n", argv[j]);
            return 2;
        }
    }
    if (mode == 0 || arg == 0 || arg[0] == '\0') {
        fprintf(stderr, "error: up 에는 --live SYM, --live-fut SYM, --replay FILE 중 하나가 필요합니다\n");
        return 2;
    }

    char web[1100];
    if (!find_web_dir(web, sizeof(web), web_override)) {
        fprintf(stderr, "error: 대시보드 디렉터리를 찾지 못했습니다 (web/server.js). 저장소 루트에서 실행하세요\n");
        return 6;
    }

    char exe[1100];
    if (!engine_path(exe, sizeof(exe))) {
        fprintf(stderr, "error: 엔진 경로 확인 실패\n");
        return 6;
    }

    char cmd_id[64];
    snprintf(cmd_id, sizeof(cmd_id), "traderctl-%llu", (unsigned long long)g_cmd_seq++);
    long existing = -1;
    bool started_engine = false;
    long engine_pid = -1;
    int st = query_status_pid(opts->endpoint, opts->timeout_ms, cmd_id, &existing);
    if (st > 0) {
        printf("엔진이 이미 실행 중: pid %ld — 대시보드만 띄웁니다\n", existing);
        fflush(stdout);
    } else {
        if (!file_readable(exe)) {
            fprintf(stderr, "error: 엔진 바이너리 없음: %s\n", exe);
            return 6;
        }
        char pub_ep[256];
        derive_pub_endpoint(opts->endpoint, pub_ep, sizeof(pub_ep));
        const char *av[12];
        int n = 0;
        av[n++] = exe;
        av[n++] = mode;
        av[n++] = arg;
        if (replay_delay != 0) {
            av[n++] = "--replay-delay";
            av[n++] = replay_delay;
        }
        av[n++] = "--cmd-endpoint";
        av[n++] = opts->endpoint;
        av[n++] = "--pub-endpoint";
        av[n++] = pub_ep;
        av[n] = 0;
        char err[256] = {0};
        engine_pid = spawn_attached(exe, (char *const *)av, 0, err, sizeof(err), false);
        if (engine_pid < 0) {
            fprintf(stderr, "error: %s\n", err);
            return 6;
        }
        started_engine = true;
        printf("엔진 기동 중: pid %ld\n", engine_pid);
        fflush(stdout);
        unsigned long start = now_ms();
        for (;;) {
            snprintf(cmd_id, sizeof(cmd_id), "traderctl-%llu", (unsigned long long)g_cmd_seq++);
            long new_pid = -1;
            if (query_status_pid(opts->endpoint, 500, cmd_id, &new_pid) != 0) {
                printf("엔진 준비됨: pid %ld\n", new_pid > 0 ? new_pid : engine_pid);
                fflush(stdout);
                break;
            }
            if (child_exited(engine_pid)) {
                fprintf(stderr, "error: 엔진이 기동 중 종료됨\n");
                return 6;
            }
            if (now_ms() - start >= 60000UL) {
                fprintf(stderr, "error: 60초 내 엔진이 준비되지 않음\n");
                force_kill(engine_pid);
                return 4;
            }
            sleep_ms(500);
        }
    }

#ifndef _WIN32
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_up_signal;
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);
#else
    SetConsoleCtrlHandler(on_up_console, TRUE);
#endif

    int deps = ensure_web_modules(web);
    if (deps != 0) {
        if (started_engine && pid_alive(engine_pid)) {
            force_kill(engine_pid);
        }
        return deps == -2 ? 0 : deps;
    }

    char web_abs[1100];
#ifdef _WIN32
    DWORD wn = GetFullPathNameA(web, (DWORD)sizeof(web_abs), web_abs, NULL);
    if (wn == 0 || wn >= sizeof(web_abs)) {
        snprintf(web_abs, sizeof(web_abs), "%s", web);
    }
    SetEnvironmentVariableA("DASHBOARD_ROOT", web_abs);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    if (web[0] == '/') {
        snprintf(web_abs, sizeof(web_abs), "%s", web);
    } else if (getcwd(web_abs, sizeof(web_abs)) == 0) {
        snprintf(web_abs, sizeof(web_abs), "%s", web);
    } else {
        size_t n = strlen(web_abs);
        snprintf(web_abs + n, sizeof(web_abs) - n, "/%s", web);
    }
    setenv("DASHBOARD_ROOT", web_abs, 1);
#endif

    /* 윈도우 브라우저의 localhost 는 ::1 을 먼저 연다. 그 쪽 8080 은
       Elgato Stream Deck 이 차지하고 404 page not found 를 돌려준다.
       리눅스에서 8080 이 비어 있어도 그 404 는 보이지 않으므로 18080 을 쓴다. */
    int dash_port = 18080;
    if (port_open(dash_port) || port_open_v6(dash_port)) {
        dash_port = 18081;
        fprintf(stderr, "18080은 이미 사용 중입니다. 대시보드는 %d 포트를 사용합니다\n", dash_port);
    }
    char port_text[16];
    snprintf(port_text, sizeof(port_text), "%d", dash_port);
#ifdef _WIN32
    SetEnvironmentVariableA("DASHBOARD_PORT", port_text);
#else
    setenv("DASHBOARD_PORT", port_text, 1);
#endif

    const char *nav[3];
    nav[0] = "node";
    nav[1] = "server.js";
    nav[2] = 0;
    char err[256] = {0};
    long node_pid = spawn_attached(0, (char *const *)nav, web, err, sizeof(err), true);
    if (node_pid < 0) {
        fprintf(stderr, "error: 대시보드 기동 실패: %s\n", err);
        if (started_engine) {
            force_kill(engine_pid);
        }
        return 6;
    }
    char dash_url[64];
    snprintf(dash_url, sizeof(dash_url), "http://127.0.0.1:%d", dash_port);
    printf("대시보드: %s  (Ctrl+C 로 종료)\n", dash_url);
    fflush(stdout);
    bool ready = false;
    unsigned long wait_from = now_ms();
    while (!g_up_stop && !node_exited() && now_ms() - wait_from < 20000UL) {
        if (port_open(dash_port)) {
            ready = true;
            break;
        }
        sleep_ms(100);
    }
    if (ready) {
        open_dashboard(dash_url);
    } else if (!node_exited()) {
        fprintf(stderr, "error: 20초 안에 대시보드 포트가 열리지 않았습니다\n");
    }

    while (!g_up_stop) {
        if (started_engine && child_exited(engine_pid)) {
            fprintf(stderr, "엔진이 종료되었습니다\n");
            break;
        }
        if (node_exited()) {
            fprintf(stderr, "대시보드가 종료되었습니다\n");
            break;
        }
        sleep_ms(200);
    }

    if (!node_exited()) {
#ifdef _WIN32
        if (g_node_proc != NULL) {
            TerminateProcess(g_node_proc, 1);
        }
#else
        kill((pid_t)g_node_pid, SIGTERM);
#endif
    }
    if (started_engine && pid_alive(engine_pid) && !wait_gone(engine_pid, 3)) {
        force_kill(engine_pid);
    }
#ifdef _WIN32
    if (g_node_proc != NULL) {
        CloseHandle(g_node_proc);
        g_node_proc = NULL;
    }
    if (g_child_proc != NULL) {
        CloseHandle(g_child_proc);
        g_child_proc = NULL;
    }
#endif
    return 0;
}

static int run_engine_restart(const opts_t *opts, const restart_opts_t *ro) {
    char cmd_id[64];

    /* traderctl과 같은 디렉터리의 trading-engine — 신원 확인과 기동 양쪽에서 쓴다 */
    char exe[1100];
    if (!engine_path(exe, sizeof(exe))) {
        fprintf(stderr, "error: 엔진 경로 확인 실패\n");
        return 6;
    }

    /* 1) status로 실행 중 엔진의 pid를 얻는다 */
    snprintf(cmd_id, sizeof(cmd_id), "traderctl-%llu", (unsigned long long)g_cmd_seq++);
    long old_pid = -1;
    int st = query_status_pid(opts->endpoint, opts->timeout_ms, cmd_id, &old_pid);
    if (st < 0) {
        /* pid 없는 구 바이너리가 살아 있으면 소멸 확인이 불가 — 이 상태에서 기동하면 포트 충돌 */
        fprintf(stderr, "error: 실행 중 엔진이 pid를 보고하지 않음 (구버전 바이너리) — 수동 정지 후 재시도\n");
        return 6;
    }
    if (st > 0 && old_pid > 0) {
        printf("정지 요청: pid %ld\n", old_pid);
        fflush(stdout);
        /* engine.stop 응답 타임아웃은 흔하므로 결과는 프로세스 소멸로만 판정한다 */
        snprintf(cmd_id, sizeof(cmd_id), "traderctl-%llu", (unsigned long long)g_cmd_seq++);
        tr_ipc_client_t *c = tr_ipc_client_connect(opts->endpoint, opts->timeout_ms);
        if (c != 0) {
            tr_ipc_msg_t reply;
            tr_ipc_client_call(c, cmd_id, "engine.stop", 0, &reply);
            tr_ipc_client_close(c);
        }
        if (wait_gone(old_pid, RESTART_STOP_WAIT_SEC)) {
            printf("정상 정지됨\n");
        } else {
            printf("응답 없음 — 강제 종료: pid %ld\n", old_pid);
            fflush(stdout);
            if (!force_kill(old_pid)) {
                fprintf(stderr, "error: 엔진을 죽일 수 없음: pid %ld\n", old_pid);
                return 6;
            }
            printf("강제 정지됨\n");
        }
        fflush(stdout);
        remove_pid_file_if(ro->pidfile, old_pid); /* 강제 종료된 엔진은 pid 파일을 못 지운다 */
    } else {
        /* status 미응답 — 백필 구간(bind 후 첫 poll 전)이나 replay 모드는 포트를 잡은 채
         * 묵병이므로, pid 파일로 "정말 없음"과 "살아 있지만 미응답"을 구분한다 */
        long fpid = -1;
        int existed = 0;
        if (read_pid_file(ro->pidfile, &fpid, &existed) == 1 && pid_alive(fpid)) {
            if (pid_is_our_engine(fpid, exe) != 1) {
                /* pid 재사용 의심 — 무관 프로세스를 쏘지 않기 위해 신호도 기동도 하지 않는다 */
                fprintf(stderr, "error: pid 파일의 pid %ld가 엔진 바이너리(%s)로 확인되지 않음 — 수동 확인\n",
                        fpid, exe);
                return 6;
            }
            printf("status 미응답이지만 pid 파일의 엔진 생존 — 정지: pid %ld\n", fpid);
            fflush(stdout);
            if (!force_kill(fpid)) {
                fprintf(stderr, "error: 엔진을 죽일 수 없음: pid %ld\n", fpid);
                return 6;
            }
            printf("강제 정지됨\n");
            fflush(stdout);
            remove_pid_file_if(ro->pidfile, fpid);
        } else if (existed) {
            printf("stale pid 파일 삭제: %s\n", ro->pidfile);
            fflush(stdout);
            remove(ro->pidfile);
        } else {
            printf("실행 중인 엔진 없음 — 바로 기동\n");
            fflush(stdout);
        }
    }

    /* 2) 기동 — .env는 엔진이 스스로 읽는다 */
    FILE *probe = fopen(exe, "rb");
    if (probe == 0) {
        fprintf(stderr, "error: 엔진 바이너 없음: %s (cmake --build build 먼저)\n", exe);
        return 6;
    }
    fclose(probe);

    char pub_ep[256];
    derive_pub_endpoint(opts->endpoint, pub_ep, sizeof(pub_ep));
    char err[256] = {0};
    long child = spawn_engine(exe, ro->symbol, ro->log_path, ro->pidfile,
                              opts->endpoint, pub_ep, err, sizeof(err));
    if (child < 0) {
        fprintf(stderr, "error: %s\n", err);
        return 6;
    }
    printf("기동 중: pid %ld (%s, 로그 %s)\n", child, ro->symbol, ro->log_path);
    fflush(stdout);

    /* 3) 준비 확인 — 백필을 끝내고 명령 루프에 들어가야 status가 응답한다 */
    int rc = 0;
    unsigned long start = now_ms();
    unsigned long budget = (unsigned long)ro->wait_sec * 1000UL;
    for (;;) {
        snprintf(cmd_id, sizeof(cmd_id), "traderctl-%llu", (unsigned long long)g_cmd_seq++);
        long new_pid = -1;
        if (query_status_pid(opts->endpoint, 500, cmd_id, &new_pid) != 0) {
            printf("기동 완료: pid %ld\n", new_pid > 0 ? new_pid : child);
            fflush(stdout);
            rc = 0;
            break;
        }
        if (child_exited(child)) {
            fprintf(stderr, "error: 엔진이 기동 중 종료됨\n");
            print_log_tail(ro->log_path);
            rc = 6;
            break;
        }
        if (now_ms() - start >= budget) { /* 부호 없는 경과 비교 (랩 안전) */
            fprintf(stderr, "error: %d초 내 준비 확인 못함 — %s 확인 요망\n", ro->wait_sec, ro->log_path);
            rc = 4;
            break;
        }
        sleep_ms(1000);
    }
#ifdef _WIN32
    if (g_child_proc != NULL) { /* 조기 종료 감지용 핸들은 여기서 닫는다 */
        CloseHandle(g_child_proc);
        g_child_proc = NULL;
    }
#endif
    return rc;
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
    if (strcmp(argv[i], "up") == 0) {
        return run_up(&opts, argc - (i + 1), argv + i + 1);
    }
    if (strcmp(argv[i], "engine") == 0 && i + 1 < argc && strcmp(argv[i + 1], "restart") == 0) {
        restart_opts_t ro = {"A016C000", "/tmp/engine-lived2.log", 0, 240};
        for (int j = i + 2; j < argc; j++) {
            if (strcmp(argv[j], "--symbol") == 0 && j + 1 < argc) {
                ro.symbol = argv[++j];
            } else if (strcmp(argv[j], "--log") == 0 && j + 1 < argc) {
                ro.log_path = argv[++j];
            } else if (strcmp(argv[j], "--pidfile") == 0 && j + 1 < argc) {
                ro.pidfile = argv[++j];
            } else if (strcmp(argv[j], "--wait") == 0 && j + 1 < argc) {
                ro.wait_sec = atoi(argv[++j]);
            } else {
                fprintf(stderr, "error: engine restart 인자 오류: %s\n", argv[j]);
                return 2;
            }
        }
        if (ro.wait_sec < 1) {
            fprintf(stderr, "error: --wait는 1초 이상이어야 합니다\n");
            return 2;
        }
        if (ro.wait_sec > 86400) {
            ro.wait_sec = 86400; /* Win32 GetTickCount(49일 랩)·32비트 unsigned long 곱셈 안전 상한 */
        }
        /* 엔진과 같은 규칙: 기본 pid 파일은 --endpoint 포트로 유도.
         * Windows에는 /tmp가 없어 엔진이 파일을 못 쓴다. */
        char pidfile_buf[320];
        if (ro.pidfile == 0) {
#ifdef _WIN32
            char tmp[MAX_PATH];
            DWORD len = GetTempPathA((DWORD)sizeof(tmp), tmp);
            if (len == 0 || len >= sizeof(tmp)) {
                snprintf(pidfile_buf, sizeof(pidfile_buf), "trading-engine-%d.pid",
                         cmd_port_of(opts.endpoint));
            } else {
                snprintf(pidfile_buf, sizeof(pidfile_buf), "%strading-engine-%d.pid",
                         tmp, cmd_port_of(opts.endpoint));
            }
#else
            snprintf(pidfile_buf, sizeof(pidfile_buf), "/tmp/trading-engine-%d.pid",
                     cmd_port_of(opts.endpoint));
#endif
            ro.pidfile = pidfile_buf;
        }
        return run_engine_restart(&opts, &ro);
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
