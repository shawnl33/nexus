#ifndef _WIN32
#define _POSIX_C_SOURCE 199309L
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
static void sleep_ms(int ms) {
    Sleep((DWORD)ms);
}
#else
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
#include "adapters/ls/ls_auth.h"
#include "adapters/ls/ls_chart.h"
#include "adapters/ls/ls_http.h"
#include "adapters/ls/ls_master.h"
#include "adapters/ls/ls_ovsfut.h"
#include "adapters/ls/ls_realtime.h"
#include "core/model/civil_time.h"
#include "core/indicators/weekly_plot_v1.h"
#include "core/signals/weekly_pair_v1.h"
#include "runtime/bar_cache.h"
#include "runtime/engine.h"
#ifdef TR_HAS_STORE
#include "adapters/storage/live_bars.h"
#endif
#include "yyjson.h"

#include <signal.h>

/* status 페이로드에 싣는 자기 pid — traderctl engine restart가 소멸을 확인하는 기준이다 */
static long self_pid(void) {
#ifdef _WIN32
    return (long)_getpid();
#else
    return (long)getpid();
#endif
}

/* pid 파일: status 미응답(백필·replay 묵병 구간)에도 traderctl이 생사를 판별할 수 있게
 * ipc bind 성공 직후에 자기 pid를 기록한다(bind 실패 엔진은 파일을 만들지 않는다).
 * 기본 경로는 명령 엔드포인트 포트에서 유도(/tmp/trading-engine-<port>.pid)해
 * 다른 포트의 replay/테스트 엔진과 파일이 갈린다. 삭제는 내용이 자기 pid일 때만 —
 * 어떤 경로로든 자기 것이 아닌 파일을 지우지 않는다. status의 pid와 같은 소스(getpid)다. */
static const char *g_pidfile_path = 0;
static tr_ipc_t *g_boot_ipc = 0; /* 백필 중에도 status에 답하기 위한 소켓 */

static void live_command_handler(void *ctx, tr_ipc_command_t *cmd);

static void boot_poll_ipc(void) {
    if (g_boot_ipc != 0) {
        tr_ipc_poll(g_boot_ipc, 0, 4, live_command_handler, 0);
    }
}

static void default_pid_path(char *buf, size_t n, int port) {
#ifdef _WIN32
    char tmp[MAX_PATH];
    DWORD len = GetTempPathA((DWORD)sizeof(tmp), tmp);
    if (len == 0 || len >= sizeof(tmp)) {
        snprintf(buf, n, "trading-engine-%d.pid", port);
        return;
    }
    snprintf(buf, n, "%strading-engine-%d.pid", tmp, port);
#else
    snprintf(buf, n, "/tmp/trading-engine-%d.pid", port);
#endif
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

static void pidfile_write(const char *path) {
    FILE *f = fopen(path, "w");
    if (f == 0) {
        fprintf(stderr, "warning: failed to write pid file: %s\n", path);
        return;
    }
    fprintf(f, "%ld", self_pid());
    fclose(f);
    g_pidfile_path = path; /* 쓰기에 성공한 것만 삭제 대상으로 기억 */
}

static void pidfile_remove(void) {
    if (g_pidfile_path == 0) {
        return;
    }
    FILE *f = fopen(g_pidfile_path, "r");
    if (f != 0) {
        long v = -1;
        int mine = fscanf(f, "%ld", &v) == 1 && v == self_pid();
        fclose(f);
        if (mine) {
            remove(g_pidfile_path);
        }
    }
    g_pidfile_path = 0;
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

#define BB_STORE_MAX 8192 /* 봉 링 저장소. 실행 중 상한은 이 값을 넘지 못한다 */
#define BB_CAP_DEFAULT 2560 /* 2일치 1분봉(선물 주간+야간 2,130) + 라이브 여유 */
#define BB_CAP_MIN 16
static size_t g_bar_cap = BB_CAP_DEFAULT; /* 대시보드 상한 입력. chart.cap 으로 바뀐다 */

static size_t clamp_bar_cap(long n) {
    if (n < BB_CAP_MIN) {
        return BB_CAP_MIN;
    }
    if ((size_t)n > BB_STORE_MAX) {
        return BB_STORE_MAX;
    }
    return (size_t)n;
}

/* 파이프라인별 저장소 풀 (다중 종목 지원 — 관측 상한 TR_ENGINE_MAX_PIPES).
 * 단일 종목 경로는 pool[0]을 쓰며, 기존 이름은 그 별칭으로 유지한다.
 * 링 용량은 g_bar_cap(≤ BB_STORE_MAX)이고, 배열은 최댓값으로 잡아 둔다. */
static tr_candle_t g_bb_pool[TR_ENGINE_MAX_PIPES][BB_STORE_MAX];
static tr_bar_status_t g_status_pool[TR_ENGINE_MAX_PIPES][BB_STORE_MAX]; /* 봉별 지표 링 */
static tr_candle_t g_mkt_pool[TR_ENGINE_MAX_PIPES][64];            /* ⑧ 마켓 밴드용 (마켓계산기간 20의 3배 여유) */
static double g_score_mid_pool[TR_ENGINE_MAX_PIPES][64];
#define g_bb_storage (g_bb_pool[0])
#define g_status_storage (g_status_pool[0])
#define g_mkt_storage (g_mkt_pool[0])
#define g_score_mid (g_score_mid_pool[0])

static volatile sig_atomic_t g_stop = 0;
static volatile sig_atomic_t g_running = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

/* 세션 정책: 주식은 NXT 운영 시간(08:00~20:00) 전체를 받는다 (KRX 정규장 포함).
 * 선물은 주간 08:45~15:45 + 야간 18:00~익일 05:00 — 사이 공백(15:45~18:00)은 데이터가
 * 없어 무해하므로 익일 폐장 야간장 세션 하나로 표현한다 (close <= open 이면 익일 폐장).
 * 좁은 세션(정규장만)을 쓰면 NXT·야간 틱이 세션 밖으로 버려진다 (2026-09-28 유실 사건). */
static const tr_session_policy_t SESS_STOCK = {540, 480, 1200, TR_SESSION_WEEKDAYS};
static const tr_session_policy_t SESS_FUT = {540, 525, 300, TR_SESSION_WEEKDAYS};

/* live 모드 명령 컨텍스트: 관측(watch) 목록. 파이프라인 포인터는 remove 시 dangling이
 * 되므로 장기 보관하지 않는다 — shcode/instrument_id만 보관하고 매번 pipe_find로 조회한다.
 * 저장소 풀 슬롯은 엔진 파이프라인의 bb_storage 포인터로 역추적한다 (별도 장부 없음). */
typedef struct {
    char shcode[16];
    char tick_cd[4]; /* 실제 구독 중인 채널 (해지 시 그대로 사용) */
    char ob_cd[4];
    int kind;          /* LS_MARKET_* 값 — 주식/국내선물/해외선물 판별 (resolve_instrument) */
    bool rt_only;      /* 백필 불가(LS_CHART_EMPTY, 계정 권한) — 실시간 전용. 재백필·캐치업 제외 */
    uint64_t instrument_id;
} watch_entry_t;

typedef struct {
    tr_ls_rt_t *rt;
    tr_engine_t *engine;
    tr_ls_master_t *master;
    ls_auth_t *auth;
    watch_entry_t watches[TR_ENGINE_MAX_PIPES]; /* 엔진 파이프라인과 1:1 대응 (instrument_id가 키).
        순서는 파이프라인 순서와 무관하다 — pipe0 이식(pipe_remove)으로 pipes[] 순서는 바뀔 수 있다 */
    int watch_count;
} live_ctx_t;

#define BACKFILL_PAGE_BARS 500 /* 비압축 qrycnt 상한 (t8465 명세) */
static tr_candle_t g_hist[BB_STORE_MAX + BACKFILL_PAGE_BARS]; /* 저장 상한 + 마지막 페이지가 넘칠 자리 */
static tr_candle_t g_page[512];   /* 주간 페이지 스크래치 (비압축 500 상한) */
static tr_candle_t g_night[1008]; /* 야간 t8461 (서버 상한 999) */
static ls_daily_bar_t g_daily[32]; /* ⑤ 체인 프라임용 일봉 (t8410/t8466) */
static tr_candle_t g_cache_bars[BB_STORE_MAX];
static tr_candle_t g_merged_bars[BB_STORE_MAX];
static tr_candle_t g_merge_bars[BB_STORE_MAX];       /* RT 캐치업 병합 스크래치 (링 저장소와 분리) */
static tr_bar_status_t g_merge_status[BB_STORE_MAX];
#define HIST_CAP ((size_t)(sizeof(g_hist) / sizeof(g_hist[0])))
#define NIGHT_CAP ((size_t)(sizeof(g_night) / sizeof(g_night[0])))
#define BACKFILL_DAILY_BARS 15   /* ⑤ 체인 워밍업용 일봉 수 (dtl1/gap1 각 10세션 필요) */
#define RT_CATCHUP_GAP_US (120 * TR_US_PER_SEC) /* 이 이상 RT 이벤트가 끊기면 공백-재개로 본다 */

/* 미래 스텁 봉 필터: LS t8412(주식 분봉)는 프리마켓(08:00~09:00) 중 응답 마지막에
 * 미래 시각의 스텁 봉(당일 09:01, 거래량 0)을 싣는다 (2026-10-01 08:12/08:24 실측).
 * 백필이 이를 진짜 CLOSED 봉으로 주입하면 시리즈 꼬리가 한 칸 많아져 차트 시간축이
 * 어긋나므로 주입 전에 걸러낸다. 현재 진행 중 분의 봉(open ≤ now)은 정상 흐름이므로
 * 절대 걸리지 않게 1봉(timeframe)의 여유를 둔다 — 다음 분 이후 봉만 버린다.
 * 순수 판정이라 tests/test_backfill.c에서 extern으로 링크해 경계를 고정한다. */
bool tr_backfill_keep_bar(tr_time_us_t open_us, tr_time_us_t now_us, uint32_t timeframe_sec) {
    return open_us <= now_us + (tr_time_us_t)timeframe_sec * TR_US_PER_SEC;
}

/* RT-only 판정 (순수, tests/test_backfill.c에서 extern으로 링크해 경계를 고정한다):
 * 해외선물 백필이 LS_CHART_EMPTY("해당자료가 없습니다" — 계정 권한으로 차단, 2026-10-01
 * 실측)면 그 종목은 실시간 전용으로 표시한다. 일시적 빈 페이지로 오탐 고정될 수 있으므로
 * 회복 경로는 명시적으로 둔다 — 권한이 풀리거나 오탐이 의심되면 수동 unwatch/watch로
 * 즉시 백필을 재시도한다 (watch 때마다 o3103을 다시 시도한다). */
bool tr_backfill_marks_rt_only(int kind, int chart_rc) {
    return kind == LS_MARKET_OVS_FUT && chart_rc == LS_CHART_EMPTY;
}

/* 연속 페이지를 앞에 붙일지. 첫 페이지는 항상 받는다.
 * 다음 페이지의 가장 오래된 봉이 이미 가진 것보다 과거가 아니면 같은 페이지가
 * 반복된 것이다. 반복분을 링 상한까지 이어 붙이면 최신 구간만 남는다
 * (2026-10-03 005930: 같은 500봉이 반복되며 19:01–19:59 59봉만 주입됨). */
bool tr_backfill_page_extends(tr_time_us_t page_oldest, bool have_bars, tr_time_us_t held_oldest) {
    if (!have_bars) {
        return true;
    }
    return page_oldest < held_oldest;
}

static void persist_closed_bar(void *ctx, const tr_candle_t *bar) {
#ifdef TR_HAS_STORE
    (void)ctx;
    tr_live_bars_put(bar);
#else
    (void)ctx;
    (void)bar;
#endif
}

static size_t load_cached_bars(uint64_t instrument_id, uint32_t timeframe, tr_candle_t *out, size_t cap) {
#ifdef TR_HAS_STORE
    return tr_live_bars_load(instrument_id, timeframe, out, cap);
#else
    (void)instrument_id;
    (void)timeframe;
    (void)out;
    (void)cap;
    return 0;
#endif
}

/* 조회 봉과 캐시를 합쳐 주입한다. 파이프라인에 OPEN 봉이 생기기 전에 호출한다. */
static int inject_with_cache(tr_engine_t *eng, tr_pipeline_t *pipe, const tr_candle_t *broker, size_t nb) {
    size_t nc = load_cached_bars(pipe->instrument_id, pipe->bb.cfg.timeframe_sec, g_cache_bars, g_bar_cap);
    size_t nm = tr_bar_cache_merge(broker, nb, g_cache_bars, nc, g_merged_bars, g_bar_cap);
    int injected = 0;
    int future_dropped = 0;
    tr_time_us_t now_us = (tr_time_us_t)time(0) * TR_US_PER_SEC;
    for (size_t i = 0; i < nm; i++) {
        if (!tr_backfill_keep_bar(g_merged_bars[i].open_time_us, now_us, pipe->bb.cfg.timeframe_sec)) {
            future_dropped++;
            continue;
        }
        if (tr_engine_inject_bar(eng, &g_merged_bars[i])) {
            injected++;
        }
    }
    if (future_dropped > 0) {
        fprintf(stderr, "backfill %s: dropped %d future bars\n", pipe->shcode, future_dropped);
    }
    return injected;
}

/* 해외선물 워밍업 백필 (o3103, 1페이지 — 서버가 cts 연속 조회를 받지 않는다, 2026-10-01 실측).
 * 이 계정에서 CME는 "해당자료가 없습니다"(LS_CHART_EMPTY)가 와서 RT-only로 표시하고
 * 0을 돌려준다 (실패가 아니다 — 라이브부터 봉을 쌓는다). ⑤ 체인 일봉 프라임(o3108)은
 * 해외선물에서 스킵한다 (v1). 성공 시 주입한 봉 수, 실패 시 -1. */
static int backfill_ovs_minute_bars(ls_auth_t *auth, tr_engine_t *eng, tr_pipeline_t *pipe,
                                    const char *shcode, int kind, bool *rt_only_out) {
    size_t n = 0;
    char cerr[128] = {0};
    int rc = ls_chart_fetch_ovs_minute(auth, shcode, 1, BACKFILL_PAGE_BARS,
                                       pipe->instrument_id, 2, g_page,
                                       sizeof(g_page) / sizeof(g_page[0]), &n, cerr, sizeof(cerr));
    if (tr_backfill_marks_rt_only(kind, rc)) {
        /* 계정 권한으로 과거 데이터가 차단됐다 — stderr 1줄로 표시하고 라이브만 쌓는다.
         * rt_only 종목은 60초 재백필·RT 캐치업 대상에서 빠진다 (로그 홍수 방지) */
        fprintf(stderr, "backfill %s: no overseas-futures history (account permission) - starting RT-only (unwatch/watch after access is fixed)\n",
                shcode);
        if (rt_only_out != 0) {
            *rt_only_out = true;
        }
        return inject_with_cache(eng, pipe, 0, 0);
    }
    if (rc != LS_HTTP_OK && rc != LS_CHART_EMPTY) {
        fprintf(stderr, "ovs backfill %s failed rc=%d: %s\n", shcode, rc, cerr);
        int cached = inject_with_cache(eng, pipe, 0, 0);
        return cached > 0 ? cached : -1;
    }
    return inject_with_cache(eng, pipe, g_page, n);
}

/* 워밍업 백필: 1분봉(실제 OHLC)을 링 상한까지 조회해 봉 자체로 주입한다.
 * 주간은 t8465/t8412, 선물 야간은 t8461 — 두 계열을 시각으로 병합해 오름차순 주입한다.
 * (봉 낶부 틱 경로는 알 수 없지만 OHLC·거래량은 실측값이다.
 * 호가 지표는 과거 호가가 없어 라이브부터 워밍업된다)
 * kind는 LS_MARKET_* 값. rt_only_out은 해외선물이 RT-only로 표시됐을 때 true로 세팅된다.
 * 성공 시 주입한 봉 수, 실패 시 -1. */
static int backfill_minute_bars(ls_auth_t *auth, tr_engine_t *eng, tr_pipeline_t *pipe,
                                const char *shcode, int kind, bool *rt_only_out) {
    if (rt_only_out != 0) {
        *rt_only_out = false;
    }
    if (kind == LS_MARKET_OVS_FUT) {
        return backfill_ovs_minute_bars(auth, eng, pipe, shcode, kind, rt_only_out);
    }
    const bool is_fut = kind == LS_MARKET_KP200_FUT;
    /* 지수옵션 분봉·일봉은 이 계정에서 t8415가 IGW00215(유효하지 않은 TR)라 t8465/t8466을 쓴다
     * (2026-10-04 B016A745 실측). 연속 조회는 선물과 다르다: edate에 cts를 옮기면 같은
     * 페이지가 반복되고, cts를 유지하고 tr_cont=Y일 때만 과거로 넘어간다. 야간 t8461은
     * 옵션에 확인하지 않았으므로 주간 봉만 넣는다. */
    const bool is_opt = kind == LS_MARKET_KP200_OPT;
    size_t target = g_bar_cap;
    /* 1) 주간 페이지 수집: 최신→과거 순으로 오므로 배열 끝에서부터 앞으로 채워 오름차순을 만든다 */
    size_t hi = HIST_CAP;
    char cont_date[9] = "";
    char cont_time[11] = "";
    bool have_cont = false;
    /* 페이지가 500 미만이어도 상한에 닿도록 페이지 수를 저장 상한까지 연다.
     * 연속 조회는 TR마다 다르다 (ls_chart_fetch_minute 주석).
     * 주식 t8412: edate=99999999 유지, 이전 cts를 InBlock에 넣고 tr_cont=Y.
     * 선물 t8465: cts는 비우고 edate/etime에 이전 cts를 넣는다. */
    const size_t page_limit = (BB_STORE_MAX / BACKFILL_PAGE_BARS) + 2;
    for (size_t pg = 0; pg < page_limit && HIST_CAP - hi < target; pg++) {
        boot_poll_ipc();
        ls_chart_page_t page;
        char cerr[128] = {0};
        const char *edate = "99999999";
        const char *etime = " ";
        const char *cts_d = " ";
        const char *cts_t = " ";
        if (have_cont) {
            if (is_fut) {
                edate = cont_date;
                etime = cont_time;
            } else {
                cts_d = cont_date;
                cts_t = cont_time;
            }
        }
        int rc = ls_chart_fetch_minute(auth,
                                       (is_fut || is_opt) ? LS_CHART_FUT_MIN : LS_CHART_STOCK_MIN,
                                       shcode, 1, BACKFILL_PAGE_BARS, edate, etime, cts_d, cts_t,
                                       pipe->instrument_id, 2, g_page,
                                       sizeof(g_page) / sizeof(g_page[0]), &page, cerr, sizeof(cerr));
        if (rc == LS_CHART_EMPTY) {
            break;
        }
        if (rc != LS_HTTP_OK) {
            fprintf(stderr, "backfill page %zu failed rc=%d: %s\n", pg, rc, cerr);
            break;
        }
        if (page.count == 0 || page.count > hi) {
            break; /* 남은 자리가 한 페이지보다 작으면 넣지 않는다 */
        }
        bool have_bars = hi < HIST_CAP;
        tr_time_us_t held_oldest = have_bars ? g_hist[hi].open_time_us : 0;
        if (!tr_backfill_page_extends(g_page[0].open_time_us, have_bars, held_oldest)) {
            fprintf(stderr, "backfill %s: page %zu is not older; stopping\n", shcode, pg);
            break;
        }
        hi -= page.count;
        memcpy(g_hist + hi, g_page, page.count * sizeof(tr_candle_t));
        if (!page.has_more) {
            break;
        }
        if (have_cont && strcmp(page.cts_date, cont_date) == 0 &&
            strcmp(page.cts_time, cont_time) == 0) {
            break; /* 커서가 안 움직이면 다음 호출도 같은 페이지다 */
        }
        snprintf(cont_date, sizeof(cont_date), "%s", page.cts_date);
        snprintf(cont_time, sizeof(cont_time), "%s", page.cts_time);
        have_cont = cont_date[0] != '\0' && cont_time[0] != '\0';
    }
    const tr_candle_t *day = g_hist + hi;
    size_t nday = HIST_CAP - hi;

    /* 1-1) ⑤ 매매 상태 체인(dtl1/gap1) 워밍업: 일봉(t8410/t8466)으로 과거 완성 세션을
     * 프라임한다. 분봉 백필이 커버하는 세션(최근 ~2일)은 분봉 주입이 완성하므로 중복을
     * 피해 그보다 오래된 일봉만 프라임한다. 실패해도 분봉 백필은 그대로 진행한다
     * (기존 백필 실패 관용 패턴). */
    if (nday > 0) {
        int64_t oldest_day = 0;
        tr_local_day_and_min(day[0].open_time_us, 540, &oldest_day, 0);
        char derr[128] = {0};
        size_t ndaily = 0;
        boot_poll_ipc();
        int drc = ls_chart_fetch_daily(auth,
                                       (is_fut || is_opt) ? LS_CHART_FUT_DAY : LS_CHART_STOCK_DAY,
                                       shcode, BACKFILL_DAILY_BARS, "99999999",
                                       g_daily, sizeof(g_daily) / sizeof(g_daily[0]),
                                       &ndaily, derr, sizeof(derr));
        if (drc == LS_HTTP_OK || drc == LS_CHART_EMPTY) {
            double mids[32], trs[32]; /* g_daily와 같은 상한, 오래된 순으로 채운다 */
            size_t nd = 0;
            for (size_t i = 0; i < ndaily; i++) {
                if (g_daily[i].day >= oldest_day) {
                    continue; /* 분봉 커버 구간(당일 진행 중 포함)은 프라임하지 않는다 */
                }
                mids[nd] = ((double)g_daily[i].high + (double)g_daily[i].low) / 2.0;
                trs[nd] = (double)(g_daily[i].high - g_daily[i].low);
                nd++;
            }
            if (nd > 0) {
                tr_dtl1_prime(&pipe->dtl1, mids, nd);
                tr_gap1_prime(&pipe->gap1, trs, nd);
            }
        } else {
            fprintf(stderr, "daily backfill unavailable rc=%d: %s (chain warmup skipped)\n",
                    drc, derr);
        }
    }

    /* 2) 선물이면 야간 세션(t8461)도 가져온다. 주식은 t8412가 NXT까지 포함해 불필요.
     * 야간 봉은 날짜가 없어 실제 거래일(t8465 주간 봉의 날짜)로 부여한다 — 추석 같은
     * 연휴가 끼면 평일 추정이 어긋난다 (2026-09-28 실측 사건). */
    size_t nnight = 0;
    if (is_fut && nday > 0) {
        int64_t tdays[16];
        size_t ntd = 0;
        for (size_t i = 0; i < nday && ntd < 16; i++) {
            int64_t d;
            uint32_t m;
            tr_local_day_and_min(day[i].open_time_us, 540, &d, &m);
            if (ntd == 0 || tdays[ntd - 1] != d) {
                tdays[ntd++] = d;
            }
        }
        char nerr[128] = {0};
        int nrc = ls_chart_fetch_fut_night(auth, shcode, 999, tdays, ntd,
                                           pipe->instrument_id, 2,
                                           g_night, NIGHT_CAP, &nnight, nerr, sizeof(nerr));
        if (nrc != LS_HTTP_OK) {
            fprintf(stderr, "night backfill unavailable rc=%d: %s (day session only)\n", nrc, nerr);
            nnight = 0;
        }
    }

    /* 3) 두 오름차순 계열을 시각으로 모은 뒤 캐시와 합쳐 주입한다.
     * 미래 스텁 봉(t8412 프리마켓 응답 꼬리)은 여기서 걸러낸다. */
    tr_time_us_t now_us = (tr_time_us_t)time(0) * TR_US_PER_SEC;
    size_t i = 0, j = 0, nb = 0;
    int future_dropped = 0;
    while (i < nday || j < nnight) {
        const tr_candle_t *c;
        if (j >= nnight || (i < nday && day[i].open_time_us <= g_night[j].open_time_us)) {
            c = &day[i++];
            if (j < nnight && c->open_time_us == g_night[j].open_time_us) {
                j++;
            }
        } else {
            c = &g_night[j++];
        }
        if (!tr_backfill_keep_bar(c->open_time_us, now_us, pipe->bb.cfg.timeframe_sec)) {
            future_dropped++;
            continue;
        }
        if (nb < g_bar_cap) {
            g_merge_bars[nb++] = *c;
        } else {
            memmove(g_merge_bars, g_merge_bars + 1, (g_bar_cap - 1) * sizeof(g_merge_bars[0]));
            g_merge_bars[g_bar_cap - 1] = *c;
        }
    }
    if (future_dropped > 0) {
        fprintf(stderr, "backfill %s: dropped %d future bars\n", shcode, future_dropped);
    }
    int injected = inject_with_cache(eng, pipe, g_merge_bars, nb);
    return injected;
}

/* RT 공백 캐치업: 실시간 이벤트가 RT_CATCHUP_GAP_US 이상 끊겼다가 재개될 때 호출된다.
 * 재연결만으로는 공백 구간의 봉이 영구 유실되므로(2026-09-30 사건: 13:08~13:34 반복
 * 접속 끊김), 모든 관측 종목의 최근 분봉을 다시 조회해(edate=99999999 — 공백은 항상
 * 최근 구간이라 1페이지 500봉이면 수 시간을 커버한다) 봉 링에 병합한다.
 * 병합은 지표 재평가 없이 빠진 봉만 삽입한다 (tr_engine_pipe_merge_bars) — 라이브
 * 워밍업된 호가·회귀 상태를 보존하기 위한다. watch 백필과 마찬가지로 루프를 막는다.
 * 선물 야간 세션(t8461)은 커버하지 않는다 — 주간 TR(t8465)만 조회한다 (후속 과제). */
static void rt_catchup_missing_bars(live_ctx_t *lc, tr_time_us_t now_us) {
    fprintf(stderr, "rt catch-up: start (watches=%d)\n", lc->watch_count);
    for (int i = 0; i < lc->watch_count; i++) {
        const watch_entry_t *w = &lc->watches[i];
        if (w->rt_only) {
            continue; /* RT-only는 조회해 봐야 계정 권한으로 비어 있다 (로그 홍수 방지) */
        }
        char cerr[128] = {0};
        size_t fetched = 0;
        int rc;
        if (w->kind == LS_MARKET_OVS_FUT) {
            /* 해외선물은 o3103 1페이지 (연속 조회 미지원, 2026-10-01 실측) */
            rc = ls_chart_fetch_ovs_minute(lc->auth, w->shcode, 1, BACKFILL_PAGE_BARS,
                                           w->instrument_id, 2, g_page,
                                           sizeof(g_page) / sizeof(g_page[0]), &fetched,
                                           cerr, sizeof(cerr));
        } else {
            ls_chart_page_t page;
            rc = ls_chart_fetch_minute(lc->auth,
                                       (w->kind == LS_MARKET_KP200_FUT ||
                                        w->kind == LS_MARKET_KP200_OPT)
                                           ? LS_CHART_FUT_MIN
                                           : LS_CHART_STOCK_MIN,
                                       w->shcode, 1, BACKFILL_PAGE_BARS, "99999999", " ", " ", " ",
                                       w->instrument_id, 2, g_page,
                                       sizeof(g_page) / sizeof(g_page[0]), &page, cerr,
                                       sizeof(cerr));
            fetched = page.count;
        }
        if (rc != LS_HTTP_OK && rc != LS_CHART_EMPTY) {
            fprintf(stderr, "rt catch-up %s: fetch failed rc=%d: %s\n", w->shcode, rc, cerr);
            continue;
        }
        size_t merged = tr_engine_pipe_merge_bars(lc->engine, w->instrument_id,
                                                  g_page, fetched, now_us,
                                                  g_merge_bars, g_bar_cap,
                                                  g_merge_status, g_bar_cap);
        fprintf(stderr, "rt catch-up %s: merged %zu bars (page=%zu)\n", w->shcode, merged,
                fetched);
    }
    fprintf(stderr, "rt catch-up: done\n");
}

static live_ctx_t g_live_ctx;
static time_t g_master_retry_after = 0;

/* 기동 때 마스터 조회가 실패하면 검색(market.instruments)이 세션 내내 거절된다.
 * 명령 시점에 한 번 더 받고, 연속 실패는 30초 간격으로만 재시도한다. */
static bool ensure_master(live_ctx_t *lc) {
    if (lc->master != 0) {
        return true;
    }
    if (lc->auth == 0) {
        return false;
    }
    time_t now = time(0);
    if (now < g_master_retry_after) {
        return false;
    }
    char merr[128] = {0};
    tr_ls_master_t *master = ls_master_fetch(lc->auth, merr, sizeof(merr));
    if (master == 0) {
        fprintf(stderr, "instrument master unavailable: %s\n", merr);
        g_master_retry_after = now + 30;
        return false;
    }
    lc->master = master;
    printf("instruments: %zu registered\n", ls_master_count(master));
    fflush(stdout);
    return true;
}

/* shcode → instrument_id 해시 (FNV-1a). 종목별 지표 상태가 섞이지 않게 한다 */
static uint64_t instrument_id_of(const char *shcode) {
    uint64_t h = 1469598103934665603ULL;
    while (*shcode) {
        h ^= (unsigned char)*shcode++;
        h *= 1099511628211ULL;
    }
    return h != 0 ? h : 1;
}

/* 선물 실시간 채널: 주간 FC9/FH9, 야간(KRX야간파생 18:00~05:00) DC0/DH0
 * (docs/ls_api_mapping.md §4). 현재 시각(KST)으로 고른다.
 * 세션 경계 자동 재구독은 미지원 — 기동·market select 시점에 재평가된다. */
static void fut_rt_channels(const char **tick_cd, const char **ob_cd) {
    long kst_sec = (long)((time(0) + 9 * 3600) % 86400);
    int day = kst_sec >= 5 * 3600 && kst_sec < 15 * 3600 + 45 * 60;
    *tick_cd = day ? "FC9" : "DC0";
    *ob_cd = day ? "FH9" : "DH0";
}

/* 종목 유형별 실시간 채널 선택 (kind는 LS_MARKET_* 값).
 * 해외선물은 OVC(체결)/OVH(호가) — tr_key 8자리 공백 패딩은 어댑터가 채널로 판별한다 */
static void rt_channels_for(int kind, const char **tick_cd, const char **ob_cd) {
    if (kind == LS_MARKET_OVS_FUT) {
        *tick_cd = "OVC";
        *ob_cd = "OVH";
    } else if (kind == LS_MARKET_KP200_FUT) {
        fut_rt_channels(tick_cd, ob_cd);
    } else if (kind == LS_MARKET_KP200_OPT) {
        *tick_cd = "OC0";
        *ob_cd = "OH0";
    } else {
        *tick_cd = "S3_";
        *ob_cd = "H1_";
    }
}

/* 종목 유형별 세션 정책 (값 반환 — 호출자가 주소를 넘긴다).
 * 해외선물은 CME 기준 KST 07:00 → 익일 06:00 (ls_ovsfut_session, DST 1시간 오차는 v1 한계) */
static tr_session_policy_t session_for(int kind) {
    if (kind == LS_MARKET_OVS_FUT) {
        return ls_ovsfut_session();
    }
    return (kind == LS_MARKET_KP200_FUT || kind == LS_MARKET_KP200_OPT) ? SESS_FUT : SESS_STOCK;
}

/* 종목 유형 판별 (3분류): LS_MARKET_* 값을 돌려준다.
 * 마스터 레지스트리(t8436+t8467+o3101+정적 표 등록분) 우선, 없으면 해외선물 정적 표
 * 접두 매치(롤링된 신규 월물), 그래도 없으면 코드 길이 추정(폐기 예정 경고).
 * tick_out에는 해외선물의 1틱 raw 크기를 돌려준다 (국내는 0 = 엔진 자동 규칙). */
static int resolve_instrument(tr_ls_master_t *master, const char *shcode,
                              const char **name_out, double *tick_out) {
    if (name_out != 0) {
        *name_out = 0;
    }
    if (tick_out != 0) {
        *tick_out = 0.0;
    }
    const ls_instrument_info_t *info = ls_master_find(master, shcode);
    if (info != 0) {
        if (name_out != 0) {
            *name_out = info->name;
        }
        if (info->market == LS_MARKET_OVS_FUT || info->market == LS_MARKET_KP200_OPT) {
            if (tick_out != 0) {
                *tick_out = info->tick_raw;
            }
            return info->market;
        }
        return info->is_futures ? LS_MARKET_KP200_FUT : LS_MARKET_KOSPI;
    }
    const ls_ovsfut_entry_t *ov = ls_ovsfut_find(shcode);
    if (ov != 0) {
        if (name_out != 0) {
            *name_out = ov->name;
        }
        if (tick_out != 0) {
            *tick_out = ov->tick_raw;
        }
        return LS_MARKET_OVS_FUT;
    }
    return strlen(shcode) > 6 ? LS_MARKET_KP200_FUT : LS_MARKET_KOSPI; /* 폐기 예정 추정 */
}

/* 엔진의 is_futures 규칙(호가 부호·자동 틱)은 국내·해외 선물이 같다 */
static bool kind_is_futures(int kind) {
    return kind == LS_MARKET_KP200_FUT || kind == LS_MARKET_OVS_FUT;
}

/* 워치 목록 조회 (shcode 기준). 없으면 -1 */
static int watch_find(const live_ctx_t *lc, const char *shcode) {
    for (int i = 0; i < lc->watch_count; i++) {
        if (strcmp(lc->watches[i].shcode, shcode) == 0) {
            return i;
        }
    }
    return -1;
}

/* 워치 목록 조회 (instrument_id 기준 — 파이프라인에서 역추적할 때). 없으면 -1 */
static int watch_find_by_id(const live_ctx_t *lc, uint64_t instrument_id) {
    for (int i = 0; i < lc->watch_count; i++) {
        if (lc->watches[i].instrument_id == instrument_id) {
            return i;
        }
    }
    return -1;
}

/* 어느 활성 파이프라인도 쓰지 않는 저장소 풀 슬롯을 찾는다 (엔진이 유일한 진실 원천).
 * 없으면 -1. 파이프라인 0 이식( pipe_remove ) 후에도 실사용 기준이라 안전하다. */
static int watch_pool_alloc(const live_ctx_t *lc) {
    for (int j = 0; j < TR_ENGINE_MAX_PIPES; j++) {
        bool used = false;
        for (int i = 0; i < lc->engine->pipe_count; i++) {
            if (lc->engine->pipes[i]->bb_storage == g_bb_pool[j]) {
                used = true;
                break;
            }
        }
        if (!used) {
            return j;
        }
    }
    return -1;
}

/* 파이프라인이 쓰는 풀 슬롯 번호 (부착 저장소를 같은 슬롯으로 맞추기 위한 역추적) */
static int watch_pool_of(const tr_pipeline_t *p) {
    for (int j = 0; j < TR_ENGINE_MAX_PIPES; j++) {
        if (p->bb_storage == g_bb_pool[j]) {
            return j;
        }
    }
    return 0;
}
#define MKT_POOL_CAP (sizeof(g_mkt_pool[0]) / sizeof(g_mkt_pool[0][0]))

/* 지표 매니페스트 본문: 대시보드 지표 선택 패널의 표시 목록 (레이어 defaultOn 포함).
 * 고정 길이 문자열이므로 sizeof로 스냅샷 버퍼의 헤드룸을 정확히 잡는다 */
static const char SNAP_IND_MANIFEST_A[] =
    "\"indicators\":["
    "{\"id\":\"mirae_v16\",\"name\":\"미래곡선 V16\",\"layers\":["
    "{\"id\":\"score\",\"name\":\"① 통합 점수\",\"defaultOn\":true},"
    "{\"id\":\"reg\",\"name\":\"② 회귀선\",\"defaultOn\":true},"
    "{\"id\":\"rays\",\"name\":\"③ 미래 목표선\",\"defaultOn\":true},"
    "{\"id\":\"band\",\"name\":\"④ 결과 띠\",\"defaultOn\":true},"
    "{\"id\":\"state\",\"name\":\"⑤ 매매 상태\",\"defaultOn\":true},"
    "{\"id\":\"memory\",\"name\":\"⑥ 방향 기억\",\"defaultOn\":true},"
    "{\"id\":\"snap\",\"name\":\"⑦ 지속 사진\",\"defaultOn\":true},"
    "{\"id\":\"mktband\",\"name\":\"⑧ 마켓 밴드\",\"defaultOn\":false}]},"
    "{\"id\":\"sma\",\"name\":\"이평선 5/20/60\",\"layers\":["
    "{\"id\":\"sma5\",\"name\":\"SMA 5\",\"defaultOn\":true},"
    "{\"id\":\"sma20\",\"name\":\"SMA 20\",\"defaultOn\":true},"
    "{\"id\":\"sma60\",\"name\":\"SMA 60\",\"defaultOn\":true}]},"
    "{\"id\":\"fx_mirae_v1\",\"name\":\"해외선물 미래곡선 V1\",\"layers\":["
    "{\"id\":\"score\",\"name\":\"단계화\",\"defaultOn\":false},"
    "{\"id\":\"reg\",\"name\":\"회귀선\",\"defaultOn\":true},"
    "{\"id\":\"market\",\"name\":\"마켓중심\",\"defaultOn\":true},"
    "{\"id\":\"persist\",\"name\":\"지속 목표\",\"defaultOn\":true},"
    "{\"id\":\"swingUp\",\"name\":\"지난상승\",\"defaultOn\":true},"
    "{\"id\":\"swingDn\",\"name\":\"지난하락\",\"defaultOn\":true},"
    "{\"id\":\"synth\",\"name\":\"합성 5/15/30\",\"defaultOn\":true}]},"
    "{\"id\":\"fx_mirae_v3\",\"name\":\"해외선물 미래곡선 V3\",\"layers\":["
    "{\"id\":\"stage\",\"name\":\"단계화\",\"defaultOn\":true},"
    "{\"id\":\"reg\",\"name\":\"회귀선\",\"defaultOn\":true},"
    "{\"id\":\"past\",\"name\":\"과거 채점\",\"defaultOn\":true},"
    "{\"id\":\"state\",\"name\":\"매매 상태\",\"defaultOn\":true},"
    "{\"id\":\"memory\",\"name\":\"방향 기억\",\"defaultOn\":true},"
    "{\"id\":\"persist\",\"name\":\"지속선\",\"defaultOn\":true},"
    "{\"id\":\"market\",\"name\":\"마켓 밴드\",\"defaultOn\":true},"
    "{\"id\":\"swing\",\"name\":\"스윙 되돌림\",\"defaultOn\":true},"
    "{\"id\":\"rays\",\"name\":\"미래 목표선\",\"defaultOn\":true},"
    "{\"id\":\"rayBand\",\"name\":\"미래 범위\",\"defaultOn\":true}]},"
    "{\"id\":\"fx_pgap3\",\"name\":\"지속목표차 삼선\",\"layers\":["
    "{\"id\":\"width\",\"name\":\"폭\",\"defaultOn\":true},"
    "{\"id\":\"ratio\",\"name\":\"비율\",\"defaultOn\":true},"
    "{\"id\":\"count\",\"name\":\"유지 봉수\",\"defaultOn\":true}]},"
    "{\"id\":\"fx_pgap5\",\"name\":\"지속목표차 오선\",\"layers\":["
    "{\"id\":\"width\",\"name\":\"폭\",\"defaultOn\":true},"
    "{\"id\":\"ratio\",\"name\":\"비율\",\"defaultOn\":true},"
    "{\"id\":\"count\",\"name\":\"유지 봉수\",\"defaultOn\":true}]},"
    "{\"id\":\"fx_rgap\",\"name\":\"평탄회귀차\",\"layers\":["
    "{\"id\":\"width\",\"name\":\"폭\",\"defaultOn\":true},"
    "{\"id\":\"ratio\",\"name\":\"비율\",\"defaultOn\":true},"
    "{\"id\":\"count\",\"name\":\"유지 봉수\",\"defaultOn\":true}]},"
    "{\"id\":\"fx_mgap\",\"name\":\"마켓중심차\",\"layers\":["
    "{\"id\":\"width\",\"name\":\"폭\",\"defaultOn\":true},"
    "{\"id\":\"ratio\",\"name\":\"비율\",\"defaultOn\":true},"
    "{\"id\":\"count\",\"name\":\"유지 봉수\",\"defaultOn\":true}]},"
    "{\"id\":\"fx_ugap\",\"name\":\"지속목표차 통합\",\"layers\":["
    "{\"id\":\"three\",\"name\":\"삼선\",\"defaultOn\":true},"
    "{\"id\":\"five\",\"name\":\"오선\",\"defaultOn\":true},"
    "{\"id\":\"reg\",\"name\":\"평탄회귀\",\"defaultOn\":true},"
    "{\"id\":\"market\",\"name\":\"마켓중심\",\"defaultOn\":true}]},"
    "{\"id\":\"fx_ymae\",\"name\":\"양매수\",\"layers\":["
    "{\"id\":\"range\",\"name\":\"구간 범위\",\"defaultOn\":true},"
    "{\"id\":\"hypo\",\"name\":\"가설 신호\",\"defaultOn\":true}]},";

static const char SNAP_IND_MANIFEST_B[] =
    "{\"id\":\"fx_curve_os\",\"name\":\"미래곡선 해외\",\"layers\":["
    "{\"id\":\"score\",\"name\":\"단계화\",\"defaultOn\":true},"
    "{\"id\":\"reg\",\"name\":\"회귀선·마켓\",\"defaultOn\":true},"
    "{\"id\":\"target\",\"name\":\"지속 목표\",\"defaultOn\":true},"
    "{\"id\":\"swing\",\"name\":\"지난 구간\",\"defaultOn\":true},"
    "{\"id\":\"synth\",\"name\":\"합성 5/15/30\",\"defaultOn\":true},"
    "{\"id\":\"entry\",\"name\":\"진입후보\",\"defaultOn\":true}]},"
    "{\"id\":\"fx_snco\",\"name\":\"스나이퍼 CO\",\"layers\":["
    "{\"id\":\"three\",\"name\":\"삼선 비율\",\"defaultOn\":true},"
    "{\"id\":\"reg\",\"name\":\"회귀 비율\",\"defaultOn\":true},"
    "{\"id\":\"market\",\"name\":\"마켓 비율\",\"defaultOn\":true},"
    "{\"id\":\"price\",\"name\":\"가격 비율\",\"defaultOn\":true},"
    "{\"id\":\"vol\",\"name\":\"거래량 비율\",\"defaultOn\":true},"
    "{\"id\":\"signal\",\"name\":\"압축 신호점\",\"defaultOn\":true},"
    "{\"id\":\"emphasis\",\"name\":\"비율 강조\",\"defaultOn\":true},"
    "{\"id\":\"range\",\"name\":\"범위·이탈\",\"defaultOn\":true},"
    "{\"id\":\"scope\",\"name\":\"스코프 유지\",\"defaultOn\":true},"
    "{\"id\":\"both\",\"name\":\"동시압축\",\"defaultOn\":true}]},"
    "{\"id\":\"fx_judge_v3\",\"name\":\"1분통합판정\",\"layers\":["
    "{\"id\":\"state\",\"name\":\"당일통합상태\",\"defaultOn\":true},"
    "{\"id\":\"fut\",\"name\":\"미래방향\",\"defaultOn\":true},"
    "{\"id\":\"prof\",\"name\":\"5선매물대\",\"defaultOn\":true},"
    "{\"id\":\"di\",\"name\":\"DI방향\",\"defaultOn\":true},"
    "{\"id\":\"adx\",\"name\":\"ADX상태\",\"defaultOn\":true},"
    "{\"id\":\"entry\",\"name\":\"진입후보\",\"defaultOn\":true},"
    "{\"id\":\"release\",\"name\":\"압축해제\",\"defaultOn\":true}]},"
    "{\"id\":\"fx_pack_v4\",\"name\":\"매물대압축\",\"layers\":["
    "{\"id\":\"hold\",\"name\":\"압축지속\",\"defaultOn\":true},"
    "{\"id\":\"ratio\",\"name\":\"폭비율\",\"defaultOn\":true},"
    "{\"id\":\"mark\",\"name\":\"해제·확정\",\"defaultOn\":true}]},"
    "{\"id\":\"fx_sniper\",\"name\":\"스나이퍼스코프\",\"layers\":["
    "{\"id\":\"three\",\"name\":\"삼선 비율\",\"defaultOn\":true},"
    "{\"id\":\"reg\",\"name\":\"회귀 비율\",\"defaultOn\":true},"
    "{\"id\":\"market\",\"name\":\"마켓 비율\",\"defaultOn\":true},"
    "{\"id\":\"price\",\"name\":\"가격 비율\",\"defaultOn\":true},"
    "{\"id\":\"marks\",\"name\":\"이탈 표시\",\"defaultOn\":true}]},"
    "{\"id\":\"fx_pvc\",\"name\":\"가격거래량압축\",\"layers\":["
    "{\"id\":\"price\",\"name\":\"가격 비율\",\"defaultOn\":true},"
    "{\"id\":\"volume\",\"name\":\"거래량 비율\",\"defaultOn\":true},"
    "{\"id\":\"both\",\"name\":\"동시 압축\",\"defaultOn\":true}]},"
    "{\"id\":\"fx_data2\",\"name\":\"스나이퍼 Data2\",\"layers\":["
    "{\"id\":\"three\",\"name\":\"삼선 비율\",\"defaultOn\":true},"
    "{\"id\":\"reg\",\"name\":\"회귀 비율\",\"defaultOn\":true},"
    "{\"id\":\"market\",\"name\":\"마켓 비율\",\"defaultOn\":true},"
    "{\"id\":\"price\",\"name\":\"가격 비율\",\"defaultOn\":true},"
    "{\"id\":\"marks\",\"name\":\"이탈 표시\",\"defaultOn\":true}]},"
    "{\"id\":\"w_ret_long\",\"name\":\"위클리 합산수익률 양매수\",\"layers\":["
    "{\"id\":\"ret\",\"name\":\"첫만남합산수익률\",\"defaultOn\":true},"
    "{\"id\":\"zero\",\"name\":\"영점기준\",\"defaultOn\":true}]},"
    "{\"id\":\"w_ret_short\",\"name\":\"위클리 합산수익률 양매도\",\"layers\":["
    "{\"id\":\"ret\",\"name\":\"양매도합산수익률\",\"defaultOn\":true},"
    "{\"id\":\"zero\",\"name\":\"영점기준\",\"defaultOn\":true}]},"
    "{\"id\":\"w_link_long\",\"name\":\"위클리 프라이스링크 양매수\",\"layers\":["
    "{\"id\":\"link\",\"name\":\"D3_첫만남가격\",\"defaultOn\":true}]},"
    "{\"id\":\"w_link_short\",\"name\":\"위클리 프라이스링크 양매도\",\"layers\":["
    "{\"id\":\"link\",\"name\":\"D3_첫만남가격\",\"defaultOn\":true}]},{"
    "\"id\":\"ks_data2\",\"name\":\"스나이퍼스코프 국내선물 Data2\",\"layers\":["
    "{\"id\":\"three\",\"name\":\"삼선 비율\",\"defaultOn\":true},"
    "{\"id\":\"reg\",\"name\":\"회귀 비율\",\"defaultOn\":true},"
    "{\"id\":\"market\",\"name\":\"마켓 비율\",\"defaultOn\":true},"
    "{\"id\":\"price\",\"name\":\"가격 비율\",\"defaultOn\":true},"
    "{\"id\":\"marks\",\"name\":\"이탈 표시\",\"defaultOn\":true}]}]}";

/* chart.snapshot 꼬리 고정 바이트: 섹션 구분자 3개(\"],\"ind|mem|pst\":[", 각 10자) +
 * 매니페스트 본문. 각 루프 가드는 항목 최악 크기에 이 고정 꼬리를 더한 만큼을 남긴다 —
 * 항목이 추정 최악 크기 안에 드는 동안은 꼬리(구분자·매니페스트)가 온전히 쓰인다.
 * gaps 섹션은 길이가 가변이라 여기 포함하지 않고, 직렬화된 실제 길이(gaps_len)를
 * 각 루프 가드가 따로 빼서 예약한다 */
#define SNAP_TAIL_FIXED (30 + (int)sizeof(SNAP_IND_MANIFEST_A) + (int)sizeof(SNAP_IND_MANIFEST_B) - 2)

/* snprintf는 잘리면 "썼어야 할 길이"를 돌려주므로 off가 buf 끝을 넘어설 수 있다.
 * 누적할 때마다 클램프해 뒤따르는 쓰기가 buf 범위 밖으로 나가지 않게 한다(메모리 안전).
 * 잘린 항목 자체는 복구되지 않으므로 추정 최악을 넘는 항목이 나오면 페이로드는 여전히
 * 잘린(malformed) JSON이 된다 — 내용 완전성이 아니라 쓰기 범위를 보호하는 장치다 */
#define SNAP_CLAMP(buf, off)                                                  \
    do {                                                                      \
        if ((off) < 0 || (size_t)(off) >= sizeof(buf)) {                      \
            (off) = (int)sizeof(buf) - 1;                                     \
        }                                                                     \
    } while (0)

/* 페어 시뮬 봉 버퍼. 링 상한과 같다. 명령 스레드 하나라 정적 버퍼를 쓴다. */
#define PAIR_EVT_MAX 160
static tr_wpair_pxbar_t g_pair_self[BB_STORE_MAX];
static tr_wpair_pxbar_t g_pair_opp[BB_STORE_MAX];
static tr_wpair_pxbar_t g_pair_fut[BB_STORE_MAX];
static tr_wpair_event_t g_pair_ev[PAIR_EVT_MAX];
static char g_pair_json[48 * 1024];
static tr_wplot_pt_t g_wplot_ll[BB_STORE_MAX];
static tr_wplot_pt_t g_wplot_lr[BB_STORE_MAX];
static tr_wplot_pt_t g_wplot_sl[BB_STORE_MAX];
static tr_wplot_pt_t g_wplot_sr[BB_STORE_MAX];
static char g_wplot_json[60 * 1024];

static void pair_code(char dst[16], const char *src) {
    size_t n = 0;
    if (src != 0) {
        for (const char *s = src; *s != '\0' && n < 15; s++) {
            unsigned char c = (unsigned char)*s;
            if (c >= 'a' && c <= 'z') {
                c = (unsigned char)(c - 'a' + 'A');
            }
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                dst[n++] = (char)c;
            }
        }
    }
    dst[n] = '\0';
}

static int pair_name_ok(const char *s) {
    if (s == 0) {
        return 0;
    }
    for (; *s != '\0'; s++) {
        if (*s == '"' || *s == '\\' || (unsigned char)*s < 0x20) {
            return 0;
        }
    }
    return 1;
}

/* 링의 오래된 봉부터. 가격은 raw/100. day_index는 현지 날짜 안의 봉 순번(0부터). */
static size_t pair_copy_ring(tr_wpair_pxbar_t *dst, const tr_pipeline_t *pipe) {
    size_t n = tr_ring_count(&pipe->bb.bars);
    int32_t off = pipe->session.utc_offset_min;
    int64_t prev_ymd = -1;
    int32_t day_i = 0;
    if (n > BB_STORE_MAX) {
        n = BB_STORE_MAX;
    }
    for (size_t i = 0; i < n; i++) {
        tr_candle_t c;
        tr_wpair_pxbar_t *b = &dst[i];
        tr_civil_t cv;
        if (!tr_ring_at(&pipe->bb.bars, n - 1 - i, &c)) {
            return i;
        }
        memset(b, 0, sizeof(*b));
        b->open_us = c.open_time_us;
        if (tr_civil_from_time_us(c.open_time_us, off, &cv)) {
            b->ymd = (int64_t)cv.year * 10000 + (int64_t)cv.month * 100 + (int64_t)cv.day;
            b->hhmmss = (int64_t)cv.hour * 10000 + (int64_t)cv.min * 100 + (int64_t)cv.sec;
        }
        if (b->ymd != prev_ymd) {
            day_i = 0;
            prev_ymd = b->ymd;
        }
        b->day_index = day_i++;
        b->open = (double)c.open / 100.0;
        b->high = (double)c.high / 100.0;
        b->low = (double)c.low / 100.0;
        b->close = (double)c.close / 100.0;
        b->volume = (double)c.volume;
    }
    return n;
}

static int pair_cfg_num(yyjson_val *cfg, const char *key, double *out) {
    yyjson_val *v;
    if (cfg == 0 || !yyjson_is_obj(cfg) || out == 0) {
        return 0;
    }
    v = yyjson_obj_get(cfg, key);
    if (!yyjson_is_num(v)) {
        return 0;
    }
    *out = yyjson_get_num(v);
    return 1;
}

/* 화면이 보낸 시스템 변수만 기본값 위에 덮는다. 없는 키는 원본 Input 기본값이다. */
static void pair_apply_user_cfg(tr_wpair_config_t *cfg, yyjson_val *raw) {
    double n;
    if (cfg == 0) {
        return;
    }
    if (pair_cfg_num(raw, "매매종료", &n)) cfg->trade_off = n != 0.0;
    if (pair_cfg_num(raw, "총투자금", &n)) cfg->capital = n;
    if (pair_cfg_num(raw, "익절률", &n)) cfg->take_pct = n;
    if (pair_cfg_num(raw, "손절률", &n)) cfg->stop_pct = n;
    if (pair_cfg_num(raw, "진입시작봉수", &n)) cfg->entry_start_bar = (int32_t)n;
    if (pair_cfg_num(raw, "오후전환봉수", &n)) cfg->afternoon_switch_bar = (int32_t)n;
    if (pair_cfg_num(raw, "오전삼선만남최대비율", &n)) cfg->morning_three_max = n;
    if (pair_cfg_num(raw, "오후삼선만남최대비율", &n)) cfg->afternoon_three_max = n;
    if (pair_cfg_num(raw, "진입시작", &n)) cfg->entry_start_time = (int64_t)n;
    if (pair_cfg_num(raw, "전량청산", &n)) cfg->flat_time = (int64_t)n;
    if (pair_cfg_num(raw, "만남방식", &n)) cfg->meet_mode = (int)n;
    if (pair_cfg_num(raw, "허용오차", &n)) cfg->tolerance = n;
    if (pair_cfg_num(raw, "진단출력사용", &n)) cfg->diag = (int)n;
    if (pair_cfg_num(raw, "분할청산", &n)) cfg->split_exit = (int)n;
    if (pair_cfg_num(raw, "만기잔량익절률", &n)) cfg->expiry_resid_pct = n;
    if (pair_cfg_num(raw, "잔량익절대기분", &n)) cfg->resid_wait_min = (int32_t)n;
    if (pair_cfg_num(raw, "저수익대기분", &n)) cfg->low_wait_min = (int32_t)n;
    if (pair_cfg_num(raw, "저수익기준률", &n)) cfg->low_pct = n;
    if (pair_cfg_num(raw, "저수익청산비율", &n)) cfg->low_cut_pct = n;
}

/* 보유 1분봉으로 페어 주문을 다시 계산한다. 브로커 주문은 내지 않는다. */
static void pair_sim_command(tr_ipc_command_t *cmd, const char *p) {
    tr_engine_t *eng = g_live_ctx.engine;
    char self[16], opp[16], fut[16];
    int side = 0;
    yyjson_doc *doc;
    yyjson_val *data;
    tr_pipeline_t *ps;
    tr_pipeline_t *po;
    tr_pipeline_t *pf;
    tr_wpair_config_t cfg;
    tr_ksscore_config_t scfg;
    tr_wpair_t st;
    size_t nself, nopp, nfut;
    int n;
    int shown;
    int off;
    double pct;
    const char *lname;

    self[0] = opp[0] = fut[0] = '\0';
    if (eng == 0) {
        cmd->status = "rejected";
        cmd->error_code = "not_ready";
        cmd->payload_json = 0;
        return;
    }
    if (eng->cfg.timeframe_sec != 60) {
        cmd->status = "rejected";
        cmd->error_code = "not_1m";
        cmd->payload_json = 0;
        return;
    }
    doc = yyjson_read((char *)p, strlen(p), 0);
    data = doc != 0 ? yyjson_obj_get(yyjson_doc_get_root(doc), "data") : 0;
    if (data != 0) {
        yyjson_val *sv = yyjson_obj_get(data, "side");
        yyjson_val *a = yyjson_obj_get(data, "self");
        yyjson_val *b = yyjson_obj_get(data, "opp");
        yyjson_val *c = yyjson_obj_get(data, "fut");
        if (yyjson_is_num(sv)) {
            side = (int)yyjson_get_num(sv);
        }
        if (yyjson_is_str(a)) {
            pair_code(self, yyjson_get_str(a));
        }
        if (yyjson_is_str(b)) {
            pair_code(opp, yyjson_get_str(b));
        }
        if (yyjson_is_str(c)) {
            pair_code(fut, yyjson_get_str(c));
        }
    }
    tr_wpair_default_config(&cfg);
    if (data != 0) {
        pair_apply_user_cfg(&cfg, yyjson_obj_get(data, "cfg"));
    }
    if (doc != 0) {
        yyjson_doc_free(doc);
    }
    if ((side != TR_WPAIR_LONG && side != TR_WPAIR_SHORT) || strlen(self) < 4 || strlen(opp) < 4 ||
        strlen(fut) < 4) {
        cmd->status = "rejected";
        cmd->error_code = "bad_request";
        cmd->payload_json = 0;
        return;
    }
    ps = tr_engine_pipe_find(eng, instrument_id_of(self));
    po = tr_engine_pipe_find(eng, instrument_id_of(opp));
    pf = tr_engine_pipe_find(eng, instrument_id_of(fut));
    if (ps == 0 || po == 0 || pf == 0) {
        cmd->status = "rejected";
        cmd->error_code = "not_watched";
        cmd->payload_json = ps == 0 ? "{\"missing\":\"self\"}" : po == 0 ? "{\"missing\":\"opp\"}" : "{\"missing\":\"fut\"}";
        return;
    }
    nself = pair_copy_ring(g_pair_self, ps);
    nopp = pair_copy_ring(g_pair_opp, po);
    nfut = pair_copy_ring(g_pair_fut, pf);
    tr_wpair_init(&st, side, &cfg);
    tr_ksscore_default_config(&scfg, tr_engine_pipe_tick_scale(pf) / 100.0);
    if (scfg.values.price_scale <= 0.0) {
        scfg.values.price_scale = 0.05;
    }
    n = tr_wpair_replay(&st, g_pair_self, nself, g_pair_opp, nopp, g_pair_fut, nfut, &scfg, g_pair_ev,
                        PAIR_EVT_MAX);
    if (n < 0) {
        cmd->status = "rejected";
        cmd->error_code = "bad_request";
        cmd->payload_json = 0;
        return;
    }
    shown = n > PAIR_EVT_MAX ? PAIR_EVT_MAX : n;
    pct = st.now.book_total_pct;
    if (!isfinite(pct)) {
        pct = 0.0;
    }
    lname = pair_name_ok(st.now.order_name) ? st.now.order_name : "";
    off = snprintf(g_pair_json, sizeof(g_pair_json),
                   "{\"side\":%d,\"self\":\"%s\",\"opp\":\"%s\",\"fut\":\"%s\","
                   "\"bars\":%zu,\"oppBars\":%zu,\"futBars\":%zu,\"more\":%d,\"events\":[",
                   side, self, opp, fut, nself, nopp, nfut, n > shown ? 1 : 0);
    SNAP_CLAMP(g_pair_json, off);
    for (int i = 0; i < shown && off < (int)sizeof(g_pair_json) - 240; i++) {
        const tr_wpair_event_t *e = &g_pair_ev[i];
        const char *name = pair_name_ok(e->name) ? e->name : "";
        off += snprintf(g_pair_json + off, sizeof(g_pair_json) - (size_t)off,
                        "%s{\"t\":%lld,\"k\":%d,\"r\":%d,\"q\":%d,\"p\":%d,\"c\":%d,\"n\":\"%s\"}",
                        i > 0 ? "," : "", (long long)e->time_sec, e->kind, e->reason, (int)e->qty,
                        e->pending, (int)e->contracts, name);
        SNAP_CLAMP(g_pair_json, off);
    }
    off += snprintf(g_pair_json + off, sizeof(g_pair_json) - (size_t)off,
                    "],\"last\":{\"pos\":%d,\"contracts\":%d,\"pct\":%.10g,\"pending\":%d,"
                    "\"reason\":%d,\"name\":\"%s\"}}",
                    st.now.market_position, (int)st.now.contracts, pct,
                    st.now.order_kind != TR_WPAIR_ORDER_NONE ? 1 : 0, st.now.order_reason, lname);
    SNAP_CLAMP(g_pair_json, off);
    cmd->status = "applied";
    cmd->error_code = "none";
    cmd->payload_json = g_pair_json;
}

/* 보유 1분봉으로 위클리 네 지표의 선을 만든다. 브로커 주문은 내지 않는다. */
static void pair_plots_command(tr_ipc_command_t *cmd, const char *p) {
    tr_engine_t *eng = g_live_ctx.engine;
    char self[16], opp[16], fut[16];
    yyjson_doc *doc;
    yyjson_val *data;
    tr_pipeline_t *ps;
    tr_pipeline_t *po;
    tr_pipeline_t *pf;
    tr_wplot_config_t cfg;
    tr_ksscore_config_t scfg;
    tr_wplot_t st;
    size_t nself, nopp, nfut, nll, nlr, nsl, nsr;
    int64_t t0 = 0;
    int64_t t1 = 0;

    self[0] = opp[0] = fut[0] = '\0';
    if (eng == 0) {
        cmd->status = "rejected";
        cmd->error_code = "not_ready";
        cmd->payload_json = 0;
        return;
    }
    if (eng->cfg.timeframe_sec != 60) {
        cmd->status = "rejected";
        cmd->error_code = "not_1m";
        cmd->payload_json = 0;
        return;
    }
    doc = yyjson_read((char *)p, strlen(p), 0);
    data = doc != 0 ? yyjson_obj_get(yyjson_doc_get_root(doc), "data") : 0;
    if (data != 0) {
        yyjson_val *a = yyjson_obj_get(data, "self");
        yyjson_val *b = yyjson_obj_get(data, "opp");
        yyjson_val *c = yyjson_obj_get(data, "fut");
        if (yyjson_is_str(a)) {
            pair_code(self, yyjson_get_str(a));
        }
        if (yyjson_is_str(b)) {
            pair_code(opp, yyjson_get_str(b));
        }
        if (yyjson_is_str(c)) {
            pair_code(fut, yyjson_get_str(c));
        }
    }
    if (doc != 0) {
        yyjson_doc_free(doc);
    }
    if (strlen(self) < 4 || strlen(opp) < 4 || strlen(fut) < 4) {
        cmd->status = "rejected";
        cmd->error_code = "bad_request";
        cmd->payload_json = 0;
        return;
    }
    ps = tr_engine_pipe_find(eng, instrument_id_of(self));
    po = tr_engine_pipe_find(eng, instrument_id_of(opp));
    pf = tr_engine_pipe_find(eng, instrument_id_of(fut));
    if (ps == 0 || po == 0 || pf == 0) {
        cmd->status = "rejected";
        cmd->error_code = "not_watched";
        cmd->payload_json = ps == 0 ? "{\"missing\":\"self\"}" : po == 0 ? "{\"missing\":\"opp\"}" : "{\"missing\":\"fut\"}";
        return;
    }
    nself = pair_copy_ring(g_pair_self, ps);
    nopp = pair_copy_ring(g_pair_opp, po);
    nfut = pair_copy_ring(g_pair_fut, pf);
    tr_wplot_default_config(&cfg);
    tr_ksscore_default_config(&scfg, tr_engine_pipe_tick_scale(pf) / 100.0);
    if (scfg.values.price_scale <= 0.0) {
        scfg.values.price_scale = 0.05;
    }
    if (tr_wplot_init(&st, TR_WPLOT_LONG, &cfg) != 0 ||
        tr_wplot_replay(&st, g_pair_self, nself, g_pair_opp, nopp, g_pair_fut, nfut, &scfg, g_wplot_ll,
                        BB_STORE_MAX, &nll, g_wplot_lr, BB_STORE_MAX, &nlr) != 0 ||
        tr_wplot_init(&st, TR_WPLOT_SHORT, &cfg) != 0 ||
        tr_wplot_replay(&st, g_pair_self, nself, g_pair_opp, nopp, g_pair_fut, nfut, 0, g_wplot_sl,
                        BB_STORE_MAX, &nsl, g_wplot_sr, BB_STORE_MAX, &nsr) != 0) {
        cmd->status = "rejected";
        cmd->error_code = "bad_request";
        cmd->payload_json = 0;
        return;
    }
    if (nself > 0) {
        t0 = g_pair_self[0].open_us / INT64_C(1000000);
        t1 = g_pair_self[nself - 1].open_us / INT64_C(1000000);
    }
    if (tr_wplot_pack(g_wplot_json, sizeof(g_wplot_json), t0, t1, g_wplot_ll, nll, g_wplot_lr, nlr,
                      g_wplot_sl, nsl, g_wplot_sr, nsr) != 0) {
        cmd->status = "rejected";
        cmd->error_code = "payload_too_large";
        cmd->payload_json = 0;
        return;
    }
    cmd->status = "applied";
    cmd->error_code = "none";
    cmd->payload_json = g_wplot_json;
}

static int hit_strike_cmp(const void *a, const void *b) {
    const ls_instrument_info_t *ia = *(const ls_instrument_info_t *const *)a;
    const ls_instrument_info_t *ib = *(const ls_instrument_info_t *const *)b;
    double sa = 0, sb = 0;
    char ca = 0, cb = 0;
    ls_opt_parse_name(ia->name, &ca, 0, 0, &sa);
    ls_opt_parse_name(ib->name, &cb, 0, 0, &sb);
    if (sa < sb) {
        return -1;
    }
    if (sa > sb) {
        return 1;
    }
    return (ca == 'P') - (cb == 'P');
}

static int hit_ovs_rank(const char *shcode) {
    const ls_ovsfut_entry_t *e = ls_ovsfut_find(shcode);
    if (e == 0) {
        return 1000;
    }
    for (size_t i = 0; i < ls_ovsfut_count(); i++) {
        if (ls_ovsfut_at(i) == e) {
            return (int)i;
        }
    }
    return 1000;
}

static int hit_ovs_cmp(const void *a, const void *b) {
    const ls_instrument_info_t *ia = *(const ls_instrument_info_t *const *)a;
    const ls_instrument_info_t *ib = *(const ls_instrument_info_t *const *)b;
    int d = hit_ovs_rank(ia->shcode) - hit_ovs_rank(ib->shcode);
    if (d != 0) {
        return d;
    }
    return strcmp(ia->shcode, ib->shcode);
}

static void live_command_handler(void *ctx, tr_ipc_command_t *cmd) {
    (void)ctx;
    static char payload[512];
    const char *p = cmd->msg.payload;

    if (strstr(p, "\"type\":\"engine.stop\"") != 0) {
        g_stop = 1;
        cmd->status = "applied";
        cmd->error_code = "none";
        cmd->payload_json = "{\"stopping\":true}";
        return;
    }

    if (strstr(p, "\"type\":\"chart.cap\"") != 0) {
        /* 대시보드 상한 입력. 저장 상한 안으로 맞춘 뒤 관측 중인 종목을 그 용량으로
         * 다시 초기화하고 백필한다. 봉이 이미 있으면 더 과거 봉은 주입되지 않으므로
         * 링을 비우고 다시 채운다. */
        yyjson_doc *doc = yyjson_read((char *)p, strlen(p), 0);
        yyjson_val *data = doc != 0 ? yyjson_obj_get(yyjson_doc_get_root(doc), "data") : 0;
        yyjson_val *bv = data != 0 ? yyjson_obj_get(data, "bars") : 0;
        if (!yyjson_is_num(bv)) {
            if (doc != 0) {
                yyjson_doc_free(doc);
            }
            cmd->status = "rejected";
            cmd->error_code = "invalid_cap";
            cmd->payload_json = 0;
            return;
        }
        size_t cap = clamp_bar_cap((long)yyjson_get_num(bv));
        yyjson_doc_free(doc);
        g_bar_cap = cap;
        live_ctx_t *lc = &g_live_ctx;
        for (int i = 0; i < lc->watch_count; i++) {
            watch_entry_t *w = &lc->watches[i];
            tr_pipeline_t *pipe = tr_engine_pipe_find(lc->engine, w->instrument_id);
            if (pipe == 0) {
                continue;
            }
            if (!tr_engine_pipe_resize(lc->engine, pipe, cap)) {
                cmd->status = "rejected";
                cmd->error_code = "resize_failed";
                cmd->payload_json = 0;
                return;
            }
            int slot = watch_pool_of(pipe);
            tr_engine_pipe_attach_status_ring(lc->engine, w->instrument_id, g_status_pool[slot], cap);
            tr_engine_pipe_attach_market(lc->engine, w->instrument_id, g_mkt_pool[slot], MKT_POOL_CAP);
            bool rt_only = false;
            int nb = backfill_minute_bars(lc->auth, lc->engine, pipe, w->shcode, w->kind, &rt_only);
            w->rt_only = rt_only;
            (void)nb;
        }
        snprintf(payload, sizeof(payload), "{\"bars\":%zu}", cap);
        cmd->status = "applied";
        cmd->error_code = "none";
        cmd->payload_json = payload;
        return;
    }

    if (strstr(p, "\"type\":\"market.select\"") != 0) {
        yyjson_doc *doc = yyjson_read((char *)p, strlen(p), 0);
        yyjson_val *sh = doc != 0 ? yyjson_obj_get(yyjson_doc_get_root(doc), "data") : 0;
        sh = sh != 0 ? yyjson_obj_get(sh, "shcode") : 0;
        if (!yyjson_is_str(sh) || strlen(yyjson_get_str(sh)) >= sizeof(g_live_ctx.watches[0].shcode)) {
            if (doc != 0) {
                yyjson_doc_free(doc);
            }
            cmd->status = "rejected";
            cmd->error_code = "invalid_symbol";
            cmd->payload_json = 0;
            return;
        }
        const char *new_code = yyjson_get_str(sh);
        live_ctx_t *lc = &g_live_ctx;

        /* 전체 대체: 기존 워치를 모두 해지하고 파이프라인 0 하나만 남긴다
         * (전략 거래 대상과 무관한 화면 상태 변경) */
        for (int i = 0; i < lc->watch_count; i++) {
            tr_ls_rt_unsubscribe(lc->rt, lc->watches[i].tick_cd, lc->watches[i].shcode);
            tr_ls_rt_unsubscribe(lc->rt, lc->watches[i].ob_cd, lc->watches[i].shcode);
        }
        while (lc->engine->pipe_count > 1) {
            tr_engine_pipe_remove(lc->engine, lc->engine->pipes[1]->instrument_id);
        }

        uint64_t new_id = instrument_id_of(new_code);
        const char *new_name = 0;
        double new_tick = 0.0;
        ensure_master(lc);
        int new_kind = resolve_instrument(lc->master, new_code, &new_name, &new_tick);
        const char *new_tick_cd, *new_ob;
        rt_channels_for(new_kind, &new_tick_cd, &new_ob);
        tr_ls_rt_subscribe(lc->rt, new_tick_cd, new_code, new_id);
        tr_ls_rt_subscribe(lc->rt, new_ob, new_code, new_id);

        tr_session_policy_t new_sess = session_for(new_kind);
        tr_engine_select_symbol(lc->engine, new_id, kind_is_futures(new_kind), new_code,
                                &new_sess,
                                (new_kind == LS_MARKET_OVS_FUT || new_kind == LS_MARKET_KP200_OPT)
                                    ? new_tick
                                    : 0.0,
                                new_kind == LS_MARKET_OVS_FUT);
        /* 재초기화로 링이 끊기므로 다시 부착한다 (백필 주입 전에). 파이프라인 0이 쓰는
         * 풀 슬롯과 같은 슬롯의 상태/마켓 저장소를 부착한다 (pipe0 이식 후에도 정합) */
        int slot = watch_pool_of(lc->engine->pipes[0]);
        tr_engine_attach_status_ring(lc->engine, g_status_pool[slot], g_bar_cap);
        tr_engine_attach_market(lc->engine, g_mkt_pool[slot], MKT_POOL_CAP);
        lc->watch_count = 0;
        watch_entry_t *w = &lc->watches[lc->watch_count++];
        snprintf(w->shcode, sizeof(w->shcode), "%s", new_code);
        snprintf(w->tick_cd, sizeof(w->tick_cd), "%s", new_tick_cd);
        snprintf(w->ob_cd, sizeof(w->ob_cd), "%s", new_ob);
        w->kind = new_kind;
        w->rt_only = false;
        w->instrument_id = new_id;

        /* 새 종목도 기동 시와 같은 경로로 백필한다 (없으면 빈 차트로 시작한다) */
        bool rt_only = false;
        int nb = backfill_minute_bars(lc->auth, lc->engine, lc->engine->pipes[0], new_code,
                                      new_kind, &rt_only);
        w->rt_only = rt_only;

        snprintf(payload, sizeof(payload), "{\"shcode\":\"%s\",\"name\":\"%s\",\"generation\":%u,\"backfilled\":%d}",
                 new_code, new_name != 0 ? new_name : "", lc->engine->pipes[0]->generation,
                 nb > 0 ? nb : 0);
        cmd->status = "applied";
        cmd->error_code = "none";
        cmd->payload_json = payload;
        yyjson_doc_free(doc);
        return;
    }

    if (strstr(p, "\"type\":\"market.watch\"") != 0) {
        /* 다중 관측: 파이프라인 생성(없으면)+실시간 구독+백필. 이미 있으면 현 상태 응답 */
        yyjson_doc *doc = yyjson_read((char *)p, strlen(p), 0);
        yyjson_val *sh = doc != 0 ? yyjson_obj_get(yyjson_doc_get_root(doc), "data") : 0;
        sh = sh != 0 ? yyjson_obj_get(sh, "shcode") : 0;
        if (!yyjson_is_str(sh) || yyjson_get_str(sh)[0] == '\0' ||
            strlen(yyjson_get_str(sh)) >= sizeof(g_live_ctx.watches[0].shcode)) {
            if (doc != 0) {
                yyjson_doc_free(doc);
            }
            cmd->status = "rejected";
            cmd->error_code = "invalid_symbol";
            cmd->payload_json = 0;
            return;
        }
        const char *code = yyjson_get_str(sh);
        live_ctx_t *lc = &g_live_ctx;
        /* 종목 없이 기동하면 파이프라인 0이 비어 있다. 첫 watch가 그 칸을 채운다.
         * 빈 칸을 남긴 채 파이프를 추가하면 빈 종목 재백필이 계속된다. */
        if (lc->watch_count == 0 && lc->engine->pipes[0]->shcode[0] == '\0') {
            uint64_t new_id = instrument_id_of(code);
            const char *new_name = 0;
            double new_tick = 0.0;
            ensure_master(lc);
            int new_kind = resolve_instrument(lc->master, code, &new_name, &new_tick);
            const char *new_tick_cd, *new_ob;
            rt_channels_for(new_kind, &new_tick_cd, &new_ob);
            tr_session_policy_t new_sess = session_for(new_kind);
            if (!tr_engine_select_symbol(lc->engine, new_id, kind_is_futures(new_kind), code,
                                         &new_sess,
                                         (new_kind == LS_MARKET_OVS_FUT || new_kind == LS_MARKET_KP200_OPT)
                                             ? new_tick
                                             : 0.0,
                                         new_kind == LS_MARKET_OVS_FUT)) {
                cmd->status = "rejected";
                cmd->error_code = "invalid_symbol";
                cmd->payload_json = 0;
                yyjson_doc_free(doc);
                return;
            }
            int slot = watch_pool_of(lc->engine->pipes[0]);
            tr_engine_attach_status_ring(lc->engine, g_status_pool[slot], g_bar_cap);
            tr_engine_attach_market(lc->engine, g_mkt_pool[slot], MKT_POOL_CAP);
            tr_ls_rt_subscribe(lc->rt, new_tick_cd, code, new_id);
            tr_ls_rt_subscribe(lc->rt, new_ob, code, new_id);
            watch_entry_t *w = &lc->watches[lc->watch_count++];
            snprintf(w->shcode, sizeof(w->shcode), "%s", code);
            snprintf(w->tick_cd, sizeof(w->tick_cd), "%s", new_tick_cd);
            snprintf(w->ob_cd, sizeof(w->ob_cd), "%s", new_ob);
            w->kind = new_kind;
            w->rt_only = false;
            w->instrument_id = new_id;
            bool blank_rt_only = false;
            int nb = backfill_minute_bars(lc->auth, lc->engine, lc->engine->pipes[0], code,
                                          new_kind, &blank_rt_only);
            w->rt_only = blank_rt_only;
            snprintf(payload, sizeof(payload),
                     "{\"shcode\":\"%s\",\"name\":\"%s\",\"generation\":%u,\"backfilled\":%d}",
                     code, new_name != 0 ? new_name : "", lc->engine->pipes[0]->generation,
                     nb > 0 ? nb : 0);
            cmd->status = "applied";
            cmd->error_code = "none";
            cmd->payload_json = payload;
            yyjson_doc_free(doc);
            return;
        }
        uint64_t id = instrument_id_of(code);
        tr_pipeline_t *pipe = tr_engine_pipe_find(lc->engine, id);
        if (pipe != 0) {
            const char *name = 0;
            resolve_instrument(lc->master, code, &name, 0);
            snprintf(payload, sizeof(payload),
                     "{\"shcode\":\"%s\",\"name\":\"%s\",\"generation\":%u,\"backfilled\":0}",
                     code, name != 0 ? name : "", pipe->generation);
            cmd->status = "applied";
            cmd->error_code = "none";
            cmd->payload_json = payload;
            yyjson_doc_free(doc);
            return;
        }
        int slot = watch_pool_alloc(lc);
        if (lc->engine->pipe_count >= TR_ENGINE_MAX_PIPES || slot < 0) {
            cmd->status = "rejected";
            cmd->error_code = "watch_limit";
            cmd->payload_json = 0;
            yyjson_doc_free(doc);
            return;
        }
        ensure_master(lc);
        const char *name = 0;
        double tick = 0.0;
        int kind = resolve_instrument(lc->master, code, &name, &tick);
        tr_session_policy_t sess = session_for(kind);
        pipe = tr_engine_pipe_add(lc->engine, id, kind_is_futures(kind), code, &sess,
                                  (kind == LS_MARKET_OVS_FUT || kind == LS_MARKET_KP200_OPT) ? tick : 0.0,
                                  kind == LS_MARKET_OVS_FUT,
                                  g_bb_pool[slot], g_bar_cap, g_score_mid_pool[slot], 64);
        if (pipe == 0) {
            cmd->status = "rejected";
            cmd->error_code = "watch_limit";
            cmd->payload_json = 0;
            yyjson_doc_free(doc);
            return;
        }
        tr_engine_pipe_attach_status_ring(lc->engine, id, g_status_pool[slot], g_bar_cap);
        tr_engine_pipe_attach_market(lc->engine, id, g_mkt_pool[slot], MKT_POOL_CAP);
        const char *tick_cd, *ob_cd;
        rt_channels_for(kind, &tick_cd, &ob_cd);
        tr_ls_rt_subscribe(lc->rt, tick_cd, code, id);
        tr_ls_rt_subscribe(lc->rt, ob_cd, code, id);
        watch_entry_t *w = &lc->watches[lc->watch_count++];
        snprintf(w->shcode, sizeof(w->shcode), "%s", code);
        snprintf(w->tick_cd, sizeof(w->tick_cd), "%s", tick_cd);
        snprintf(w->ob_cd, sizeof(w->ob_cd), "%s", ob_cd);
        w->kind = kind;
        w->rt_only = false;
        w->instrument_id = id;

        bool rt_only = false;
        int nb = backfill_minute_bars(lc->auth, lc->engine, pipe, code, kind, &rt_only);
        w->rt_only = rt_only;

        snprintf(payload, sizeof(payload), "{\"shcode\":\"%s\",\"name\":\"%s\",\"generation\":%u,\"backfilled\":%d}",
                 code, name != 0 ? name : "", pipe->generation, nb > 0 ? nb : 0);
        cmd->status = "applied";
        cmd->error_code = "none";
        cmd->payload_json = payload;
        yyjson_doc_free(doc);
        return;
    }

    if (strstr(p, "\"type\":\"market.unwatch\"") != 0) {
        /* 관측 해지: 구독 해지+파이프라인 제거. 마지막 1개는 제거할 수 없다 */
        yyjson_doc *doc = yyjson_read((char *)p, strlen(p), 0);
        yyjson_val *sh = doc != 0 ? yyjson_obj_get(yyjson_doc_get_root(doc), "data") : 0;
        sh = sh != 0 ? yyjson_obj_get(sh, "shcode") : 0;
        if (!yyjson_is_str(sh) || yyjson_get_str(sh)[0] == '\0' ||
            strlen(yyjson_get_str(sh)) >= sizeof(g_live_ctx.watches[0].shcode)) {
            if (doc != 0) {
                yyjson_doc_free(doc);
            }
            cmd->status = "rejected";
            cmd->error_code = "invalid_symbol";
            cmd->payload_json = 0;
            return;
        }
        const char *code = yyjson_get_str(sh);
        live_ctx_t *lc = &g_live_ctx;
        int wi = watch_find(lc, code);
        if (wi < 0) {
            cmd->status = "rejected";
            cmd->error_code = "not_watched";
            cmd->payload_json = 0;
            yyjson_doc_free(doc);
            return;
        }
        if (lc->watch_count <= 1) {
            cmd->status = "rejected";
            cmd->error_code = "last_watch";
            cmd->payload_json = 0;
            yyjson_doc_free(doc);
            return;
        }
        watch_entry_t w = lc->watches[wi]; /* compact 전에 값을 보관한다 */
        /* 엔진부터 제거한다: 실패(목록 불일치)하면 구독·워치 항목을 그대로 둬야
         * 재시도가 가능하다. pipes[0] 대상이면 엔진이 마지막 파이프라인을 pipe0에
         * 이식한다 (engine.h 참조) */
        if (!tr_engine_pipe_remove(lc->engine, w.instrument_id)) {
            fprintf(stderr, "unwatch %s: engine pipeline missing (watch list inconsistent)\n",
                    w.shcode);
            cmd->status = "rejected";
            cmd->error_code = "engine_inconsistent";
            cmd->payload_json = 0;
            yyjson_doc_free(doc);
            return;
        }
        tr_ls_rt_unsubscribe(lc->rt, w.tick_cd, w.shcode);
        tr_ls_rt_unsubscribe(lc->rt, w.ob_cd, w.shcode);
        for (int j = wi; j + 1 < lc->watch_count; j++) {
            lc->watches[j] = lc->watches[j + 1];
        }
        lc->watch_count--;
        snprintf(payload, sizeof(payload), "{\"shcode\":\"%s\",\"watches\":%d}",
                 w.shcode, lc->watch_count);
        cmd->status = "applied";
        cmd->error_code = "none";
        cmd->payload_json = payload;
        yyjson_doc_free(doc);
        return;
    }

    if (strstr(p, "\"type\":\"chart.snapshot\"") != 0) {
        /* 늦게 접속한 대시보드의 과거 봉 시딩용. PUB/SUB는 과거 메시지를 보존하지 않으므로
         * 엔진의 봉 링에서 직접 돌려준다. data.back_index(최신 기준 건너뜀, 기본 0)로
         * 페이지를 나누고, 이어지면 next_back_index != 0 을 돌려준다 (페이지당 16봉, 오름차순).
         * 한 페이지는 IPC payload 상한(64KiB) 안에 있어야 한다. 넘으면 snprintf가 JSON을
         * 잘라 payload_raw로 나가고, 대시보드는 payload가 없다고 보고 차트를 비운다.
         * data.shcode로 대상 파이프라인을 고른다 (없으면 첫 파이프라인 — 구 호환).
         * 응답에는 봉·지표 외에 시간축 공백(gaps) 구간 목록도 실린다 — 대시보드가 빠진 분을
         * whitespace로 표시하는 데 쓴다 (docs/display_payload.md §2). */
        static char buf[256 * 1024];
        tr_engine_t *eng = g_live_ctx.engine;
        long back_index = 0;
        char want[16] = {0};
        yyjson_doc *doc = yyjson_read((char *)p, strlen(p), 0);
        if (doc != 0) {
            yyjson_val *data = yyjson_obj_get(yyjson_doc_get_root(doc), "data");
            yyjson_val *bv = data != 0 ? yyjson_obj_get(data, "back_index") : 0;
            yyjson_val *sv = data != 0 ? yyjson_obj_get(data, "shcode") : 0;
            if (yyjson_is_num(bv)) {
                back_index = (long)yyjson_get_num(bv);
            }
            if (yyjson_is_str(sv)) {
                snprintf(want, sizeof(want), "%s", yyjson_get_str(sv));
            }
            yyjson_doc_free(doc);
        }
        if (back_index < 0) {
            back_index = 0;
        }
        tr_pipeline_t *pipe = eng->pipes[0];
        if (want[0] != '\0') {
            pipe = tr_engine_pipe_find(eng, instrument_id_of(want));
            if (pipe == 0) {
                cmd->status = "rejected";
                cmd->error_code = "not_watched";
                cmd->payload_json = 0;
                return;
            }
        }
        uint64_t pipe_id = pipe->instrument_id;
        size_t n = tr_ring_count(&pipe->bb.bars);
        size_t from = (size_t)back_index;
        size_t remain = from < n ? n - from : 0;
        size_t take = remain < 16 ? remain : 16;
        size_t next = from + take < n ? from + take : 0;
        /* 시간축 공백(gaps): 같은 세션 안에서 이웃 봉의 시각 차가 timeframe을 넘는 구간을
         * [start_us, end_us] 쌍으로 수집한다 (tr_engine_pipe_find_gaps — 판별 규칙은 그
         * doc 주석 참조). 페이지 상한 150봉이면 경계 쌍을 포함해도 150구간을 넘지 않는다.
         * 실제 직렬화 길이(gaps_len)를 아래 각 섹션 루프의 가드 예약에 반영한다 */
        static tr_time_us_t gap_pairs[150][2];
        static char gaps_buf[8 + 150 * 44 + 3]; /* "\"gaps\":[" + 구간당 최악 44 + "]," + NUL */
        size_t ngaps = tr_engine_pipe_find_gaps(eng, pipe_id, from, take, gap_pairs, 150);
        int gaps_len = snprintf(gaps_buf, sizeof(gaps_buf), "\"gaps\":[");
        for (size_t gi = 0; gi < ngaps; gi++) {
            gaps_len += snprintf(gaps_buf + gaps_len, sizeof(gaps_buf) - (size_t)gaps_len,
                                 "%s[%lld,%lld]", gi > 0 ? "," : "",
                                 (long long)gap_pairs[gi][0], (long long)gap_pairs[gi][1]);
        }
        gaps_len += snprintf(gaps_buf + gaps_len, sizeof(gaps_buf) - (size_t)gaps_len, "],");
        SNAP_CLAMP(gaps_buf, gaps_len);
        int off = snprintf(buf, sizeof(buf),
                           "{\"shcode\":\"%s\",\"generation\":%u,\"timeframe_sec\":%u,\"total\":%zu,"
                           "\"next_back_index\":%zu,\"bars\":[",
                           pipe->shcode, pipe->generation, (unsigned)eng->cfg.timeframe_sec, n, next);
        SNAP_CLAMP(buf, off);
        bool first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 160 - SNAP_TAIL_FIXED - gaps_len;) {
            tr_candle_t c;
            tr_ring_at(&pipe->bb.bars, k, &c);
            off += snprintf(buf + off, sizeof(buf) - (size_t)off, "%s[%lld,%lld,%lld,%lld,%lld,%lld]",
                            first ? "" : ",", (long long)c.open_time_us,
                            (long long)c.open, (long long)c.high, (long long)c.low,
                            (long long)c.close, (long long)c.volume);
            SNAP_CLAMP(buf, off);
            first = false;
        }
        /* 봉별 지표(회귀·예측·점수·호가) — bars와 같은 순서. 스냅샷으로 과거 구간의
         * 미래곡선 보조지표도 복원하기 위한 값이다. 포맷은 tr_bar_status_format_ind가 소유한다.
         * [21..25]는 ⑤ 매매 상태와 reg_flat(회귀선 틱 반올림, 엔진 페이로드와 동일 규칙:
         * 선물 0.05pt×100=5 raw, 주식 1원×100=100 raw, 해외선물은 파이프라인 명시 틱).
         * [26]=틱 크기(raw, ④ 결과 띠 오프셋·⑧ 거리 기준에 사용), [27]=거래일(④ 세션 가드),
         * [28]=SMA 유효(5/20/60 모두 창 완성), [29..31]=SMA 5/20/60 (종가 기준) */
        double ps_flat = tr_engine_pipe_tick_scale(pipe);
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"ind\":[");
        SNAP_CLAMP(buf, off);
        first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 1100 - SNAP_TAIL_FIXED - gaps_len;) {
            tr_bar_status_t st;
            memset(&st, 0, sizeof(st));
            tr_engine_pipe_status_at(eng, pipe_id, k, &st);
            off += tr_bar_status_format_ind(&st, ps_flat, first, buf + off, sizeof(buf) - (size_t)off);
            SNAP_CLAMP(buf, off);
            first = false;
        }
        /* ⑥ 방향 기억 이벤트 (updated 또는 세션 리셋 봉, 창 안에서 오름차순).
         * 포맷은 tr_bar_status_format_mem이 소유한다 — 이벤트 끝의 reset 플래그로
         * 시딩 측이 세션 경계에서 진행 중 기억선 세트를 끊는다 */
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"mem\":[");
        SNAP_CLAMP(buf, off);
        first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 320 - SNAP_TAIL_FIXED - gaps_len;) {
            tr_bar_status_t st;
            memset(&st, 0, sizeof(st));
            tr_engine_pipe_status_at(eng, pipe_id, k, &st);
            int w = tr_bar_status_format_mem(&st, first, buf + off, sizeof(buf) - (size_t)off);
            if (w <= 0) {
                continue;
            }
            off += w;
            SNAP_CLAMP(buf, off);
            first = false;
        }
        /* ⑦ 지속 사진 저장 이벤트 (saved 봉만) */
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"pst\":[");
        SNAP_CLAMP(buf, off);
        first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 260 - SNAP_TAIL_FIXED - gaps_len;) {
            tr_bar_status_t st;
            memset(&st, 0, sizeof(st));
            tr_engine_pipe_status_at(eng, pipe_id, k, &st);
            if (!st.pst_saved) {
                continue;
            }
            off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                            "%s[%lld,%d,%d,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g]",
                            first ? "" : ",", (long long)st.open_time_us,
                            st.pst_valid ? 1 : 0, st.pst_dir,
                            st.pst_target[0], st.pst_target[1], st.pst_target[2],
                            st.pst_upper[0], st.pst_upper[1], st.pst_upper[2],
                            st.pst_lower[0], st.pst_lower[1], st.pst_lower[2]);
            SNAP_CLAMP(buf, off);
            first = false;
        }
        /* V3 표시 Plot. bars와 같은 순서. 국내 봉은 빈 배열. */
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"fx3\":[");
        SNAP_CLAMP(buf, off);
        first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 1400 - SNAP_TAIL_FIXED - gaps_len;) {
            tr_bar_status_t st;
            memset(&st, 0, sizeof(st));
            tr_engine_pipe_status_at(eng, pipe_id, k, &st);
            off += tr_bar_status_format_fx3(&st, first, buf + off, sizeof(buf) - (size_t)off);
            SNAP_CLAMP(buf, off);
            first = false;
        }
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"pgap\":[");
        SNAP_CLAMP(buf, off);
        first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 500 - SNAP_TAIL_FIXED - gaps_len;) {
            tr_bar_status_t st;
            memset(&st, 0, sizeof(st));
            tr_engine_pipe_status_at(eng, pipe_id, k, &st);
            off += snprintf(buf + off, sizeof(buf) - (size_t)off, "%s[", first ? "" : ",");
            SNAP_CLAMP(buf, off);
            for (int pi = 0; pi < 2; pi++) {
                off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                                "%s[%d,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%u,%u,%u,%u,%u,%u,%u]",
                                pi > 0 ? "," : "", st.pg_ready[pi],
                                st.pg_v[pi][0], st.pg_v[pi][1], st.pg_v[pi][2],
                                st.pg_v[pi][3], st.pg_v[pi][4], st.pg_v[pi][5],
                                st.pg_c4[pi], (unsigned)st.pg_w4[pi], st.pg_c5[pi],
                                (unsigned)st.pg_plot3[pi], (unsigned)st.pg_plot5[pi],
                                st.pg_union_rgb[pi], (unsigned)st.pg_union_w[pi]);
                SNAP_CLAMP(buf, off);
            }
            off += snprintf(buf + off, sizeof(buf) - (size_t)off, "]");
            SNAP_CLAMP(buf, off);
            first = false;
        }
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"rgap\":[");
        SNAP_CLAMP(buf, off);
        first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 280 - SNAP_TAIL_FIXED - gaps_len;) {
            tr_bar_status_t st;
            memset(&st, 0, sizeof(st));
            tr_engine_pipe_status_at(eng, pipe_id, k, &st);
            off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                            "%s[%d,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%u,%u,%u,%u,%u]",
                            first ? "" : ",", st.rg_ready,
                            st.rg_v[0], st.rg_v[1], st.rg_v[2], st.rg_v[3], st.rg_v[4], st.rg_v[5],
                            st.rg_c4, (unsigned)st.rg_w4, st.rg_c5,
                            (unsigned)st.rg_plot3, (unsigned)st.rg_plot5);
            SNAP_CLAMP(buf, off);
            first = false;
        }
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"mgap\":[");
        SNAP_CLAMP(buf, off);
        first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 280 - SNAP_TAIL_FIXED - gaps_len;) {
            tr_bar_status_t st;
            memset(&st, 0, sizeof(st));
            tr_engine_pipe_status_at(eng, pipe_id, k, &st);
            off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                            "%s[%d,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%u,%u,%u,%u,%u]",
                            first ? "" : ",", st.mg_ready,
                            st.mg_v[0], st.mg_v[1], st.mg_v[2], st.mg_v[3], st.mg_v[4], st.mg_v[5],
                            st.mg_c4, (unsigned)st.mg_w4, st.mg_c5,
                            (unsigned)st.mg_plot3, (unsigned)st.mg_plot5);
            SNAP_CLAMP(buf, off);
            first = false;
        }
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"ymae\":[");
        SNAP_CLAMP(buf, off);
        first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 320 - SNAP_TAIL_FIXED - gaps_len;) {
            tr_bar_status_t st;
            memset(&st, 0, sizeof(st));
            tr_engine_pipe_status_at(eng, pipe_id, k, &st);
            off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                            "%s[%d,%d,%.10g,%.10g,%.10g,%.10g,%d,%d,%d,%d,%.10g,%.10g,%.10g,%.10g,%d]",
                            first ? "" : ",", st.ym_pos, st.ym_prev_valid, st.ym_prev_hi, st.ym_prev_lo,
                            st.ym_two_hi, st.ym_two_lo, st.ym_show_h1, st.ym_show_h2, st.ym_show_h3,
                            st.ym_show_h4, st.ym_mark_h1, st.ym_mark_h2, st.ym_mark_h3, st.ym_mark_h4,
                            st.ym_show_first);
            SNAP_CLAMP(buf, off);
            first = false;
        }
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"sniper\":[");
        SNAP_CLAMP(buf, off);
        first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 220 - SNAP_TAIL_FIXED - gaps_len;) {
            tr_bar_status_t st;
            memset(&st, 0, sizeof(st));
            tr_engine_pipe_status_at(eng, pipe_id, k, &st);
            off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                            "%s[%d,%d,%d,%d,%d,%.10g,%.10g,%u,%d,%d,%d,%d]",
                            first ? "" : ",", st.sn_score, st.sn_ex, st.sn_ratio, st.sn_stage,
                            st.sn_compound, st.sn_tgt, st.sn_px, st.sn_rgb, st.sn_px_exit,
                            st.sn_below, st.sn_above, st.sn_reset);
            SNAP_CLAMP(buf, off);
            first = false;
        }
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"os\":[");
        SNAP_CLAMP(buf, off);
        first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 420 - SNAP_TAIL_FIXED - gaps_len;) {
            tr_bar_status_t st;
            memset(&st, 0, sizeof(st));
            tr_engine_pipe_status_at(eng, pipe_id, k, &st);
            off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                            "%s[%d,%u,%d,%d,%d,%d,%d,%d,%u,%d,%d,%u,%d,%.10g,%u,%d,%d,%u,%d,%d,%d,%u,%d,%d,%d,%u,%d]",
                            first ? "" : ",", st.os_judge, st.os_judge_rgb, st.os_fut, st.os_prof,
                            st.os_di, st.os_adx, st.os_sq_on, st.os_sq_len, st.os_sq_rgb, st.os_sq_w,
                            st.os_hold, st.os_hold_rgb, st.os_ratio_on, st.os_ratio, st.os_ratio_rgb,
                            st.os_rel_on, st.os_rel_len, st.os_rel_rgb, st.os_rel_w,
                            st.os_cf_on, st.os_cf_len, st.os_cf_rgb, st.os_cf_w,
                            st.os_ent_on, st.os_ent_y, st.os_ent_rgb, st.os_ent_w);
            SNAP_CLAMP(buf, off);
            first = false;
        }
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"cu\":[");
        SNAP_CLAMP(buf, off);
        first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 1800 - SNAP_TAIL_FIXED - gaps_len;) {
            tr_bar_status_t st;
            memset(&st, 0, sizeof(st));
            tr_engine_pipe_status_at(eng, pipe_id, k, &st);
            off += snprintf(buf + off, sizeof(buf) - (size_t)off, "%s[", first ? "" : ",");
            SNAP_CLAMP(buf, off);
            for (int i = 0; i < st.cu_n && i < TR_FXCU_PLOTS; i++) {
                off += snprintf(buf + off, sizeof(buf) - (size_t)off, "%s[%u,%.10g,%u,%u]",
                                i > 0 ? "," : "", (unsigned)st.cu_id[i], st.cu_v[i], st.cu_rgb[i],
                                (unsigned)st.cu_w[i]);
                SNAP_CLAMP(buf, off);
            }
            off += snprintf(buf + off, sizeof(buf) - (size_t)off, "]");
            SNAP_CLAMP(buf, off);
            first = false;
        }
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"snco\":[");
        SNAP_CLAMP(buf, off);
        first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 1200 - SNAP_TAIL_FIXED - gaps_len;) {
            tr_bar_status_t st;
            memset(&st, 0, sizeof(st));
            tr_engine_pipe_status_at(eng, pipe_id, k, &st);
            off += snprintf(buf + off, sizeof(buf) - (size_t)off, "%s[", first ? "" : ",");
            SNAP_CLAMP(buf, off);
            for (int i = 0; i < st.snco_n && i < TR_SNCO_PLOTS; i++) {
                off += snprintf(buf + off, sizeof(buf) - (size_t)off, "%s[%u,%.10g,%u,%u]",
                                i > 0 ? "," : "", (unsigned)st.snco_id[i], st.snco_v[i], st.snco_rgb[i],
                                (unsigned)st.snco_w[i]);
                SNAP_CLAMP(buf, off);
            }
            off += snprintf(buf + off, sizeof(buf) - (size_t)off, "]");
            SNAP_CLAMP(buf, off);
            first = false;
        }
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"pvc\":[");
        SNAP_CLAMP(buf, off);
        first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 120 - SNAP_TAIL_FIXED - gaps_len;) {
            tr_bar_status_t st;
            memset(&st, 0, sizeof(st));
            tr_engine_pipe_status_at(eng, pipe_id, k, &st);
            off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                            "%s[%d,%.10g,%d,%.10g,%d]",
                            first ? "" : ",", st.pvc_price_on, st.pvc_price, st.pvc_vol_on,
                            st.pvc_vol, st.pvc_both);
            SNAP_CLAMP(buf, off);
            first = false;
        }
        /* 매니페스트는 고정 길이 — 루프 가드들이 SNAP_TAIL_FIXED + gaps_len만큼 남겨 두므로,
         * 각 항목이 추정 최악 크기 안에 든 경우에 한해 gaps 섹션·매니페스트가 잘리지 않고
         * 온전히 쓰인다 */
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],%s%s%s", gaps_buf, SNAP_IND_MANIFEST_A,
                        SNAP_IND_MANIFEST_B);
        SNAP_CLAMP(buf, off);
        cmd->status = "applied";
        cmd->error_code = "none";
        cmd->payload_json = buf;
        return;
    }

    if (strstr(p, "\"type\":\"pair.sim\"") != 0) {
        pair_sim_command(cmd, p);
        return;
    }

    if (strstr(p, "\"type\":\"pair.plots\"") != 0) {
        pair_plots_command(cmd, p);
        return;
    }

    if (strstr(p, "\"type\":\"market.instruments\"") != 0) {
        /* 검색: data.q, data.limit(기본 50, 종류 없으면 최대 100).
         * data.kind(0주식 1국내선물 2해외선물 3지수옵션)가 있으면 그 종류만.
         * 옵션은 data.expiry가 비면 만기 키만, 있으면 그 만기의 콜·풋. */
        static const ls_instrument_info_t *hits[1024];
        static char buf[60 * 1024];
        static char expiries[80][24];
        if (!ensure_master(&g_live_ctx)) {
            cmd->status = "rejected";
            cmd->error_code = "registry_unavailable";
            cmd->payload_json = 0;
            return;
        }
        char qbuf[80];
        char expiry[24];
        qbuf[0] = 0;
        expiry[0] = 0;
        long limit = 50;
        int kind = -1;
        yyjson_doc *doc = yyjson_read((char *)p, strlen(p), 0);
        if (doc != 0) {
            yyjson_val *data = yyjson_obj_get(yyjson_doc_get_root(doc), "data");
            yyjson_val *qv = data != 0 ? yyjson_obj_get(data, "q") : 0;
            yyjson_val *lv = data != 0 ? yyjson_obj_get(data, "limit") : 0;
            yyjson_val *kv = data != 0 ? yyjson_obj_get(data, "kind") : 0;
            yyjson_val *ev = data != 0 ? yyjson_obj_get(data, "expiry") : 0;
            if (yyjson_is_str(qv)) {
                snprintf(qbuf, sizeof(qbuf), "%s", yyjson_get_str(qv));
            }
            if (yyjson_is_num(lv)) {
                limit = (long)yyjson_get_num(lv);
            }
            if (yyjson_is_num(kv)) {
                kind = (int)yyjson_get_num(kv);
            }
            if (yyjson_is_str(ev)) {
                snprintf(expiry, sizeof(expiry), "%s", yyjson_get_str(ev));
            }
            yyjson_doc_free(doc);
        }
        if (kind < -1 || kind > 3) {
            kind = -1;
        }
        long max_limit = kind < 0 ? 100 : (kind == 3 && expiry[0] != 0 ? 1024 : 200);
        if (limit < 1) {
            limit = 50;
        }
        if (limit > max_limit) {
            limit = max_limit;
        }
        size_t count = ls_master_count(g_live_ctx.master);
        size_t nexp = 0;
        size_t n = 0;
        if (kind == 3 && expiry[0] == 0 && qbuf[0] == 0) {
            nexp = ls_master_option_expiries(g_live_ctx.master, &expiries[0][0], 24,
                                             sizeof(expiries) / sizeof(expiries[0]));
        } else if (kind < 0) {
            n = ls_master_search(g_live_ctx.master, qbuf[0] != 0 ? qbuf : 0, hits, (size_t)limit);
        } else {
            n = ls_master_collect(g_live_ctx.master, kind, qbuf[0] != 0 ? qbuf : 0,
                                  expiry[0] != 0 ? expiry : 0, hits, (size_t)limit);
            if (kind == 3 && n > 1) {
                qsort(hits, n, sizeof(hits[0]), hit_strike_cmp);
            } else if (kind == 2 && n > 1) {
                qsort(hits, n, sizeof(hits[0]), hit_ovs_cmp);
            }
        }
        int off = snprintf(buf, sizeof(buf), "{\"total\":%zu,\"returned\":%zu,\"items\":[", count, n);
        SNAP_CLAMP(buf, off);
        size_t written = 0;
        for (size_t i = 0; i < n && off < (int)sizeof(buf) - 240; i++) {
            const ls_instrument_info_t *it = hits[i];
            int fut_flag = it->market == LS_MARKET_OVS_FUT    ? 2
                           : it->market == LS_MARKET_KP200_OPT ? 3
                           : it->is_futures                    ? 1
                                                               : 0;
            char cp = 0;
            double strike = 0;
            bool opt = fut_flag == 3 && ls_opt_parse_name(it->name, &cp, 0, 0, &strike);
            char name_esc[128];
            size_t ne = 0;
            for (const char *s = it->name; *s != 0 && ne + 2 < sizeof(name_esc); s++) {
                if (*s == '"' || *s == '\\') {
                    name_esc[ne++] = '\\';
                }
                name_esc[ne++] = *s;
            }
            name_esc[ne] = 0;
            off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                            "%s{\"shcode\":\"%s\",\"name\":\"%s\",\"fut\":%d",
                            written > 0 ? "," : "", it->shcode, name_esc, fut_flag);
            SNAP_CLAMP(buf, off);
            if (opt) {
                off += snprintf(buf + off, sizeof(buf) - (size_t)off, ",\"cp\":\"%c\",\"strike\":%.10g",
                                cp, strike);
                SNAP_CLAMP(buf, off);
            }
            off += snprintf(buf + off, sizeof(buf) - (size_t)off, "}");
            SNAP_CLAMP(buf, off);
            written++;
        }
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"expiries\":[");
        SNAP_CLAMP(buf, off);
        for (size_t i = 0; i < nexp && off < (int)sizeof(buf) - 40; i++) {
            off += snprintf(buf + off, sizeof(buf) - (size_t)off, "%s\"%s\"",
                            i > 0 ? "," : "", expiries[i]);
            SNAP_CLAMP(buf, off);
        }
        /* returned는 버퍼에 실제로 넣은 개수다 */
        snprintf(buf + off, sizeof(buf) - (size_t)off, "]}");
        /* returned를 쓴 개수로 고친다. 앞쪽 숫자는 자릿수가 바뀔 수 있어 항목 수로만 맞춘다. */
        if (written != n) {
            char head[64];
            int hlen = snprintf(head, sizeof(head), "{\"total\":%zu,\"returned\":%zu,\"items\":[",
                                count, written);
            if (hlen > 0 && hlen < (int)sizeof(head)) {
                const char *items = strstr(buf, "\"items\":[");
                if (items != 0) {
                    size_t tail = strlen(items);
                    if ((size_t)hlen + tail < sizeof(buf)) {
                        memmove(buf + hlen, items, tail + 1);
                        memcpy(buf, head, (size_t)hlen);
                    }
                }
            }
        }
        cmd->status = "applied";
        cmd->error_code = "none";
        cmd->payload_json = buf;
        return;
    }

    {
        int off = snprintf(payload, sizeof(payload), "{\"mode\":\"live\",\"running\":%d,\"pid\":%ld,\"shcode\":\"%s\",\"watches\":[",
                           (int)g_running, self_pid(),
                           g_live_ctx.engine != 0 ? g_live_ctx.engine->pipes[0]->shcode : "");
        SNAP_CLAMP(payload, off);
        for (int i = 0; i < g_live_ctx.watch_count && off < (int)sizeof(payload) - 20; i++) {
            off += snprintf(payload + off, sizeof(payload) - (size_t)off, "%s\"%s\"",
                            i > 0 ? "," : "", g_live_ctx.watches[i].shcode);
            SNAP_CLAMP(payload, off);
        }
        snprintf(payload + off, sizeof(payload) - (size_t)off, "]}");
    }
    cmd->status = "applied";
    cmd->error_code = "none";
    cmd->payload_json = payload;
}

static int run_live(const char *shcode, bool is_fut, const char *cmd_ep, const char *pub_ep,
                    const char *pidfile) {
    /* 1) 인증 */
    ls_auth_t auth;
    ls_auth_init(&auth, 0);
    const char *token = 0;
    if (!ls_auth_ensure(&auth, &token)) {
        fprintf(stderr, "error: LS auth: %s\n", auth.last_error);
        return 3;
    }

    /* 1-1) 종목 레지스트리 (t8436 + t8467 + t8433 + t8435 WK + o3101 + 해외선물 정적 표). 실패 시 추정으로 계속한다 */
    char merr[128] = {0};
    tr_ls_master_t *master = 0;
    for (int attempt = 0; attempt < 3 && master == 0; attempt++) {
        if (attempt > 0) {
            fprintf(stderr, "instrument master retry %d\n", attempt + 1);
            sleep_ms(1000);
        }
        master = ls_master_fetch(&auth, merr, sizeof(merr));
    }
    if (master == 0) {
        fprintf(stderr, "instrument master unavailable: %s (fallback to heuristic)\n", merr);
    }
    if (shcode == 0) {
        shcode = "";
    }
    const bool have_symbol = shcode[0] != '\0';
    const char *found_name = 0;
    double tick_raw = 0.0;
    int kind = LS_MARKET_KOSPI;
    if (have_symbol) {
        kind = resolve_instrument(master, shcode, &found_name, &tick_raw);
        if (kind == LS_MARKET_KOSPI && is_fut) {
            kind = LS_MARKET_KP200_FUT; /* CLI --live-fut 강제 (마스터 실패·추정 시에만 의미) */
        }
    }
    if (master != 0) {
        if (have_symbol) {
            printf("instruments: %zu registered, %s=%s (%s)\n", ls_master_count(master), shcode,
                   kind == LS_MARKET_OVS_FUT        ? "OVS"
                   : kind == LS_MARKET_KP200_FUT ? "FUT"
                   : kind == LS_MARKET_KP200_OPT ? "OPT"
                                                 : "STK",
                   found_name != 0 ? found_name : "unknown");
        } else {
            printf("instruments: %zu registered\n", ls_master_count(master));
        }
    }

    /* 2) IPC */
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
        return 3;
    }
    /* bind 성공 직후에 쓴다 — bind 실패 엔진은 실행 중 엔진의 파일을 덮어쓰지 않는다.
     * 백필 묵병 구간은 이 이후라 여전히 커버된다 */
    pidfile_write(pidfile);

    /* 3) 엔진 */
    tr_engine_config_t ecfg;
    memset(&ecfg, 0, sizeof(ecfg));
    ecfg.engine_instance_id = engine_instance_id;
    ecfg.instrument_id = instrument_id_of(shcode);
    snprintf(ecfg.shcode, sizeof(ecfg.shcode), "%s", shcode);
    ecfg.session = session_for(kind);
    ecfg.timeframe_sec = 60;
    ecfg.no_trade = TR_NO_TRADE_SKIP;
    ecfg.is_futures = kind_is_futures(kind);
    /* 해외선물·지수옵션만 틱 크기를 명시한다 (국내 주식·선물은 0 = 자동) */
    ecfg.tick_raw = (kind == LS_MARKET_OVS_FUT || kind == LS_MARKET_KP200_OPT) ? tick_raw : 0.0;
    ecfg.is_ovs = kind == LS_MARKET_OVS_FUT;
    ecfg.predict_bars[0] = 5;
    ecfg.predict_bars[1] = 10;
    ecfg.predict_bars[2] = 15;
    ecfg.htf_ticks = 10;
    ecfg.min_r2 = 0.40;
    ecfg.market_period = 20;
    ecfg.daily_reg_period = 10;
    ecfg.gap_mid = 0.35;
    ecfg.gap_big = 0.75;
    ecfg.big_gap_reeval_min = 30;
    ecfg.min_final_strength = 40;

    tr_engine_t engine;
    if (!tr_engine_init(&engine, &ecfg, g_bb_storage, g_bar_cap, g_score_mid, 64)) {
        fprintf(stderr, "error: engine init failed\n");
        tr_ipc_close(ipc);
        return 3;
    }
    tr_engine_attach_ipc(&engine, ipc, "display");
    tr_engine_attach_status_ring(&engine, g_status_storage, g_bar_cap);
    tr_engine_attach_market(&engine, g_mkt_storage, sizeof(g_mkt_storage) / sizeof(g_mkt_storage[0]));

#ifdef TR_HAS_STORE
    {
        const char *db_path = getenv("NEXUS_DB");
        if (db_path == 0 || db_path[0] == '\0') {
            db_path = "engine.db";
        }
        char serr[256] = {0};
        if (tr_live_bars_open(db_path, serr, sizeof(serr)) != 0) {
            fprintf(stderr, "bar cache disabled: %s\n", serr[0] ? serr : "open failed");
        }
        tr_engine_set_candle_hook(&engine, persist_closed_bar, 0);
    }
#endif

    /* 4) 워밍업 백필 (market select/watch 시에도 같은 경로로 다시 채운다).
     * 이 구간은 길다. 페이지 사이에서 status에 답해야 traderctl이 60초에 죽이지 않는다. */
    bool rt_only = false;
    g_boot_ipc = ipc;
    if (have_symbol) {
        int nb = backfill_minute_bars(&auth, &engine, engine.pipes[0], shcode, kind, &rt_only);
        if (nb > 0) {
            printf("backfill: %d bars\n", nb);
            fflush(stdout);
        }
    }
    g_boot_ipc = 0;

    /* 5) 실시간 구독 */
    ls_rt_config_t rcfg;
    memset(&rcfg, 0, sizeof(rcfg));
    rcfg.auth = &auth;
    rcfg.queue_capacity = 512;
    rcfg.reconnect_min_ms = 1000;
    rcfg.reconnect_max_ms = 15000;
    tr_ls_rt_t *rt = tr_ls_rt_open(&rcfg, err, sizeof(err));
    if (rt == 0) {
        fprintf(stderr, "error: realtime open failed: %s\n", err);
        tr_ipc_close(ipc);
#ifdef TR_HAS_STORE
        tr_live_bars_close();
#endif
        return 3;
    }
    /* 주식은 S3_/H1_, 선물은 주간 FC9/FH9·야간 DC0/DH0, 옵션은 OC0/OH0, 해외선물은 OVC/OVH */
    const char *tr_cd = "";
    const char *ob_tr_cd = "";
    if (have_symbol) {
        rt_channels_for(kind, &tr_cd, &ob_tr_cd);
        if (!tr_ls_rt_subscribe(rt, tr_cd, shcode, ecfg.instrument_id) ||
            !tr_ls_rt_subscribe(rt, ob_tr_cd, shcode, ecfg.instrument_id)) {
            fprintf(stderr, "error: subscribe failed\n");
            tr_ls_rt_close(rt);
            tr_ipc_close(ipc);
#ifdef TR_HAS_STORE
            tr_live_bars_close();
#endif
            return 3;
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    g_running = 1;
    g_stop = 0;
    /* 종목을 준 기동만 첫 워치(파이프라인 0)로 둔다. 없으면 대시보드 watch가 채운다. */
    g_live_ctx.watch_count = 0;
    if (have_symbol) {
        g_live_ctx.watch_count = 1;
        snprintf(g_live_ctx.watches[0].shcode, sizeof(g_live_ctx.watches[0].shcode), "%s", shcode);
        snprintf(g_live_ctx.watches[0].tick_cd, sizeof(g_live_ctx.watches[0].tick_cd), "%s", tr_cd);
        snprintf(g_live_ctx.watches[0].ob_cd, sizeof(g_live_ctx.watches[0].ob_cd), "%s", ob_tr_cd);
        g_live_ctx.watches[0].kind = kind;
        g_live_ctx.watches[0].rt_only = rt_only;
        g_live_ctx.watches[0].instrument_id = ecfg.instrument_id;
    }
    g_live_ctx.rt = rt;
    g_live_ctx.engine = &engine;
    g_live_ctx.master = master;
    g_live_ctx.auth = &auth;

    if (have_symbol) {
        printf("live %s %s: streaming (stop with Ctrl+C or 'traderctl engine stop')\n",
               kind == LS_MARKET_OVS_FUT        ? "OVS"
               : kind == LS_MARKET_KP200_FUT ? "FUT"
               : kind == LS_MARKET_KP200_OPT ? "OPT"
                                             : "STK",
               shcode);
    } else {
        printf("live: no symbol. Open one in the dashboard (stop with Ctrl+C or 'traderctl engine stop')\n");
    }

    int rc = 0;
    /* 직전 RT 이벤트 수신 시각. 0이면 기동 후 첫 이벤트 전 — 기동 백필과 중복 캐치업하지 않는다 */
    int64_t last_rt_event_us = 0;
    bool need_catchup = false;
    /* 마지막 빈 링 재백필 검사 시각. watch 백필이 연속 실패하면(HTTP 500 등) 링이 빈 채로
     * 영구 방치되는 사고(2026-10-01 삼성전자 반나절 빈 차트)를 막기 위해 60초마다
     * 빈 파이프를 다시 백필한다 */
    time_t last_backfill_retry = time(0);
    while (!g_stop) {
        tr_ls_rt_service(rt, 20);
        ls_rt_event_t ev;
        while (tr_ls_rt_next(rt, &ev)) {
            /* 공백-재개 감지: 직전 이벤트와 120초 이상 끊겼으면 큐 소비 후 캐치업한다
             * (재연결만으로는 공백 구간의 봉이 복구되지 않는다 — 2026-09-30 사건) */
            if (last_rt_event_us != 0 &&
                ev.recv_time_us - last_rt_event_us > RT_CATCHUP_GAP_US) {
                need_catchup = true;
            }
            last_rt_event_us = ev.recv_time_us;
            /* instrument_id로 파이프라인을 찾아 라우팅한다. 미관측 id는 엔진이 드롭한다
             * (구독 해지된 채널의 지연 메시지) */
            if (ev.kind == LS_RT_TICK) {
                tr_event_envelope_t env;
                memset(&env, 0, sizeof(env));
                env.kind = TR_EVENT_TICK;
                env.event_time_us = ev.event_time_us;
                env.received_time_us = ev.recv_time_us;
                tr_tick_t tk;
                memset(&tk, 0, sizeof(tk));
                tk.instrument_id = ev.instrument_id;
                tk.price = ev.price;
                tk.qty = ev.qty;
                tk.source_exec_id = 0; /* 실시간 채널은 안정적인 체결 ID 미확인 — 중복 제거 한계 기록 */
                tk.volume_meaning = ev.volume_meaning;
                tr_engine_on_tick(&engine, &env, &tk);
                tr_engine_on_timer(&engine, ev.event_time_us);
            } else if (ev.kind == LS_RT_ORDERBOOK) {
                tr_engine_on_orderbook(&engine, ev.instrument_id, ev.event_time_us,
                                       (double)ev.bid_total, (double)ev.ask_total);
            }
        }
        if (need_catchup) {
            need_catchup = false;
            rt_catchup_missing_bars(&g_live_ctx, (tr_time_us_t)time(0) * TR_US_PER_SEC);
            /* 캐치업은 블로킹이라 수백 초 걸릴 수 있다 — 경과 시간이 다음 이벤트의
             * 공백으로 재감지되어 재트리거되는 루프를 막는다 */
            last_rt_event_us = (int64_t)time(0) * TR_US_PER_SEC;
        }
        /* 빈 봉 링 재백필: 60초마다 한 번, 봉 링이 빈 관측 파이프를 watch 백필과 같은
         * 경로로 다시 채운다. 주말·거래정지처럼 구조적으로 실패하는 경우도 60초 주기
         * 로그 1줄이라 그대로 둔다 (조용한 실패 허용).
         * RT-only(해외선물 백필 차단) 종목은 제외한다 — 재시도핏 빈 응답이라 로그만
         * 늘어난다. 권한이 풀리면 수동 unwatch/watch로 즉시 백필된다 */
        time_t now_sec = time(0);
        if (now_sec - last_backfill_retry >= 60) {
            last_backfill_retry = now_sec;
            bool retried = false;
            for (int i = 0; i < engine.pipe_count; i++) {
                tr_pipeline_t *pipe = engine.pipes[i];
                if (tr_ring_count(&pipe->bb.bars) != 0 || pipe->shcode[0] == '\0') {
                    continue;
                }
                int wi = watch_find_by_id(&g_live_ctx, pipe->instrument_id);
                if (wi >= 0 && g_live_ctx.watches[wi].rt_only) {
                    continue;
                }
                int kind = wi >= 0 ? g_live_ctx.watches[wi].kind
                                   : pipe->is_futures ? LS_MARKET_KP200_FUT : LS_MARKET_KOSPI;
                int nb = backfill_minute_bars(&auth, &engine, pipe, pipe->shcode, kind, 0);
                if (nb > 0) {
                    fprintf(stderr, "backfill retry %s: %d bars\n", pipe->shcode, nb);
                } else {
                    fprintf(stderr, "backfill retry %s: still empty\n", pipe->shcode);
                }
                retried = true;
            }
            if (retried) {
                /* 재백필도 블로킹이라 수 초 걸릴 수 있다 — 경과 시간이 다음 이벤트의
                 * 공백으로 재감지되어 캐치업이 재트리거되는 루프를 막는다 (캐치업과 동일).
                 * 실제 백필이 없을 때도 갱신하면 진짜 공백-재개 감지가 묻히므로 실행 때만 한다 */
                last_rt_event_us = (int64_t)time(0) * TR_US_PER_SEC;
            }
        }
        tr_ipc_poll(ipc, 0, 4, live_command_handler, 0);
        if (tr_ls_rt_state(rt) == LS_RT_FAILED) {
            fprintf(stderr, "error: realtime failed\n");
            rc = 1;
            break;
        }
    }
    g_running = 0;
    printf("stopping: %llu status messages published\n",
           (unsigned long long)(engine.status_seq - 1));

    tr_ls_rt_close(rt);
    tr_ipc_close(ipc);
    ls_master_free(master);
#ifdef TR_HAS_STORE
    tr_live_bars_close();
#endif
    return rc;
}

#ifndef TRADING_ENGINE_VERSION
#define TRADING_ENGINE_VERSION "0.0.0-dev"
#endif

static void print_usage(const char *prog) {
    printf("Nexus Trading Engine %s\n", TRADING_ENGINE_VERSION);
    printf("\n");
    printf("Usage: %s [options]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help              Show this help and exit\n");
    printf("  -v, --version           Show version and exit\n");
    printf("      --data-dir DIR      Engine data directory (reserved; not used yet)\n");
    printf("      --replay FILE       Replay CSV ticks (epoch_us,price,qty) and publish status\n");
    printf("      --replay-delay MS   Slow replay: delay between ticks (default 0)\n");
    printf("      --live [SHCODE]     Live mode. Omit SHCODE to start with no symbol;\n");
    printf("                          open symbols with a dashboard watch\n");
    printf("      --live-fut SHCODE   Live mode: subscribe realtime ticks for futures SHCODE\n");
    printf("      --cmd-endpoint EP   Command endpoint (default tcp://127.0.0.1:5555)\n");
    printf("      --pub-endpoint EP   Status stream endpoint (default tcp://127.0.0.1:5556)\n");
    printf("      --pidfile PATH      Write own pid to PATH (default /tmp/trading-engine-<cmdport>.pid)\n");
    printf("\n");
    printf("Replay mode requires no API credentials.\n");
}

static void print_version(void) {
    printf("trading-engine %s\n", TRADING_ENGINE_VERSION);
}

static int run_replay(const char *path, const char *cmd_ep, const char *pub_ep, int delay_ms,
                      const char *pidfile) {
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
    pidfile_write(pidfile); /* live와 같게 bind 성공 직후 */

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
    ecfg.daily_reg_period = 10;
    ecfg.gap_mid = 0.35;
    ecfg.gap_big = 0.75;
    ecfg.big_gap_reeval_min = 30;
    ecfg.min_final_strength = 40;

    tr_engine_t engine;
    if (!tr_engine_init(&engine, &ecfg, g_bb_storage, g_bar_cap, g_score_mid, 64)) {
        fprintf(stderr, "error: engine init failed\n");
        tr_ipc_close(ipc);
        tr_csv_ticks_free(ticks);
        return 3;
    }
    tr_engine_attach_ipc(&engine, ipc, "display");
    tr_engine_attach_status_ring(&engine, g_status_storage, g_bar_cap);
    tr_engine_attach_market(&engine, g_mkt_storage, sizeof(g_mkt_storage) / sizeof(g_mkt_storage[0]));

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
    const char *live_shcode = 0;
    bool live_mode = false;
    bool live_fut = false;
    const char *cmd_ep = "tcp://127.0.0.1:5555";
    const char *pub_ep = "tcp://127.0.0.1:5556";
    const char *pidfile = 0; /* 미지정 시 명령 엔드포인트 포트에서 유도 */
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
        if (strcmp(argv[i], "--live") == 0) {
            live_mode = true;
            live_fut = false;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                live_shcode = argv[++i];
            }
            continue;
        }
        if (strcmp(argv[i], "--live-fut") == 0 && i + 1 < argc) {
            live_shcode = argv[++i];
            live_fut = true;
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
        if (strcmp(argv[i], "--pidfile") == 0 && i + 1 < argc) {
            pidfile = argv[++i];
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

    if (replay_file == 0 && !live_mode && live_shcode == 0) {
        fprintf(stderr, "error: no mode specified; see --help\n");
        return 1;
    }
    if (live_mode && live_shcode == 0) {
        live_shcode = "";
    }

    /* 기본 pid 파일은 명령 엔드포인트 포트로 유도 — 다른 포트의 엔진과 파일이 갈린다 */
    char pidfile_buf[320];
    if (pidfile == 0) {
        default_pid_path(pidfile_buf, sizeof(pidfile_buf), cmd_port_of(cmd_ep));
        pidfile = pidfile_buf;
    }
    int rc = replay_file != 0 ? run_replay(replay_file, cmd_ep, pub_ep, replay_delay_ms, pidfile)
                              : run_live(live_shcode, live_fut, cmd_ep, pub_ep, pidfile);
    pidfile_remove(); /* 내용이 자기 pid일 때만 지운다. SIGKILL 등은 stale로 남아 traderctl이 정리 */
    return rc;
}
