#ifndef TR_ENGINE_H
#define TR_ENGINE_H

/* 엔진 런타임 조립 (계획서 §3, §5).
 *
 * replay/backtest 경로의 이벤트 순서를 연결한다:
 *   정규화된 틱 → bar_builder → 봉 이벤트 → 지표 평가(lr3/htf/score) → 상태 스트림 발행.
 *
 * - core 상태 갱신은 이 하나의 흐름에서 처리한다 (단일 실행 흐름).
 * - 호가 입력이 없는 경로(파일 재생)에서는 호가 구성요소를 미지원으로 표시한다 (ob_dir=0).
 * - 지표가 요구하는 내장 컨텍스트(DayIndex==0, trading day 등)를 세션 정책에서 계산해 주입한다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adapters/ipc/ipc.h"
#include "core/indicators/daily_align_v2.h"
#include "core/indicators/daily_trend_link_v1.h"
#include "core/indicators/gap_regime_v1.h"
#include "core/indicators/htf_curve_predict.h"
#include "core/indicators/linreg_v3.h"
#include "core/indicators/market_profile.h"
#include "core/indicators/memory_lines.h"
#include "core/indicators/orderbook_dir_v2.h"
#include "core/indicators/score_1m.h"
#include "core/indicators/sma.h"
#include "core/market/bar_builder.h"

typedef struct {
    uint64_t engine_instance_id;
    uint64_t instrument_id;
    char shcode[16];             /* 종목 코드 (상태 페이로드 식별용). 없으면 "" (리플레이) */
    tr_session_policy_t session;
    uint32_t timeframe_sec;      /* 기본 봉 주기(초) */
    tr_no_trade_policy_t no_trade;
    bool is_futures;             /* 호가 부호 규칙(선물=매수 우세 양수)에 사용 */
    /* 지표 파라미터 (메인 원본 기본값 대응) */
    int32_t predict_bars[3];     /* 예측봉수1~3 (기본 5/10/15) */
    int32_t htf_ticks;           /* 예측변수 (기본 10) */
    double min_r2;               /* 최소신뢰도 (기본 0.40). 일봉최소신뢰도·dalign 분봉최소신뢰도 겸용 */
    uint32_t market_period;      /* 마켓계산기간 (기본 20) */
    /* ⑤ 매매 상태 체인 파라미터 (메인 원본 기본값 대응) */
    int daily_reg_period;        /* 일봉회귀기간 (기본 10) */
    double gap_mid;              /* 중간갭기준 (기본 0.35) */
    double gap_big;              /* 큰갭기준 (기본 0.75) */
    int big_gap_reeval_min;      /* 큰갭재평가분 (기본 30) */
    int min_final_strength;      /* 최종최소강도 (기본 40) */
} tr_engine_config_t;

typedef void (*tr_engine_status_fn)(void *ctx, const char *stream_id, uint64_t sequence,
                                    const char *payload_json);

/* 봉별 지표 스냅샷 (스냅샷 명령으로 과거 봉의 회귀·예측·점수를 복원하기 위한 기록).
 * 봉 링과 같은 순서·같은 용량으로 유지한다 (push/update 패턴 동일). */
typedef struct {
    tr_time_us_t open_time_us;  /* 봉 링과의 정합 검사용 */
    bool closed;
    bool reg_valid;
    double reg_line, reg_r2;
    double pred[3];
    int pred_dir[3];            /* 예측방향1~3 (과거 채점의 방향 비교에 사용) */
    double residual;            /* 회귀잔차 (미래 목표선 오차 띠) */
    double pvol;                /* 예측변동성 ATR(14) (오차 띠) */
    double upper[3], lower[3];  /* MTF상단/하단1~3 (엔진 계산, 부채꼴·기억선 공용) */
    int score;
    bool ob_valid;
    double ob_score;
    /* ⑧ 마켓 밴드 */
    bool mkt_valid;
    double mkt_center, mkt_u1, mkt_l1, mkt_u2, mkt_l2;
    /* ⑥ 방향 기억 (updated 봉에만 신규 세트. 나머지 봉은 이전 세트 유지 의미.
     * mem_reset은 세션 리셋 봉 표시 — 스냅샷 mem 이벤트가 경계에서 세트를 끊는 데 쓴다) */
    bool mem_valid, mem_updated;
    bool mem_reset;
    int mem_dir;
    double mem_price, mem_target[3], mem_upper[3], mem_lower[3];
    bool mem_show_targets, mem_show_upper, mem_show_lower;
    /* ⑦ 지속 사진 (saved 봉에만 신규 촬영) */
    bool pst_saved, pst_valid;
    int pst_dir;
    double pst_target[3], pst_upper[3], pst_lower[3];
    /* ⑤ 매매 상태 (운영최종방향/상태/강도/유효 — 일봉 추세 연결 → 갭 레짐 → 일봉 정렬 체인) */
    int final_valid;
    int final_dir;
    int final_state;
    int final_strength;
    int64_t trading_day;        /* 이 봉의 거래일 (④ 결과 띠의 세션 가드에 사용) */
    /* SMA 5/20/60 (모든 timeframe에서 평가, 진행 봉 포함 현재 값) */
    int sma_valid;              /* 세 기간 모두 valid일 때 1 */
    double sma[3];              /* 5/20/60 순 */
} tr_bar_status_t;

typedef struct tr_engine tr_engine_t;

/* 종목별 파이프라인: 한 종목의 봉 구축·지표 평가·상태 기록 상태를 모두 갖는다
 * (docs/superpowers/plans/2026-09-29-multi-symbol-pipelines.md Task 1).
 * 저장소(봉 링·점수 중간값·상태 링·마켓 링 버퍼)는 호출자 소유로 부착된다. */
typedef struct {
    tr_engine_t *engine;        /* 소유 엔진 (봉 이벤트 콜백에서 출력·공유 cfg 접근) */
    uint64_t instrument_id;
    char shcode[16];            /* 종목 코드 (페이로드 "shcode" 키). JSON 안전 문자만 보관 */
    bool is_futures;            /* 호가 부호 규칙·틱 양자화에 사용 */
    tr_session_policy_t session; /* 이 종목의 세션 정책 (봉 구축·trading day·⑤ 바 분 계산의 기준) */
    tr_candle_t *bb_storage;
    size_t bb_capacity;
    double *score_mid_storage;
    size_t score_mid_capacity;
    tr_bar_builder_t bb;
    tr_lr3_t lr3;
    tr_htf_curve_t htf;
    tr_obd2_t obd2;
    tr_score1m_t score;
    uint32_t generation;        /* 종목 전환 세대. 스냅숏·스트림 연결에 사용 (계획서 §18) */
    int64_t prev_trading_day;
    bool has_prev_day;
    tr_time_us_t prev_bar_open;
    bool has_prev_bar;
    tr_ring status_ring;        /* tr_bar_status_t 링. attached일 때만 기록 */
    bool status_ring_on;
    tr_market_t mkt;            /* ⑧ 마켓 밴드 (attach_market 시에만 평가) */
    bool mkt_on;
    /* ⑤ 매매 상태 체인 (1분봉 전용): 일봉 추세 연결 → 갭 레짐 → 일봉 정렬.
     * bar_index는 체인에 공급한 봉 수(세션 경계 게이트용, 봉당 1회 증가) */
    tr_dtl1_t dtl1;
    tr_gap1_t gap1;
    tr_dalign2_output_t dalign;
    uint64_t bar_index;
    /* ⑥ 방향 기억 (운영최종방향 = ⑤ dalign.final_dir, 무효 시 회귀선_구분 부호 폴백) */
    tr_regmem_t regmem;
    tr_persist_t persist;       /* ⑦ 지속 사진 */
    tr_sma_t sma5, sma20, sma60; /* 이평선 (게이트 없음, 모든 timeframe) */
} tr_pipeline_t;

#define TR_ENGINE_MAX_PIPES 8 /* 동시 관측 종목 상한 (더 필요하면 상수만 올린다) */

struct tr_engine {
    tr_engine_config_t cfg;     /* 공유 설정. instrument_id/is_futures/session은 pipes[0](선택 종목) 기준 */
    tr_ipc_t *ipc;              /* NULL이면 status_cb 사용 */
    tr_engine_status_fn status_cb;
    void *status_cb_ctx;
    const char *stream_id;
    uint64_t status_seq;
    tr_pipeline_t *pipes[TR_ENGINE_MAX_PIPES]; /* 활성 파이프라인. [0]은 항상 내장 pipe0 */
    int pipe_count;
    /* 파이프라인 0은 엔진에 내장하고, 익명 구조체 뷰를 겹쳐 놓아 e->bb·e->lr3 등
     * 기존 직접 접근이 pipes[0]의 저장소를 가리키게 한다 (C11 익명 멤버).
     * 아래 멤버 목록·순서는 tr_pipeline_t와 동일해야 한다 — 뒤의 offset 검사가 고정한다. */
    union {
        tr_pipeline_t pipe0;
        struct {
            tr_engine_t *engine;
            uint64_t instrument_id;
            char shcode[16];
            bool is_futures;
            tr_session_policy_t session;
            tr_candle_t *bb_storage;
            size_t bb_capacity;
            double *score_mid_storage;
            size_t score_mid_capacity;
            tr_bar_builder_t bb;
            tr_lr3_t lr3;
            tr_htf_curve_t htf;
            tr_obd2_t obd2;
            tr_score1m_t score;
            uint32_t generation;
            int64_t prev_trading_day;
            bool has_prev_day;
            tr_time_us_t prev_bar_open;
            bool has_prev_bar;
            tr_ring status_ring;
            bool status_ring_on;
            tr_market_t mkt;
            bool mkt_on;
            tr_dtl1_t dtl1;
            tr_gap1_t gap1;
            tr_dalign2_output_t dalign;
            uint64_t bar_index;
            tr_regmem_t regmem;
            tr_persist_t persist;
            tr_sma_t sma5, sma20, sma60;
        };
    };
    tr_pipeline_t pipe_slots[TR_ENGINE_MAX_PIPES - 1]; /* pipes[1..] 내장 저장소 */
};

/* 익명 뷰와 tr_pipeline_t의 레이아웃 일치를 컴파일 타임에 고정한다 */
#define TR_ENGINE_PIPE_LAYOUT_CHECK(m)                                       \
    _Static_assert(offsetof(tr_engine_t, m) - offsetof(tr_engine_t, pipe0) == \
                       offsetof(tr_pipeline_t, m),                           \
                   "tr_pipeline_t layout mismatch: " #m)
TR_ENGINE_PIPE_LAYOUT_CHECK(engine);
TR_ENGINE_PIPE_LAYOUT_CHECK(instrument_id);
TR_ENGINE_PIPE_LAYOUT_CHECK(shcode);
TR_ENGINE_PIPE_LAYOUT_CHECK(is_futures);
TR_ENGINE_PIPE_LAYOUT_CHECK(session);
TR_ENGINE_PIPE_LAYOUT_CHECK(bb_storage);
TR_ENGINE_PIPE_LAYOUT_CHECK(bb_capacity);
TR_ENGINE_PIPE_LAYOUT_CHECK(score_mid_storage);
TR_ENGINE_PIPE_LAYOUT_CHECK(score_mid_capacity);
TR_ENGINE_PIPE_LAYOUT_CHECK(bb);
TR_ENGINE_PIPE_LAYOUT_CHECK(lr3);
TR_ENGINE_PIPE_LAYOUT_CHECK(htf);
TR_ENGINE_PIPE_LAYOUT_CHECK(obd2);
TR_ENGINE_PIPE_LAYOUT_CHECK(score);
TR_ENGINE_PIPE_LAYOUT_CHECK(generation);
TR_ENGINE_PIPE_LAYOUT_CHECK(prev_trading_day);
TR_ENGINE_PIPE_LAYOUT_CHECK(has_prev_day);
TR_ENGINE_PIPE_LAYOUT_CHECK(prev_bar_open);
TR_ENGINE_PIPE_LAYOUT_CHECK(has_prev_bar);
TR_ENGINE_PIPE_LAYOUT_CHECK(status_ring);
TR_ENGINE_PIPE_LAYOUT_CHECK(status_ring_on);
TR_ENGINE_PIPE_LAYOUT_CHECK(mkt);
TR_ENGINE_PIPE_LAYOUT_CHECK(mkt_on);
TR_ENGINE_PIPE_LAYOUT_CHECK(dtl1);
TR_ENGINE_PIPE_LAYOUT_CHECK(gap1);
TR_ENGINE_PIPE_LAYOUT_CHECK(dalign);
TR_ENGINE_PIPE_LAYOUT_CHECK(bar_index);
TR_ENGINE_PIPE_LAYOUT_CHECK(regmem);
TR_ENGINE_PIPE_LAYOUT_CHECK(persist);
TR_ENGINE_PIPE_LAYOUT_CHECK(sma5);
TR_ENGINE_PIPE_LAYOUT_CHECK(sma20);
TR_ENGINE_PIPE_LAYOUT_CHECK(sma60);
#undef TR_ENGINE_PIPE_LAYOUT_CHECK

bool tr_engine_init(tr_engine_t *e, const tr_engine_config_t *cfg,
                    tr_candle_t *bb_storage, size_t bb_capacity,
                    double *score_mid_storage, size_t score_mid_capacity);

/* 파이프라인 목록 (다중 관측 준비 — pipes[0]은 init이 만드는 선택 종목).
 * add는 이미 있으면 그 파이프라인을 돌려주고, 가득 차면(NULL) 실패한다.
 * 저장소(봉 링·점수 중간값)는 호출자 소유로 init과 같은 규칙·검증이다.
 * shcode는 상태 페이로드 식별용으로 파이프라인에 보관된다 (NULL이면 "").
 * session은 이 종목의 세션 정책으로 파이프라인에 보관된다 (필수) — 기동 종목의
 * 세션을 상속하지 않는다. 주식/선물이 섞여 있으면 종목별로 따로 넘긴다.
 * 파이프라인 객체는 내장 슬롯에 고정된다: add가 돌려준 포인터는 그 파이프라인이
 * remove되기 전까지 유효하며, 장기 보관은 instrument_id만 하고 매번 find로 조회한다.
 * remove는 pipes[] 순서를 보존한다(compact). 단, 대상이 pipes[0]이면 마지막
 * 파이프라인의 내용을 pipe0(고정 주소, 익명 뷰의 기반)에 통째로 이식하고 그 슬롯을
 * 비운다 — 이 경우 순서는 보존되지 않고 다른 슬롯 포인터가 dangling이 된다.
 * remove는 마지막 1개는 제거하지 않고 false를 돌려준다. */
tr_pipeline_t *tr_engine_pipe_find(tr_engine_t *e, uint64_t instrument_id);
tr_pipeline_t *tr_engine_pipe_add(tr_engine_t *e, uint64_t instrument_id, bool is_futures,
                                  const char *shcode, const tr_session_policy_t *session,
                                  tr_candle_t *bb_storage, size_t bb_capacity,
                                  double *score_mid_storage, size_t score_mid_capacity);
bool tr_engine_pipe_remove(tr_engine_t *e, uint64_t instrument_id);

void tr_engine_attach_ipc(tr_engine_t *e, tr_ipc_t *ipc, const char *stream_id);
void tr_engine_attach_status_cb(tr_engine_t *e, tr_engine_status_fn cb, void *ctx);

/* 봉별 지표 기록 링 부착 (호출자 소유 저장소, 파이프라인 0 대상). 봉 링과 같은 용량을 권장한다.
 * 부착 시점부터 기록한다. 재부착하면 링이 초기화된다 (종목 전환 후 재사용).
 * pipe_* 변형은 instrument_id로 대상 파이프라인을 골라 부착·조회한다 (없으면 false/0). */
bool tr_engine_attach_status_ring(tr_engine_t *e, tr_bar_status_t *storage, size_t capacity);
bool tr_engine_pipe_attach_status_ring(tr_engine_t *e, uint64_t instrument_id,
                                       tr_bar_status_t *storage, size_t capacity);
size_t tr_engine_status_count(const tr_engine_t *e);
bool tr_engine_status_at(const tr_engine_t *e, size_t back_index, tr_bar_status_t *out);
size_t tr_engine_pipe_status_count(const tr_engine_t *e, uint64_t instrument_id);
bool tr_engine_pipe_status_at(const tr_engine_t *e, uint64_t instrument_id,
                              size_t back_index, tr_bar_status_t *out);

/* chart.snapshot의 ind[i] 한 행을 포맷한다 — 32개 값, 인덱스 레이아웃은
 * docs/display_payload.md §2. first=false이면 앞에 쉼표를 붙인다 (배열 연결).
 * ps_flat은 reg_flat 재계산·틱 크기의 raw 단위 (선물 5, 주식 100).
 * 반환은 snprintf 규약: cap을 넘으면 잘리고 썼어야 할 길이를 돌려준다. */
int tr_bar_status_format_ind(const tr_bar_status_t *st, double ps_flat, bool first,
                             char *buf, size_t cap);

/* chart.snapshot의 mem[i] 한 이벤트를 포맷한다 — 17개 값, 레이아웃은
 * docs/display_payload.md §2: [time, valid, dir, price, t1..3, u1..3, l1..3,
 * showT, showU, showL, reset]. 이벤트가 아닌 봉(updated도 reset도 아님)이면
 * 아무것도 쓰지 않고 0을 돌려준다. first=false이면 앞에 쉼표를 붙인다 (배열 연결).
 * 반환은 snprintf 규약: cap을 넘으면 잘리고 썼어야 할 길이를 돌려준다. */
int tr_bar_status_format_mem(const tr_bar_status_t *st, bool first, char *buf, size_t cap);

/* ⑧ 마켓 밴드 평가 부착 (호출자 소유 캔들 저장소, 마켓계산기간×2 권장).
 * 재부착 시 마켓 상태가 초기화된다 (종목 전환 후 재사용).
 * pipe_* 변형은 instrument_id로 대상 파이프라인을 골라 부착한다. */
bool tr_engine_attach_market(tr_engine_t *e, tr_candle_t *storage, size_t capacity);
bool tr_engine_pipe_attach_market(tr_engine_t *e, uint64_t instrument_id,
                                  tr_candle_t *storage, size_t capacity);

/* 틱/타이머 입력. replay 어댑터가 순서대로 호출한다.
 * 틱은 instrument_id로 파이프라인을 찾아 라우팅한다 (없으면 TR_BB_ERROR로 드롭).
 * 타이머는 모든 활성 파이프라인에 전달한다. */
tr_bb_status_t tr_engine_on_tick(tr_engine_t *e, const tr_event_envelope_t *env, const tr_tick_t *tick);
void tr_engine_on_timer(tr_engine_t *e, tr_time_us_t now_us);

/* 백필: 과거 확정 봉(실제 OHLC)을 직접 주입한다. tr_bar_builder_inject_bar 래퍼. */
bool tr_engine_inject_bar(tr_engine_t *e, const tr_candle_t *bar);

/* 호가 입력 (H1_/FH9). bids/asks는 총잔량(totbidrem/totofferrem).
 * instrument_id로 파이프라인을 찾아 라우팅한다 (없으면 드롭). */
void tr_engine_on_orderbook(tr_engine_t *e, uint64_t instrument_id,
                            int64_t event_time_us, double bids, double asks);

/* 종목 전환: 지표 상태를 새 종목 기준으로 재구성하고 generation을 올린다 (계획서 §18).
   이전 세대의 늦은 응답과 새 화면이 섞이지 않게 한다. 전략의 거래 대상과는 무관하다(화면 상태 변경).
   session은 새 종목의 세션 정책이다 (필수 — 시장이 다르면 바뀐다). */
bool tr_engine_select_symbol(tr_engine_t *e, uint64_t instrument_id, bool is_futures,
                             const char *shcode, const tr_session_policy_t *session);

#endif
