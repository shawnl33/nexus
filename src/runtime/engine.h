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
#include "core/indicators/htf_curve_predict.h"
#include "core/indicators/linreg_v3.h"
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
    double min_r2;               /* 최소신뢰도 (기본 0.40) */
    uint32_t market_period;      /* 마켓계산기간 (기본 20) */
} tr_engine_config_t;

typedef void (*tr_engine_status_fn)(void *ctx, const char *stream_id, uint64_t sequence,
                                    const char *payload_json);

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
} tr_engine_t;

bool tr_engine_init(tr_engine_t *e, const tr_engine_config_t *cfg,
                    tr_candle_t *bb_storage, size_t bb_capacity,
                    double *score_mid_storage, size_t score_mid_capacity);

void tr_engine_attach_ipc(tr_engine_t *e, tr_ipc_t *ipc, const char *stream_id);
void tr_engine_attach_status_cb(tr_engine_t *e, tr_engine_status_fn cb, void *ctx);

/* 틱/타이머 입력. replay 어댑터가 순서대로 호출한다. */
tr_bb_status_t tr_engine_on_tick(tr_engine_t *e, const tr_event_envelope_t *env, const tr_tick_t *tick);
void tr_engine_on_timer(tr_engine_t *e, tr_time_us_t now_us);

/* 호가 입력 (H1_/FH9). bids/asks는 총잔량(totbidrem/totofferrem). */
void tr_engine_on_orderbook(tr_engine_t *e, int64_t event_time_us, double bids, double asks);

/* 종목 전환: 지표 상태를 새 종목 기준으로 재구성하고 generation을 올린다 (계획서 §18).
   이전 세대의 늦은 응답과 새 화면이 섞이지 않게 한다. 전략의 거래 대상과는 무관하다(화면 상태 변경). */
bool tr_engine_select_symbol(tr_engine_t *e, uint64_t instrument_id, bool is_futures);

#endif
