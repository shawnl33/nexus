#ifndef _WIN32
#define _POSIX_C_SOURCE 199309L
#endif

#include <math.h>
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
#include "adapters/ls/ls_auth.h"
#include "adapters/ls/ls_chart.h"
#include "adapters/ls/ls_http.h"
#include "adapters/ls/ls_master.h"
#include "adapters/ls/ls_realtime.h"
#include "core/model/civil_time.h"
#include "runtime/engine.h"
#include "yyjson.h"

#include <signal.h>

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

#define BB_CAP 2560 /* 2일치 1분봉(선물 주간+야간 2,130) + 라이브 여유 */
/* 파이프라인별 저장소 풀 (다중 종목 지원 — 관측 상한 TR_ENGINE_MAX_PIPES).
 * 단일 종목 경로는 pool[0]을 쓰며, 기존 이름은 그 별칭으로 유지한다. */
static tr_candle_t g_bb_pool[TR_ENGINE_MAX_PIPES][BB_CAP];
static tr_bar_status_t g_status_pool[TR_ENGINE_MAX_PIPES][BB_CAP]; /* 봉별 지표 링 (스냅샷 복원용) */
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
    bool is_fut;
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

static tr_candle_t g_hist[2208]; /* 2일치(선물 2,130) + 페이지 경계 여유 */
static tr_candle_t g_page[512];   /* 주간 페이지 스크래치 (비압축 500 상한) */
static tr_candle_t g_night[1008]; /* 야간 t8461 (서버 상한 999) */
static ls_daily_bar_t g_daily[32]; /* ⑤ 체인 프라임용 일봉 (t8410/t8466) */
#define HIST_CAP ((size_t)(sizeof(g_hist) / sizeof(g_hist[0])))
#define NIGHT_CAP ((size_t)(sizeof(g_night) / sizeof(g_night[0])))
#define BACKFILL_PAGE_BARS 500 /* 비압축 qrycnt 상한 (t8465 명세) */
#define BACKFILL_STOCK_BARS 1440 /* NXT 720봉 × 2일 */
#define BACKFILL_FUT_BARS 2130   /* (주간 405 + 야간 660) × 2일 */
#define BACKFILL_DAILY_BARS 15   /* ⑤ 체인 워밍업용 일봉 수 (dtl1/gap1 각 10세션 필요) */

/* 워밍업 백필: 1분봉(실제 OHLC)을 2일치 조회해 봉 자체로 주입한다.
 * 주간은 t8465/t8412, 선물 야간은 t8461 — 두 계열을 시각으로 병합해 오름차순 주입한다.
 * (봉 낶부 틱 경로는 알 수 없지만 OHLC·거래량은 실측값이다.
 * 호가 지표는 과거 호가가 없어 라이브부터 워밍업된다)
 * 성공 시 주입한 봉 수, 실패 시 -1. */
static int backfill_minute_bars(ls_auth_t *auth, tr_engine_t *eng, tr_pipeline_t *pipe,
                                const char *shcode, bool is_fut) {
    size_t target = is_fut ? BACKFILL_FUT_BARS : BACKFILL_STOCK_BARS;
    /* 1) 주간 페이지 수집: 최신→과거 순으로 오므로 배열 끝에서부터 앞으로 채워 오름차순을 만든다 */
    size_t hi = HIST_CAP;
    char cont_date[9] = "99999999"; /* 첫 페이지 */
    char cont_time[11] = " ";
    for (size_t pg = 0; pg < 8 && HIST_CAP - hi < target; pg++) {
        ls_chart_page_t page;
        char cerr[128] = {0};
        /* 연속 조회: 이전 페이지 cts 값을 edate/etime으로 옮겨 다음(더 과거) 페이지를 얻는다 */
        int rc = ls_chart_fetch_minute(auth, is_fut ? LS_CHART_FUT_MIN : LS_CHART_STOCK_MIN,
                                       shcode, 1, BACKFILL_PAGE_BARS, cont_date, cont_time, " ", " ",
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
            break; /* 용량 부족 시 언더플로 방지 (8페이지×500 > HIST_CAP 가능) */
        }
        hi -= page.count;
        memcpy(g_hist + hi, g_page, page.count * sizeof(tr_candle_t));
        if (!page.has_more) {
            break;
        }
        snprintf(cont_date, sizeof(cont_date), "%s", page.cts_date);
        snprintf(cont_time, sizeof(cont_time), "%s", page.cts_time);
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
        int drc = ls_chart_fetch_daily(auth, is_fut ? LS_CHART_FUT_DAY : LS_CHART_STOCK_DAY,
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

    /* 3) 두 오름차순 계열을 시각으로 병합 주입 (같은 시각은 주간 우선) */
    int injected = 0;
    size_t i = 0, j = 0;
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
        if (tr_engine_inject_bar(eng, c)) {
            injected++;
        }
    }
    return injected;
}

static live_ctx_t g_live_ctx;

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

/* 종목 유형별 실시간 채널 선택 */
static void rt_channels_for(bool is_fut, const char **tick_cd, const char **ob_cd) {
    if (is_fut) {
        fut_rt_channels(tick_cd, ob_cd);
    } else {
        *tick_cd = "S3_";
        *ob_cd = "H1_";
    }
}

/* 종목 유형 판별: 마스터 레지스트리 우선, 없으면 코드 길이 추정(폐기 예정 경고) */
static bool resolve_is_fut(tr_ls_master_t *master, const char *shcode, const char **name_out) {
    const ls_instrument_info_t *info = ls_master_find(master, shcode);
    if (info != 0) {
        if (name_out != 0) {
            *name_out = info->name;
        }
        return info->is_futures;
    }
    if (name_out != 0) {
        *name_out = 0;
    }
    return strlen(shcode) > 6; /* 폐기 예정: 마스터에 없는 코드의 추정 */
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
static const char SNAP_IND_MANIFEST[] =
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
    "{\"id\":\"sma60\",\"name\":\"SMA 60\",\"defaultOn\":true}]}]}";

/* chart.snapshot 꼬리 고정 바이트: 섹션 구분자 3개(\"],\"ind|mem|pst\":[", 각 10자) +
 * 매니페스트 본문. 각 루프 가드는 항목 최악 크기에 이 고정 꼬리를 더한 만큼을 남긴다 —
 * 이론적 최악에도 매니페스트가 잘려 malformed JSON이 되지 않게 한다 */
#define SNAP_TAIL_FIXED (30 + (int)sizeof(SNAP_IND_MANIFEST) - 1)

/* snprintf는 잘리면 "썼어야 할 길이"를 돌려주므로 off가 buf 끝을 넘어설 수 있다.
 * 누적할 때마다 클램프해 뒤따르는 쓰기(섹션 구분자·매니페스트)가 buf 안에서 이어지게 한다 */
#define SNAP_CLAMP(buf, off)                                                  \
    do {                                                                      \
        if ((off) < 0 || (size_t)(off) >= sizeof(buf)) {                      \
            (off) = (int)sizeof(buf) - 1;                                     \
        }                                                                     \
    } while (0)

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
        bool new_fut = resolve_is_fut(lc->master, new_code, &new_name);
        const char *new_tick, *new_ob;
        rt_channels_for(new_fut, &new_tick, &new_ob);
        tr_ls_rt_subscribe(lc->rt, new_tick, new_code, new_id);
        tr_ls_rt_subscribe(lc->rt, new_ob, new_code, new_id);

        tr_engine_select_symbol(lc->engine, new_id, new_fut, new_code,
                                new_fut ? &SESS_FUT : &SESS_STOCK);
        /* 재초기화로 링이 끊기므로 다시 부착한다 (백필 주입 전에). 파이프라인 0이 쓰는
         * 풀 슬롯과 같은 슬롯의 상태/마켓 저장소를 부착한다 (pipe0 이식 후에도 정합) */
        int slot = watch_pool_of(lc->engine->pipes[0]);
        tr_engine_attach_status_ring(lc->engine, g_status_pool[slot], BB_CAP);
        tr_engine_attach_market(lc->engine, g_mkt_pool[slot], MKT_POOL_CAP);
        lc->watch_count = 0;
        watch_entry_t *w = &lc->watches[lc->watch_count++];
        snprintf(w->shcode, sizeof(w->shcode), "%s", new_code);
        snprintf(w->tick_cd, sizeof(w->tick_cd), "%s", new_tick);
        snprintf(w->ob_cd, sizeof(w->ob_cd), "%s", new_ob);
        w->is_fut = new_fut;
        w->instrument_id = new_id;

        /* 새 종목도 기동 시와 같은 경로로 백필한다 (없으면 빈 차트로 시작한다) */
        int nb = backfill_minute_bars(lc->auth, lc->engine, lc->engine->pipes[0], new_code, new_fut);

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
        uint64_t id = instrument_id_of(code);
        tr_pipeline_t *pipe = tr_engine_pipe_find(lc->engine, id);
        if (pipe != 0) {
            const char *name = 0;
            resolve_is_fut(lc->master, code, &name);
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
        const char *name = 0;
        bool fut = resolve_is_fut(lc->master, code, &name);
        pipe = tr_engine_pipe_add(lc->engine, id, fut, code, fut ? &SESS_FUT : &SESS_STOCK,
                                  g_bb_pool[slot], BB_CAP, g_score_mid_pool[slot], 64);
        if (pipe == 0) {
            cmd->status = "rejected";
            cmd->error_code = "watch_limit";
            cmd->payload_json = 0;
            yyjson_doc_free(doc);
            return;
        }
        tr_engine_pipe_attach_status_ring(lc->engine, id, g_status_pool[slot], BB_CAP);
        tr_engine_pipe_attach_market(lc->engine, id, g_mkt_pool[slot], MKT_POOL_CAP);
        const char *tick_cd, *ob_cd;
        rt_channels_for(fut, &tick_cd, &ob_cd);
        tr_ls_rt_subscribe(lc->rt, tick_cd, code, id);
        tr_ls_rt_subscribe(lc->rt, ob_cd, code, id);
        watch_entry_t *w = &lc->watches[lc->watch_count++];
        snprintf(w->shcode, sizeof(w->shcode), "%s", code);
        snprintf(w->tick_cd, sizeof(w->tick_cd), "%s", tick_cd);
        snprintf(w->ob_cd, sizeof(w->ob_cd), "%s", ob_cd);
        w->is_fut = fut;
        w->instrument_id = id;

        int nb = backfill_minute_bars(lc->auth, lc->engine, pipe, code, fut);

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
         * 페이지를 나누고, 이어지면 next_back_index != 0 을 돌려준다 (페이지당 150봉, 오름차순).
         * data.shcode로 대상 파이프라인을 고른다 (없으면 첫 파이프라인 — 구 호환). */
        static char buf[60 * 1024];
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
        size_t take = remain < 150 ? remain : 150;
        size_t next = from + take < n ? from + take : 0;
        int off = snprintf(buf, sizeof(buf),
                           "{\"shcode\":\"%s\",\"generation\":%u,\"timeframe_sec\":%u,\"total\":%zu,"
                           "\"next_back_index\":%zu,\"bars\":[",
                           pipe->shcode, pipe->generation, (unsigned)eng->cfg.timeframe_sec, n, next);
        SNAP_CLAMP(buf, off);
        bool first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 160 - SNAP_TAIL_FIXED;) {
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
         * 선물 0.05pt×100=5 raw, 주식 1원×100=100 raw).
         * [26]=틱 크기(raw, ④ 결과 띠 오프셋·⑧ 거리 기준에 사용), [27]=거래일(④ 세션 가드),
         * [28]=SMA 유효(5/20/60 모두 창 완성), [29..31]=SMA 5/20/60 (종가 기준) */
        double ps_flat = pipe->is_futures ? 5.0 : 100.0;
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"ind\":[");
        SNAP_CLAMP(buf, off);
        first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 448 - SNAP_TAIL_FIXED;) {
            tr_bar_status_t st;
            memset(&st, 0, sizeof(st));
            tr_engine_pipe_status_at(eng, pipe_id, k, &st);
            off += tr_bar_status_format_ind(&st, ps_flat, first, buf + off, sizeof(buf) - (size_t)off);
            SNAP_CLAMP(buf, off);
            first = false;
        }
        /* ⑥ 방향 기억 갱신 이벤트 (updated 봉만, 창 안에서 오름차순) */
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"mem\":[");
        SNAP_CLAMP(buf, off);
        first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 320 - SNAP_TAIL_FIXED;) {
            tr_bar_status_t st;
            memset(&st, 0, sizeof(st));
            tr_engine_pipe_status_at(eng, pipe_id, k, &st);
            if (!st.mem_updated) {
                continue;
            }
            off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                            "%s[%lld,%d,%d,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%d,%d,%d]",
                            first ? "" : ",", (long long)st.open_time_us,
                            st.mem_valid ? 1 : 0, st.mem_dir, st.mem_price,
                            st.mem_target[0], st.mem_target[1], st.mem_target[2],
                            st.mem_upper[0], st.mem_upper[1], st.mem_upper[2],
                            st.mem_lower[0], st.mem_lower[1], st.mem_lower[2],
                            st.mem_show_targets ? 1 : 0, st.mem_show_upper ? 1 : 0,
                            st.mem_show_lower ? 1 : 0);
            SNAP_CLAMP(buf, off);
            first = false;
        }
        /* ⑦ 지속 사진 저장 이벤트 (saved 봉만) */
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],\"pst\":[");
        SNAP_CLAMP(buf, off);
        first = true;
        for (size_t k = from + take; k-- > from && off < (int)sizeof(buf) - 260 - SNAP_TAIL_FIXED;) {
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
        /* 매니페스트는 고정 길이 — 위 루프 가드들이 SNAP_TAIL_FIXED만큼 남겨 두어 잘리지 않는다 */
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "],%s", SNAP_IND_MANIFEST);
        SNAP_CLAMP(buf, off);
        cmd->status = "applied";
        cmd->error_code = "none";
        cmd->payload_json = buf;
        return;
    }

    if (strstr(p, "\"type\":\"market.instruments\"") != 0) {
        /* 검색: data.q(종목코드 접두사 또는 종목명 부분 문자열), data.limit(기본 50, 최대 100) */
        static const ls_instrument_info_t *hits[100];
        static char buf[100 * 128 + 256];
        if (g_live_ctx.master == 0) {
            cmd->status = "rejected";
            cmd->error_code = "registry_unavailable";
            cmd->payload_json = 0;
            return;
        }
        const char *q = 0;
        long limit = 50;
        yyjson_doc *doc = yyjson_read((char *)p, strlen(p), 0);
        if (doc != 0) {
            yyjson_val *data = yyjson_obj_get(yyjson_doc_get_root(doc), "data");
            yyjson_val *qv = data != 0 ? yyjson_obj_get(data, "q") : 0;
            yyjson_val *lv = data != 0 ? yyjson_obj_get(data, "limit") : 0;
            if (yyjson_is_str(qv)) {
                q = yyjson_get_str(qv);
            }
            if (yyjson_is_num(lv)) {
                limit = (long)yyjson_get_num(lv);
            }
            yyjson_doc_free(doc);
        }
        if (limit < 1) {
            limit = 50;
        }
        if (limit > 100) {
            limit = 100;
        }
        size_t count = ls_master_count(g_live_ctx.master);
        size_t n = ls_master_search(g_live_ctx.master, q, hits, (size_t)limit);
        int off = snprintf(buf, sizeof(buf), "{\"total\":%zu,\"returned\":%zu,\"items\":[", count, n);
        for (size_t i = 0; i < n && off < (int)sizeof(buf) - 130; i++) {
            const ls_instrument_info_t *it = hits[i];
            off += snprintf(buf + off, sizeof(buf) - (size_t)off, "%s{\"shcode\":\"%s\",\"name\":\"%s\",\"fut\":%d}",
                            i > 0 ? "," : "", it->shcode, it->name, it->is_futures ? 1 : 0);
        }
        snprintf(buf + off, sizeof(buf) - (size_t)off, "]}");
        cmd->status = "applied";
        cmd->error_code = "none";
        cmd->payload_json = buf;
        return;
    }

    {
        int off = snprintf(payload, sizeof(payload), "{\"mode\":\"live\",\"running\":%d,\"shcode\":\"%s\",\"watches\":[",
                           (int)g_running,
                           g_live_ctx.engine != 0 ? g_live_ctx.engine->pipes[0]->shcode : "");
        for (int i = 0; i < g_live_ctx.watch_count && off < (int)sizeof(payload) - 20; i++) {
            off += snprintf(payload + off, sizeof(payload) - (size_t)off, "%s\"%s\"",
                            i > 0 ? "," : "", g_live_ctx.watches[i].shcode);
        }
        snprintf(payload + off, sizeof(payload) - (size_t)off, "]}");
    }
    cmd->status = "applied";
    cmd->error_code = "none";
    cmd->payload_json = payload;
}

static int run_live(const char *shcode, bool is_fut, const char *cmd_ep, const char *pub_ep) {
    /* 1) 인증 */
    ls_auth_t auth;
    ls_auth_init(&auth, 0);
    const char *token = 0;
    if (!ls_auth_ensure(&auth, &token)) {
        fprintf(stderr, "error: LS auth: %s\n", auth.last_error);
        return 3;
    }

    /* 1-1) 종목 레지스트리 (t8436 + t8467). 실패 시 추정으로 계속한다 */
    char merr[128] = {0};
    tr_ls_master_t *master = ls_master_fetch(&auth, merr, sizeof(merr));
    if (master != 0) {
        const char *found_name = 0;
        bool resolved = resolve_is_fut(master, shcode, &found_name);
        printf("instruments: %zu registered, %s=%s (%s)\n", ls_master_count(master),
               shcode, resolved ? "FUT" : "STK", found_name != 0 ? found_name : "unknown");
        is_fut = resolved;
    } else {
        fprintf(stderr, "instrument master unavailable: %s (fallback to heuristic)\n", merr);
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

    /* 3) 엔진 */
    tr_engine_config_t ecfg;
    memset(&ecfg, 0, sizeof(ecfg));
    ecfg.engine_instance_id = engine_instance_id;
    ecfg.instrument_id = instrument_id_of(shcode);
    snprintf(ecfg.shcode, sizeof(ecfg.shcode), "%s", shcode);
    ecfg.session = is_fut ? SESS_FUT : SESS_STOCK;
    ecfg.timeframe_sec = 60;
    ecfg.no_trade = TR_NO_TRADE_SKIP;
    ecfg.is_futures = is_fut;
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
    if (!tr_engine_init(&engine, &ecfg, g_bb_storage, BB_CAP, g_score_mid, 64)) {
        fprintf(stderr, "error: engine init failed\n");
        tr_ipc_close(ipc);
        return 3;
    }
    tr_engine_attach_ipc(&engine, ipc, "display");
    tr_engine_attach_status_ring(&engine, g_status_storage, BB_CAP);
    tr_engine_attach_market(&engine, g_mkt_storage, sizeof(g_mkt_storage) / sizeof(g_mkt_storage[0]));

    /* 4) 워밍업 백필 (market select/watch 시에도 같은 경로로 다시 채운다) */
    {
        int nb = backfill_minute_bars(&auth, &engine, engine.pipes[0], shcode, is_fut);
        if (nb > 0) {
            printf("backfill: %d bars\n", nb);
        }
    }

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
        return 3;
    }
    /* 주식은 S3_/H1_, 선물은 주간 FC9/FH9·야간 DC0/DH0을 현재 시각으로 고른다 */
    const char *tr_cd, *ob_tr_cd;
    rt_channels_for(is_fut, &tr_cd, &ob_tr_cd);
    if (!tr_ls_rt_subscribe(rt, tr_cd, shcode, ecfg.instrument_id) ||
        !tr_ls_rt_subscribe(rt, ob_tr_cd, shcode, ecfg.instrument_id)) {
        fprintf(stderr, "error: subscribe failed\n");
        tr_ls_rt_close(rt);
        tr_ipc_close(ipc);
        return 3;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    g_running = 1;
    g_stop = 0;
    /* 초기 종목 = 첫 워치 (파이프라인 0) */
    g_live_ctx.watch_count = 1;
    snprintf(g_live_ctx.watches[0].shcode, sizeof(g_live_ctx.watches[0].shcode), "%s", shcode);
    snprintf(g_live_ctx.watches[0].tick_cd, sizeof(g_live_ctx.watches[0].tick_cd), "%s", tr_cd);
    snprintf(g_live_ctx.watches[0].ob_cd, sizeof(g_live_ctx.watches[0].ob_cd), "%s", ob_tr_cd);
    g_live_ctx.watches[0].is_fut = is_fut;
    g_live_ctx.watches[0].instrument_id = ecfg.instrument_id;
    g_live_ctx.rt = rt;
    g_live_ctx.engine = &engine;
    g_live_ctx.master = master;
    g_live_ctx.auth = &auth;

    printf("live %s %s: streaming (Ctrl+C 또는 'traderctl engine stop'으로 중지)\n",
           is_fut ? "FUT" : "STK", shcode);

    int rc = 0;
    while (!g_stop) {
        tr_ls_rt_service(rt, 20);
        ls_rt_event_t ev;
        while (tr_ls_rt_next(rt, &ev)) {
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
    return rc;
}

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
    printf("      --live SHCODE       Live mode: subscribe realtime ticks for stock SHCODE\n");
    printf("      --live-fut SHCODE   Live mode: subscribe realtime ticks for futures SHCODE\n");
    printf("      --cmd-endpoint EP   Command endpoint (default tcp://127.0.0.1:5555)\n");
    printf("      --pub-endpoint EP   Status stream endpoint (default tcp://127.0.0.1:5556)\n");
    printf("\n");
    printf("Replay mode requires no API credentials.\n");
}

static void print_version(void) {
    printf("trading-engine %s\n", TRADING_ENGINE_VERSION);
}

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
    ecfg.daily_reg_period = 10;
    ecfg.gap_mid = 0.35;
    ecfg.gap_big = 0.75;
    ecfg.big_gap_reeval_min = 30;
    ecfg.min_final_strength = 40;

    tr_engine_t engine;
    if (!tr_engine_init(&engine, &ecfg, g_bb_storage, BB_CAP, g_score_mid, 64)) {
        fprintf(stderr, "error: engine init failed\n");
        tr_ipc_close(ipc);
        tr_csv_ticks_free(ticks);
        return 3;
    }
    tr_engine_attach_ipc(&engine, ipc, "display");
    tr_engine_attach_status_ring(&engine, g_status_storage, BB_CAP);
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
    bool live_fut = false;
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
        if (strcmp(argv[i], "--live") == 0 && i + 1 < argc) {
            live_shcode = argv[++i];
            live_fut = false;
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
    if (live_shcode != 0) {
        return run_live(live_shcode, live_fut, cmd_ep, pub_ep);
    }

    fprintf(stderr, "error: no mode specified; see --help\n");
    return 1;
}
