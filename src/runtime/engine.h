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
#include "core/market/bar_builder.h"

typedef struct {
    uint64_t engine_instance_id;
    uint64_t instrument_id;
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
    /* ⑥ 방향 기억 (updated 봉에만 신규 세트. 나머지 봉은 이전 세트 유지 의미) */
    bool mem_valid, mem_updated;
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
} tr_bar_status_t;

typedef struct {
    tr_engine_config_t cfg;
    tr_candle_t *bb_storage;
    size_t bb_capacity;
    double *score_mid_storage;
    size_t score_mid_capacity;
    tr_bar_builder_t bb;
    tr_lr3_t lr3;
    tr_htf_curve_t htf;
    tr_obd2_t obd2;
    tr_score1m_t score;
    tr_ipc_t *ipc;              /* NULL이면 status_cb 사용 */
    tr_engine_status_fn status_cb;
    void *status_cb_ctx;
    const char *stream_id;
    uint64_t status_seq;
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
} tr_engine_t;

bool tr_engine_init(tr_engine_t *e, const tr_engine_config_t *cfg,
                    tr_candle_t *bb_storage, size_t bb_capacity,
                    double *score_mid_storage, size_t score_mid_capacity);

void tr_engine_attach_ipc(tr_engine_t *e, tr_ipc_t *ipc, const char *stream_id);
void tr_engine_attach_status_cb(tr_engine_t *e, tr_engine_status_fn cb, void *ctx);

/* 봉별 지표 기록 링 부착 (호출자 소유 저장소). 봉 링과 같은 용량을 권장한다.
 * 부착 시점부터 기록한다. 재부착하면 링이 초기화된다 (종목 전환 후 재사용). */
bool tr_engine_attach_status_ring(tr_engine_t *e, tr_bar_status_t *storage, size_t capacity);
size_t tr_engine_status_count(const tr_engine_t *e);
bool tr_engine_status_at(const tr_engine_t *e, size_t back_index, tr_bar_status_t *out);

/* ⑧ 마켓 밴드 평가 부착 (호출자 소유 캔들 저장소, 마켓계산기간×2 권장).
 * 재부착 시 마켓 상태가 초기화된다 (종목 전환 후 재사용). */
bool tr_engine_attach_market(tr_engine_t *e, tr_candle_t *storage, size_t capacity);

/* 틱/타이머 입력. replay 어댑터가 순서대로 호출한다. */
tr_bb_status_t tr_engine_on_tick(tr_engine_t *e, const tr_event_envelope_t *env, const tr_tick_t *tick);
void tr_engine_on_timer(tr_engine_t *e, tr_time_us_t now_us);

/* 백필: 과거 확정 봉(실제 OHLC)을 직접 주입한다. tr_bar_builder_inject_bar 래퍼. */
bool tr_engine_inject_bar(tr_engine_t *e, const tr_candle_t *bar);

/* 호가 입력 (H1_/FH9). bids/asks는 총잔량(totbidrem/totofferrem). */
void tr_engine_on_orderbook(tr_engine_t *e, int64_t event_time_us, double bids, double asks);

/* 종목 전환: 지표 상태를 새 종목 기준으로 재구성하고 generation을 올린다 (계획서 §18).
   이전 세대의 늦은 응답과 새 화면이 섞이지 않게 한다. 전략의 거래 대상과는 무관하다(화면 상태 변경). */
bool tr_engine_select_symbol(tr_engine_t *e, uint64_t instrument_id, bool is_futures);

#endif
