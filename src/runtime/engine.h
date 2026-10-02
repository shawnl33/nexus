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
#include "core/functions/daily_align_v2.h"
#include "core/functions/daily_trend_link_v1.h"
#include "core/functions/gap_regime_v1.h"
#include "core/functions/htf_curve_predict.h"
#include "core/functions/linreg_v3.h"
#include "core/indicators/market_profile.h"
#include "core/indicators/memory_lines.h"
#include "core/functions/orderbook_dir_v2.h"
#include "core/indicators/score_1m.h"
#include "core/functions/sma.h"
#include "core/indicators/fx_mirae_v1.h"
#include "core/indicators/fx_mirae_v3.h"
#include "core/indicators/fx_persist_gap.h"
#include "core/indicators/fx_yangmae.h"
#include "core/functions/osf_clv_vol_flow_v1.h"
#include "core/indicators/fx_sniper.h"
#include "core/market/bar_builder.h"

typedef struct {
    uint64_t engine_instance_id;
    uint64_t instrument_id;
    char shcode[16];             /* 종목 코드 (상태 페이로드 식별용). 없으면 "" (리플레이) */
    tr_session_policy_t session;
    uint32_t timeframe_sec;      /* 기본 봉 주기(초) */
    tr_no_trade_policy_t no_trade;
    bool is_futures;             /* 호가 부호 규칙(선물=매수 우세 양수)에 사용 */
    double tick_raw;             /* 1틱의 raw 크기 (실제 × 100). 0이면 자동(선물 5, 주식 100) — 해외선물만 명시 */
    bool is_ovs;                 /* 해외선물. 1분봉에서만 fx_mirae_v1을 평가한다 */
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

#define TR_FX3_WIRE 64 /* V3 표시 Plot을 봉 상태에 실을 상한 */

/* 봉별 지표 스냅샷 (스냅샷 명령으로 과거 봉의 회귀·예측·점수를 복원하기 위한 기록).
 * 봉 링과 같은 순서·같은 용량으로 유지한다 (push/update 패턴 동일). */
typedef struct {
    tr_time_us_t open_time_us;  /* 봉 링과의 정합 검사용 */
    bool closed;
    bool reg_valid;
    double reg_line, reg_slope, reg_r2;
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
    /* 해외선물 미래곡선 V1. fx_on=0이면 국내 봉(스냅샷 ind는 32원소로 끝난다) */
    int fx_on;
    uint32_t fx_mask;           /* bit k = Plot(k+1) 표시 */
    double fx_plot[TR_FXMIRAE_PLOTS];
    /* 해외선물 미래곡선 V3 표시. 켜진 Plot만, 번호 오름차순, 최대 TR_FX3_WIRE개 */
    int fx3_on;
    int fx3_n;
    uint16_t fx3_id[TR_FX3_WIRE];
    double fx3_v[TR_FX3_WIRE];
    uint32_t fx3_rgb[TR_FX3_WIRE];
    uint8_t fx3_w[TR_FX3_WIRE];
    /* 지속목표차. [0]=삼선 [1]=오선. v = gap,peak,prev,ratio,prevRatio,cnt */
    int pg_ready[2];
    double pg_v[2][6];
    uint32_t pg_c4[2], pg_c5[2];
    uint8_t pg_w4[2];
    uint8_t pg_plot3[2], pg_plot5[2];
    /* 평탄회귀차 1/5/15/30분. v = gap,peak,prev,ratio,prevRatio,cnt */
    int rg_ready;
    double rg_v[6];
    uint32_t rg_c4, rg_c5;
    uint8_t rg_w4, rg_plot3, rg_plot5;
    /* 마켓중심차 1/5/15/30분 */
    int mg_ready;
    double mg_v[6];
    uint32_t mg_c4, mg_c5;
    uint8_t mg_w4, mg_plot3, mg_plot5;
    /* 통합 지표의 삼선·오선 비율색. [0]=삼선 [1]=오선 */
    uint32_t pg_union_rgb[2];
    uint8_t pg_union_w[2];
    /* 양매수. 0이면 표시 없음 */
    int ym_pos, ym_prev_valid, ym_show_h1, ym_show_h2, ym_show_h3, ym_show_h4, ym_show_first;
    double ym_prev_hi, ym_prev_lo, ym_two_hi, ym_two_lo;
    double ym_mark_h1, ym_mark_h2, ym_mark_h3, ym_mark_h4;
    /* 스나이퍼. score=합계, ex=스코프제외, ratio=비율점수, stage=압축단계 */
    int sn_ready, sn_score, sn_ex, sn_ratio, sn_stage, sn_compound;
    double sn_tgt, sn_px;
    uint32_t sn_rgb;
    int sn_px_exit, sn_below, sn_above, sn_reset;
    /* 가격거래량압축. 직전 봉 비율. on이 0이면 그 선은 그리지 않는다. */
    int pvc_price_on, pvc_vol_on, pvc_both;
    double pvc_price, pvc_vol;
    /* 우측 미래 목표선. 회귀가 유효하고 세션 첫 봉이 아닐 때만. */
    int ray_on;
    int ray_sign;
    double ray_px[5], ray_up[5], ray_dn[5];
    int8_t ray_dir[5];
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
    double tick_raw;            /* 1틱의 raw 크기. 0이면 자동(선물 5, 주식 100) — 해외선물만 명시 */
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
    bool is_ovs;                /* 해외선물 파이프라인. 1분봉에서 fx_mirae_v1 평가 */
    tr_fxmirae_t fx;            /* is_ovs일 때만 init. 이식 후 tr_fxmirae_relink */
    tr_fxv3_run_t fx3;          /* is_ovs 1분봉 표시부. 이식 후 tr_fxv3_run_relink */
    tr_fxpgap_t pgap3, pgap5;   /* 지속목표차 삼선·오선 */
    tr_fxpgap_t rgap;           /* 평탄회귀차 1/5/15/30분 */
    tr_fxpgap_t mgap;           /* 마켓중심차 1/5/15/30분 */
    tr_fxymae_t ymae;
    tr_fxsniper_t sniper;
    tr_osf_combo_t osf; /* 해외선물 1분봉 호가 대체. 국내 파이프는 읽지 않는다 */
    int64_t pgap_key;
    bool pgap_has_key;
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
    /* 확정·정정 봉을 캐시에 남기는 고리. 없으면 호출하지 않는다. */
    void (*candle_fn)(void *ctx, const tr_candle_t *bar);
    void *candle_fn_ctx;
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
            double tick_raw;
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
            bool is_ovs;
            tr_fxmirae_t fx;
            tr_fxv3_run_t fx3;
            tr_fxpgap_t pgap3, pgap5;
            tr_fxpgap_t rgap;
            tr_fxpgap_t mgap;
            tr_fxymae_t ymae;
    tr_fxsniper_t sniper;
    tr_osf_combo_t osf; /* 해외선물 1분봉 호가 대체. 국내 파이프는 읽지 않는다 */
            int64_t pgap_key;
            bool pgap_has_key;
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
TR_ENGINE_PIPE_LAYOUT_CHECK(tick_raw);
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
TR_ENGINE_PIPE_LAYOUT_CHECK(is_ovs);
TR_ENGINE_PIPE_LAYOUT_CHECK(fx);
TR_ENGINE_PIPE_LAYOUT_CHECK(fx3);
TR_ENGINE_PIPE_LAYOUT_CHECK(pgap3);
TR_ENGINE_PIPE_LAYOUT_CHECK(pgap5);
TR_ENGINE_PIPE_LAYOUT_CHECK(rgap);
TR_ENGINE_PIPE_LAYOUT_CHECK(mgap);
TR_ENGINE_PIPE_LAYOUT_CHECK(ymae);
TR_ENGINE_PIPE_LAYOUT_CHECK(sniper);
TR_ENGINE_PIPE_LAYOUT_CHECK(osf);
TR_ENGINE_PIPE_LAYOUT_CHECK(pgap_key);
TR_ENGINE_PIPE_LAYOUT_CHECK(pgap_has_key);
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
                                  double tick_raw, bool is_ovs,
                                  tr_candle_t *bb_storage, size_t bb_capacity,
                                  double *score_mid_storage, size_t score_mid_capacity);
bool tr_engine_pipe_remove(tr_engine_t *e, uint64_t instrument_id);

/* 파이프라인의 1틱 raw 크기 (tick_raw 명시 > 자동: 선물 5, 주식 100).
 * chart.snapshot의 reg_flat 재계산 등 엔진 밖 포맷이 엔진과 같은 규칙을 쓰게 한다. */
double tr_engine_pipe_tick_scale(const tr_pipeline_t *p);

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

/* chart.snapshot의 ind[i] 한 행을 포맷한다. 국내 봉은 32개, fx_on 봉은 57개
 * ([32]=Plot 표시 비트, [33..56]=Plot1..24). 레이아웃은 docs/display_payload.md §2.
 * first=false이면 앞에 쉼표를 붙인다 (배열 연결).
 * ps_flat은 reg_flat 재계산·틱 크기의 raw 단위 (선물 5, 주식 100, 해외선물은 명시 틱).
 * 반환은 snprintf 규약: cap을 넘으면 잘리고 썼어야 할 길이를 돌려준다. */
int tr_bar_status_format_ind(const tr_bar_status_t *st, double ps_flat, bool first,
                             char *buf, size_t cap);

/* chart.snapshot의 fx3[i]. 켜진 Plot의 [번호,값,rgb,두께]. first=false면 앞에 쉼표. */
int tr_bar_status_format_fx3(const tr_bar_status_t *st, bool first, char *buf, size_t cap);

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

/* 확정 봉과 확정 봉의 늦은 정정을 알린다. 무거래 채움 봉은 알리지 않는다. */
typedef void (*tr_engine_candle_fn)(void *ctx, const tr_candle_t *bar);
void tr_engine_set_candle_hook(tr_engine_t *e, tr_engine_candle_fn fn, void *ctx);

/* RT 공백 캐치업 병합 (2026-09-30, RT 재연결 공백 유실 사건 후속):
 * 조회해 온 과거 확정 봉을 봉 링에 **지표 재평가 없이** 병합한다.
 * watch 백필 경로(tr_engine_inject_bar → engine_on_bar)는 봉마다 지표를 돌리므로
 * 라이브 워밍업 상태(회귀 창·호가·⑤ 체인)가 있는 파이프라인에는 쓸 수 없고,
 * tr_bar_builder_inject_bar 자체가 OPEN 봉 존재 시와 최신보다 과거인 봉을 거부해
 * 중간 구멍을 채우지 못한다 — 그래서 링 수준 병합으로 빠진 봉만 삽입한다.
 *
 * 규칙:
 * - bars는 open_time 오름차순(어긋난 행은 건너뛴다). 기존 봉과 open_time이 같으면
 *   기존 봉을 유지한다 (라이브 OPEN 봉·늦은 틱 정정 이력 보호).
 * - 종목·timeframe 불일치, 세션 밖, close_time > now_us(아직 안 닫힌 봉)는 건너뛴다.
 * - 진행 중이던 OPEN 봉보다 새로운 조회 봉이 오면(정체된 OPEN 봉) 그 봉을 제자리에서
 *   닫고(has_open 해제) 병합한다 — 다음 라이브 틱이 새 봉을 열 수 있게 한다.
 * - 상태 링(부착 시)은 봉 링과 인덱스 정합이므로, 삽입된 봉 자리에는 지표 무효 슬롯
 *   (open_time·closed·trading_day만 기록)을 같은 위치에 넣어 정합을 유지한다.
 * - 1봉 이상 삽입되면 generation을 올린다 (대시보드 재시딩 트리거, 계획서 §18).
 * - 지표·스트림 발행에는 아무 영향이 없다 (봉 이벤트를 발생시키지 않는다).
 *
 * scratch는 링 저장소와 겹치지 않는 호출자 소유 버퍼로, 각 링 용량 이상이어야 한다.
 * 상태 링 부착 시 그 용량이 봉 링보다 작으면 병합을 거부하고 0을 돌려준다.
 * 반환: 실제로 삽입된 봉 수. */
size_t tr_engine_pipe_merge_bars(tr_engine_t *e, uint64_t instrument_id,
                                 const tr_candle_t *bars, size_t n, tr_time_us_t now_us,
                                 tr_candle_t *bar_scratch, size_t bar_scratch_cap,
                                 tr_bar_status_t *st_scratch, size_t st_scratch_cap);

/* chart.snapshot의 시간축 공백(gaps) 수집 (2026-09-30 RT 공백 사건 후속, G2):
 * 봉 링의 창 [from, from+take) (back_index 기준) 안에서 시간상 이웃한 두 봉의
 * open 시각 차가 timeframe을 넘고 **같은 세션**이면 구멍으로 기록한다.
 * 같은 세션 판별은 파이프라인의 세션 정책(tr_session_span의 개장 시각 일치)으로 한다
 * — 개장일이 다른 전이(주간→야간 정책 전환·익일 개장 등)는 구멍이 아니다.
 * 거래 없는 분은 수신 유실과 구분할 수 없어 모두 구멍으로 표시한다 (세션 정책상 같은
 * 세션인 휴장 — 선물 15:45~18:00 등 — 도 포함).
 *
 * 각 구간은 [이전 봉 open + timeframe, 다음 봉 open − timeframe] (빈 분의 양 끝, µs)로
 * out에 시각 오름차순으로 채운다. 창의 가장 오래된 봉과 그 직전 봉(창 밖, 링에 있으면)
 * 의 쌍도 검사해 페이지 경계에 걸친 구멍이 빠지지 않게 한다.
 * 반환: 채운 구간 수 (cap까지만 채운다). */
size_t tr_engine_pipe_find_gaps(const tr_engine_t *e, uint64_t instrument_id,
                                size_t from, size_t take, tr_time_us_t (*out)[2], size_t cap);

/* 호가 입력 (H1_/FH9). bids/asks는 총잔량(totbidrem/totofferrem).
 * instrument_id로 파이프라인을 찾아 라우팅한다 (없으면 드롭). */
void tr_engine_on_orderbook(tr_engine_t *e, uint64_t instrument_id,
                            int64_t event_time_us, double bids, double asks);

/* 종목 전환: 지표 상태를 새 종목 기준으로 재구성하고 generation을 올린다 (계획서 §18).
   이전 세대의 늦은 응답과 새 화면이 섞이지 않게 한다. 전략의 거래 대상과는 무관하다(화면 상태 변경).
   session은 새 종목의 세션 정책이다 (필수 — 시장이 다르면 바뀐다). */
bool tr_engine_select_symbol(tr_engine_t *e, uint64_t instrument_id, bool is_futures,
                             const char *shcode, const tr_session_policy_t *session,
                             double tick_raw, bool is_ovs);

#endif
