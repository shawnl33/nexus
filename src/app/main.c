#ifndef _WIN32
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) {
    Sleep((DWORD)ms);
}
#else
#include <time.h>
#include <unistd.h>
static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, 0);
}
#endif

#include "adapters/history/csv_ticks.h"
#include "adapters/ipc/ipc.h"
#include "runtime/engine.h"

#ifndef TRADING_ENGINE_VERSION
#define TRADING_ENGINE_VERSION "0.0.0-dev"
#endif

static void print_usage(const char *prog) {
    printf("C Trading Engine %s\n", TRADING_ENGINE_VERSION);
    printf("\n");
    printf("Usage: %s [options]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help              Show this help and exit\n");
    printf("  -v, --version           Show version and exit\n");
    printf("      --data-dir DIR      Engine data directory (reserved; not used yet)\n");
    printf("      --replay FILE       Replay CSV ticks (epoch_us,price,qty) and publish status\n");
    printf("      --replay-delay MS   Slow replay: delay between ticks (default 0)\n");
    printf("      --cmd-endpoint EP   Command endpoint (default tcp://127.0.0.1:5555)\n");
    printf("      --pub-endpoint EP   Status stream endpoint (default tcp://127.0.0.1:5556)\n");
    printf("\n");
    printf("Replay mode requires no API credentials.\n");
}

static void print_version(void) {
    printf("trading-engine %s\n", TRADING_ENGINE_VERSION);
}

/* 실행마다 고유한 엔진 실행 ID. 재시작을 구독자가 식별하는 값이다 (계획서 §16). */
static uint64_t make_engine_instance_id(void) {
#ifdef _WIN32
    uint64_t pid = (uint64_t)GetCurrentProcessId();
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t100ns = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (t100ns << 16) ^ pid;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL) << 16 ^
           (uint64_t)getpid();
#endif
}

#define BB_CAP 512
static tr_candle_t g_bb_storage[BB_CAP];
static double g_score_mid[64];

static int run_replay(const char *path, const char *cmd_ep, const char *pub_ep, int delay_ms) {
    size_t n = 0;
    tr_replay_tick_t *ticks = tr_csv_ticks_load(path, 1, &n);
    if (ticks == 0) {
        fprintf(stderr, "error: cannot load ticks from %s\n", path);
        return 3;
    }
    if (n == 0) {
        fprintf(stderr, "error: no ticks in %s\n", path);
        tr_csv_ticks_free(ticks);
        return 3;
    }

    char err[256] = {0};
    uint64_t engine_instance_id = make_engine_instance_id();
    tr_ipc_config_t icfg;
    memset(&icfg, 0, sizeof(icfg));
    icfg.cmd_endpoint = cmd_ep;
    icfg.pub_endpoint = pub_ep;
    icfg.engine_instance_id = engine_instance_id;
    icfg.pub_sndhwm = 1000;
    tr_ipc_t *ipc = tr_ipc_open(&icfg, err, sizeof(err));
    if (ipc == 0) {
        fprintf(stderr, "error: ipc open failed: %s\n", err);
        tr_csv_ticks_free(ticks);
        return 3;
    }

    tr_engine_config_t ecfg;
    memset(&ecfg, 0, sizeof(ecfg));
    ecfg.engine_instance_id = engine_instance_id;
    ecfg.instrument_id = 1;
    ecfg.session = (tr_session_policy_t){540, 540, 930, TR_SESSION_WEEKDAYS};
    ecfg.timeframe_sec = 60;
    ecfg.no_trade = TR_NO_TRADE_SKIP;
    ecfg.predict_bars[0] = 5;
    ecfg.predict_bars[1] = 10;
    ecfg.predict_bars[2] = 15;
    ecfg.htf_ticks = 10;
    ecfg.min_r2 = 0.40;
    ecfg.market_period = 20;

    tr_engine_t engine;
    if (!tr_engine_init(&engine, &ecfg, g_bb_storage, BB_CAP, g_score_mid, 64)) {
        fprintf(stderr, "error: engine init failed\n");
        tr_ipc_close(ipc);
        tr_csv_ticks_free(ticks);
        return 3;
    }
    tr_engine_attach_ipc(&engine, ipc, "display");

    if (delay_ms > 0) {
        /* 느린 재생: 대시보드가 실시간으로 관찰할 수 있게 한다 (논리 시간은 기록값 유지) */
        tr_replay_result_t result;
        result.fed = 0;
        result.rejected = 0;
        result.first_bad_index = (size_t)-1;
        for (size_t i = 0; i < n; i++) {
            tr_engine_on_timer(&engine, ticks[i].env.event_time_us);
            tr_bb_status_t st = tr_engine_on_tick(&engine, &ticks[i].env, &ticks[i].tick);
            if (st == TR_BB_ERROR || st == TR_BB_REJECTED_OUT_OF_SESSION) {
                result.rejected++;
            } else {
                result.fed++;
            }
            sleep_ms(delay_ms);
        }
        tr_engine_on_timer(&engine, ticks[n - 1].env.event_time_us + 60000000);
        printf("replay done: %zu ticks fed, %zu rejected, %llu status messages published\n",
               result.fed, result.rejected, (unsigned long long)(engine.status_seq - 1));
        printf("cmd endpoint: %s, pub endpoint: %s\n", cmd_ep, pub_ep);
        tr_ipc_close(ipc);
        tr_csv_ticks_free(ticks);
        return 0;
    }

    tr_replay_result_t result;
    if (!tr_replay_run(&engine.bb, ticks, n, ticks[n - 1].env.event_time_us + 60000000, &result)) {
        fprintf(stderr, "error: replay failed at event %zu (out of order input)\n", result.first_bad_index);
        tr_ipc_close(ipc);
        tr_csv_ticks_free(ticks);
        return 3;
    }

    printf("replay done: %zu ticks fed, %zu rejected, %llu status messages published\n",
           result.fed, result.rejected, (unsigned long long)(engine.status_seq - 1));
    printf("cmd endpoint: %s, pub endpoint: %s\n", cmd_ep, pub_ep);

    tr_ipc_close(ipc);
    tr_csv_ticks_free(ticks);
    return 0;
}

int main(int argc, char **argv) {
    const char *replay_file = 0;
    const char *cmd_ep = "tcp://127.0.0.1:5555";
    const char *pub_ep = "tcp://127.0.0.1:5556";
    int replay_delay_ms = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            replay_file = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--replay-delay") == 0 && i + 1 < argc) {
            replay_delay_ms = atoi(argv[++i]);
            if (replay_delay_ms < 0) {
                replay_delay_ms = 0;
            }
            continue;
        }
        if (strcmp(argv[i], "--cmd-endpoint") == 0 && i + 1 < argc) {
            cmd_ep = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--pub-endpoint") == 0 && i + 1 < argc) {
            pub_ep = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--data-dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --data-dir requires a value\n");
                return 2;
            }
            i++;
            continue;
        }
        fprintf(stderr, "error: unknown option '%s' (try --help)\n", argv[i]);
        return 2;
    }

    if (replay_file != 0) {
        return run_replay(replay_file, cmd_ep, pub_ep, replay_delay_ms);
    }

    fprintf(stderr, "error: no mode specified; see --help\n");
    return 1;
}
