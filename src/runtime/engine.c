#include "runtime/engine.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/model/civil_time.h"

static void engine_on_bar(void *ctx, const tr_event_envelope_t *env, const tr_candle_t *bar);

/* 원본 PriceScale 대응: raw 가격 단위의 1틱 — tick_raw 명시(해외선물) > 자동(선물 0.05pt×100=5,
 * 주식 1원×100=100). raw는 주식·선물 모두 실제 × 100 스케일 (ls_chart.c parse_price_scaled) */
static double tick_scale(const tr_pipeline_t *p) {
    if (p->tick_raw > 0.0) {
        return p->tick_raw;
    }
    return p->is_futures ? 5.0 : 100.0;
}

double tr_engine_pipe_tick_scale(const tr_pipeline_t *p) {
    return p != 0 ? tick_scale(p) : 100.0;
}

/* 봉 시작 시각 → 예스랭귀지 sDate(yyyymmdd) / sTime(hhmmss). 세션 오프셋의 현지 시각. */
static void civil_stamp(tr_time_us_t t, int32_t utc_offset_min, int64_t *date, int64_t *tod) {
    tr_civil_t c;
    if (!tr_civil_from_time_us(t, utc_offset_min, &c)) {
        *date = 0;
        *tod = 0;
        return;
    }
    *date = (int64_t)c.year * 10000 + (int64_t)c.month * 100 + (int64_t)c.day;
    *tod = (int64_t)c.hour * 10000 + (int64_t)c.min * 100 + (int64_t)c.sec;
}

/* shcode를 JSON 안전 문자(영숫자)만 남겨 복사한다. 페이로드에 그대로 실리므로
 * 따옴표·역슬래시 같은 문자는 걸러낸다 (LS 종목코드는 영숫자). */
static void sanitize_shcode(char dst[16], const char *src) {
    size_t n = 0;
    if (src != 0) {
        for (const char *s = src; *s != '\0' && n < 15; s++) {
            char c = *s;
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                dst[n++] = c;
            }
        }
    }
    dst[n] = '\0';
}

/* 파이프라인 1개를 cfg 기준으로 초기화한다. 지표 상태는 모두 리셋되고 generation은 1.
 * 저장소(봉 링·점수 중간값)는 호출자 소유다. */
static bool pipe_init(tr_engine_t *e, tr_pipeline_t *p, const tr_engine_config_t *cfg,
                      tr_candle_t *bb_storage, size_t bb_capacity,
                      double *score_mid_storage, size_t score_mid_capacity) {
    if (e == 0 || p == 0 || cfg == 0 || bb_storage == 0 || score_mid_storage == 0 ||
        bb_capacity == 0 || score_mid_capacity < cfg->market_period) {
        return false;
    }
    memset(&p->bb, 0, sizeof(p->bb));
    p->engine = e;
    p->instrument_id = cfg->instrument_id;
    sanitize_shcode(p->shcode, cfg->shcode);
    p->is_futures = cfg->is_futures;
    p->session = cfg->session; /* 종목별 세션 — 봉 구축·지표 컨텍스트가 여기서 읽는다 */
    p->tick_raw = cfg->tick_raw; /* 종목별 틱 크기 (0이면 자동 — tick_scale 참조) */
    p->bb_storage = bb_storage;
    p->bb_capacity = bb_capacity;
    p->score_mid_storage = score_mid_storage;
    p->score_mid_capacity = score_mid_capacity;
    p->generation = 1;
    p->prev_trading_day = 0;
    p->has_prev_day = false;
    p->prev_bar_open = 0;
    p->has_prev_bar = false;
    p->status_ring_on = false;
    p->mkt_on = false; /* 미부착 기본값 (attach_market 성공 시에만 true) */

    tr_bar_builder_config_t bbcfg;
    memset(&bbcfg, 0, sizeof(bbcfg));
    bbcfg.instrument_id = cfg->instrument_id;
    bbcfg.timeframe_sec = cfg->timeframe_sec;
    bbcfg.session = cfg->session;
    bbcfg.no_trade = cfg->no_trade;
    bbcfg.ring_storage = bb_storage;
    bbcfg.ring_capacity = bb_capacity;
    bbcfg.engine_instance_id = cfg->engine_instance_id;
    bbcfg.source_id = 1;
    bbcfg.on_event = engine_on_bar;
    bbcfg.on_event_ctx = p;
    if (!tr_bar_builder_init(&p->bb, &bbcfg)) {
        return false;
    }
    if (!tr_lr3_init(&p->lr3, TR_COMPRESS_MIN, cfg->timeframe_sec / 60,
                     cfg->predict_bars[0], cfg->predict_bars[1], cfg->predict_bars[2])) {
        return false;
    }
    if (!tr_htf_curve_init(&p->htf, cfg->htf_ticks)) {
        return false;
    }
    if (!tr_score1m_init(&p->score, cfg->market_period, cfg->min_r2,
                         score_mid_storage, score_mid_capacity)) {
        return false;
    }
    /* 호가 지표: 원본 기본값 관심 10 / 강세 35, 부호반전 없음 */
    if (!tr_obd2_init(&p->obd2, 10.0, 35.0, false, cfg->is_futures)) {
        return false;
    }
    /* ⑥ 방향 기억·⑦ 지속 사진 (메인 원본 기본값: 가파른기울기틱 4, 추가확인봉 1, 지속봉수 5).
     * price_scale은 원본 틱 양자화 대응 (tick_scale 헬퍼 참조) */
    {
        double ps = tick_scale(p);
        tr_regmem_config_t rcfg = {ps, 4.0, 1, true};
        tr_regmem_init(&p->regmem, &rcfg);
        tr_persist_config_t pcfg = {ps, 5, cfg->min_r2};
        tr_persist_init(&p->persist, &pcfg);
        /* ⑤ 매매 상태 체인 (원본 기본값: 갭 변동기간 10은 원본 호출부 고정 리터럴) */
        tr_dtl1_config_t dcfg = {cfg->daily_reg_period, cfg->min_r2, ps};
        tr_dtl1_init(&p->dtl1, &dcfg);
        tr_gap1_config_t gcfg = {10, cfg->gap_mid, cfg->gap_big};
        tr_gap1_init(&p->gap1, &gcfg);
        memset(&p->dalign, 0, sizeof(p->dalign));
        p->bar_index = 0;
    }
    /* 이평선 5/20/60 (게이트 없음 — 모든 timeframe에서 평가) */
    tr_sma_init(&p->sma5, 5);
    tr_sma_init(&p->sma20, 20);
    tr_sma_init(&p->sma60, 60);
    /* 해외선물 미래곡선 V1. price_scale은 raw 틱 (국내 엔진의 tick_scale과 같은 단위) */
    p->is_ovs = cfg->is_ovs;
    if (cfg->is_ovs) {
        tr_fxmirae_config_t fcfg;
        tr_fxmirae_default_config(&fcfg, tick_scale(p));
        if (!tr_fxmirae_init(&p->fx, &fcfg)) {
            return false;
        }
        if (!tr_fxv3_run_init(&p->fx3, tick_scale(p))) {
            return false;
        }
        tr_fxpgap_init(&p->pgap3, 3, 25);
        tr_fxpgap_init(&p->pgap5, 5, 25);
        tr_fxpgap_init(&p->rgap, 4, 25);
        tr_fxpgap_init(&p->mgap, 4, 25);
        tr_fxymae_config_t ycfg;
        tr_fxymae_default_config(&ycfg, tick_scale(p));
        tr_fxymae_init(&p->ymae, &ycfg);
        tr_fxsniper_config_t scfg;
        tr_fxsniper_default_config(&scfg, tick_scale(p));
        tr_fxsniper_init(&p->sniper, &scfg);
        /* 호가 모듈과 같은 관심 10 / 강세 35. 흐름 기간 20은 호출 원본이 없어 엔진 기본값. */
        tr_osf_combo_init(&p->osf, 10.0, 35.0, 0, 20);
    }
    /* 국내선물 1분 스나이퍼 Data2. 옵션·주식·해외선물은 이 상태를 평가하지 않는다. */
    p->ks_bdate = 0;
    p->ks_day_index = 0;
    p->ks_current = 0;
    p->ks_has_date = false;
    if (cfg->is_futures && !cfg->is_ovs) {
        tr_ksscore_config_t kcfg;
        tr_ksscore_default_config(&kcfg, tick_scale(p));
        if (!tr_ksscore_init(&p->ks, &kcfg)) {
            return false;
        }
    }
    return true;
}

bool tr_engine_init(tr_engine_t *e, const tr_engine_config_t *cfg,
                    tr_candle_t *bb_storage, size_t bb_capacity,
                    double *score_mid_storage, size_t score_mid_capacity) {
    if (e == 0 || cfg == 0 || bb_storage == 0 || score_mid_storage == 0 ||
        bb_capacity == 0 || score_mid_capacity < cfg->market_period) {
        return false;
    }
    e->cfg = *cfg;
    e->ipc = 0;
    e->status_cb = 0;
    e->status_cb_ctx = 0;
    e->stream_id = "display";
    e->status_seq = 1;
    e->candle_fn = 0;
    e->candle_fn_ctx = 0;
    /* 단일 종목 호환 경로: 파이프라인 1개(내장 pipe0)를 만들어 부착한다 */
    e->pipes[0] = &e->pipe0;
    e->pipe_count = 1;
    return pipe_init(e, &e->pipe0, cfg, bb_storage, bb_capacity,
                     score_mid_storage, score_mid_capacity);
}

tr_pipeline_t *tr_engine_pipe_find(tr_engine_t *e, uint64_t instrument_id) {
    if (e == 0) {
        return 0;
    }
    for (int i = 0; i < e->pipe_count; i++) {
        if (e->pipes[i]->instrument_id == instrument_id) {
            return e->pipes[i];
        }
    }
    return 0;
}

/* pipes[1..]가 참조하지 않는 첫 내장 슬롯을 찾는다 (remove로 빈 슬롯 재사용).
 * 파이프라인 객체는 슬롯에 고정된다 — 반환된 tr_pipeline_t*는 remove되기 전까지 유효. */
static tr_pipeline_t *pipe_free_slot(tr_engine_t *e) {
    for (int s = 0; s < TR_ENGINE_MAX_PIPES - 1; s++) {
        bool used = false;
        for (int i = 1; i < e->pipe_count; i++) {
            if (e->pipes[i] == &e->pipe_slots[s]) {
                used = true;
                break;
            }
        }
        if (!used) {
            return &e->pipe_slots[s];
        }
    }
    return 0;
}

tr_pipeline_t *tr_engine_pipe_add(tr_engine_t *e, uint64_t instrument_id, bool is_futures,
                                  const char *shcode, const tr_session_policy_t *session,
                                  double tick_raw, bool is_ovs,
                                  tr_candle_t *bb_storage, size_t bb_capacity,
                                  double *score_mid_storage, size_t score_mid_capacity) {
    if (e == 0 || instrument_id == 0 || session == 0) {
        return 0;
    }
    tr_pipeline_t *found = tr_engine_pipe_find(e, instrument_id);
    if (found != 0) {
        return found;
    }
    if (e->pipe_count >= TR_ENGINE_MAX_PIPES) {
        return 0;
    }
    tr_engine_config_t cfg = e->cfg;
    cfg.instrument_id = instrument_id;
    sanitize_shcode(cfg.shcode, shcode);
    cfg.is_futures = is_futures;
    cfg.session = *session; /* 기동 종목 세션을 상속하지 않고 이 종목의 세션을 쓴다 */
    cfg.tick_raw = tick_raw;  /* 틱 크기도 종목별로 넘긴다 (해외선물은 자동 규칙이 다르다) */
    cfg.is_ovs = is_ovs;
    tr_pipeline_t *p = pipe_free_slot(e);
    if (p == 0) {
        return 0;
    }
    if (!pipe_init(e, p, &cfg, bb_storage, bb_capacity, score_mid_storage, score_mid_capacity)) {
        return 0;
    }
    e->pipes[e->pipe_count] = p;
    e->pipe_count++;
    return p;
}

bool tr_engine_pipe_remove(tr_engine_t *e, uint64_t instrument_id) {
    if (e == 0 || e->pipe_count <= 1) {
        return false; /* 마지막 1개는 제거하지 않는다 */
    }
    /* 대상이 pipes[0]이면: pipe0은 익명 뷰(e->bb 등)의 기반이라 주소가 고정되어 있어
     * 목록에서 빼는 대신, 마지막 파이프라인의 내용을 pipe0에 통째로 이식하고 그 슬롯을
     * 비운다. 지표 상태·저장소 포인터·shcode·세션 정책이 모두 따라오므로 생존 파이프라인은
     * 끊기지 않는다. 이 경우 pipes[] 순서는 보존되지 않는다. */
    if (e->pipes[0]->instrument_id == instrument_id) {
        tr_pipeline_t *victim = e->pipes[e->pipe_count - 1];
        e->pipe0 = *victim;
        /* 자기참조 복구: 통째 복사로 따라온 포인터가 기증 슬롯을 가리키지 않게
         * pipe0 자신을/자신의 버퍼를 가리키게 다시 연결한다 */
        e->pipe0.engine = e;
        e->pipe0.bb.cfg.on_event_ctx = &e->pipe0;
        /* yl_var ring.storage 복구 (var.h 값 복사 불안전). lr3는 v4(→ATR)까지 cascade */
        tr_lr3_relink(&e->pipe0.lr3);
        tr_obd2_relink(&e->pipe0.obd2);
        tr_htf_curve_relink(&e->pipe0.htf);
        tr_sma_relink(&e->pipe0.sma5);
        tr_sma_relink(&e->pipe0.sma20);
        tr_sma_relink(&e->pipe0.sma60);
        tr_gap1_relink(&e->pipe0.gap1);
        tr_dtl1_relink(&e->pipe0.dtl1);
        if (e->pipe0.is_ovs) {
            tr_fxmirae_relink(&e->pipe0.fx);
            tr_fxv3_run_relink(&e->pipe0.fx3);
        }
        if (e->pipe0.is_futures && !e->pipe0.is_ovs) {
            tr_ksscore_relink(&e->pipe0.ks);
        }
        /* score의 mid_hist(yl_var)는 relink 불필요: 저장소가 기증 슬롯 남부가 아니라
         * 호출자 소유 외부 버퍼(score_mid_storage)라 복사된 포인터가 그대로 올바르고,
         * 슬롯 재사용(add)도 새 파이프라인 자신의 저장소 인자를 쓴다 (score_1m.h 참조) */
        e->cfg.instrument_id = e->pipe0.instrument_id;
        e->cfg.is_futures = e->pipe0.is_futures;
        e->cfg.is_ovs = e->pipe0.is_ovs;
        e->cfg.tick_raw = e->pipe0.tick_raw;
        e->cfg.session = e->pipe0.session;
        snprintf(e->cfg.shcode, sizeof(e->cfg.shcode), "%s", e->pipe0.shcode);
        e->pipe_count--;
        return true;
    }
    for (int i = 1; i < e->pipe_count; i++) {
        if (e->pipes[i]->instrument_id == instrument_id) {
            /* compact: 뒤를 한 칸씩 당겨 pipes[] 순서를 보존한다. 파이프라인 객체는
             * 이동하지 않으므로 다른 파이프라인의 포인터는 유효하다. 빈 슬롯은
             * 다음 add의 pipe_free_slot이 재사용한다 */
            for (int j = i; j + 1 < e->pipe_count; j++) {
                e->pipes[j] = e->pipes[j + 1];
            }
            e->pipe_count--;
            return true;
        }
    }
    return false;
}

void tr_engine_attach_ipc(tr_engine_t *e, tr_ipc_t *ipc, const char *stream_id) {
    if (e == 0) {
        return;
    }
    e->ipc = ipc;
    if (stream_id != 0) {
        e->stream_id = stream_id;
    }
}

void tr_engine_attach_status_cb(tr_engine_t *e, tr_engine_status_fn cb, void *ctx) {
    if (e == 0) {
        return;
    }
    e->status_cb = cb;
    e->status_cb_ctx = ctx;
}

bool tr_engine_pipe_attach_status_ring(tr_engine_t *e, uint64_t instrument_id,
                                       tr_bar_status_t *storage, size_t capacity) {
    if (storage == 0 || capacity == 0) {
        return false;
    }
    tr_pipeline_t *p = tr_engine_pipe_find(e, instrument_id);
    if (p == 0) {
        return false;
    }
    if (!tr_ring_init(&p->status_ring, storage, sizeof(tr_bar_status_t), capacity)) {
        return false;
    }
    p->status_ring_on = true;
    return true;
}

bool tr_engine_attach_status_ring(tr_engine_t *e, tr_bar_status_t *storage, size_t capacity) {
    if (e == 0) {
        return false;
    }
    return tr_engine_pipe_attach_status_ring(e, e->pipes[0]->instrument_id, storage, capacity);
}

size_t tr_engine_pipe_status_count(const tr_engine_t *e, uint64_t instrument_id) {
    if (e == 0) {
        return 0;
    }
    for (int i = 0; i < e->pipe_count; i++) {
        if (e->pipes[i]->instrument_id == instrument_id) {
            return e->pipes[i]->status_ring_on ? tr_ring_count(&e->pipes[i]->status_ring) : 0;
        }
    }
    return 0;
}

size_t tr_engine_status_count(const tr_engine_t *e) {
    return (e != 0 && e->pipe0.status_ring_on) ? tr_ring_count(&e->pipe0.status_ring) : 0;
}

bool tr_engine_pipe_status_at(const tr_engine_t *e, uint64_t instrument_id,
                              size_t back_index, tr_bar_status_t *out) {
    if (e == 0 || out == 0) {
        return false;
    }
    for (int i = 0; i < e->pipe_count; i++) {
        if (e->pipes[i]->instrument_id == instrument_id) {
            return e->pipes[i]->status_ring_on
                       ? tr_ring_at(&e->pipes[i]->status_ring, back_index, out)
                       : false;
        }
    }
    return false;
}

bool tr_engine_status_at(const tr_engine_t *e, size_t back_index, tr_bar_status_t *out) {
    if (e == 0 || out == 0 || !e->pipe0.status_ring_on) {
        return false;
    }
    return tr_ring_at(&e->pipe0.status_ring, back_index, out);
}

bool tr_engine_pipe_attach_market(tr_engine_t *e, uint64_t instrument_id,
                                  tr_candle_t *storage, size_t capacity) {
    if (e == 0 || storage == 0 || capacity == 0) {
        return false;
    }
    tr_pipeline_t *p = tr_engine_pipe_find(e, instrument_id);
    if (p == 0) {
        return false;
    }
    double ps = tick_scale(p);
    if (!tr_market_init(&p->mkt, e->cfg.market_period, 1.0, ps, storage, capacity)) {
        return false;
    }
    p->mkt_on = true;
    return true;
}

bool tr_engine_attach_market(tr_engine_t *e, tr_candle_t *storage, size_t capacity) {
    if (e == 0) {
        return false;
    }
    return tr_engine_pipe_attach_market(e, e->pipes[0]->instrument_id, storage, capacity);
}

static void publish_status(tr_engine_t *e, tr_pipeline_t *p, const tr_candle_t *bar,
                           bool closed, int64_t trading_day,
                           int fx_on, uint32_t fx_mask, const double fx_plot[TR_FXMIRAE_PLOTS],
                           int fx3_on, int fx3_n, const uint16_t *fx3_id, const double *fx3_v,
                           const uint32_t *fx3_rgb, const uint8_t *fx3_w,
                           const tr_fxpgap_out_t *pg3, const tr_fxpgap_out_t *pg5,
                           const tr_fxpgap_out_t *rg, const tr_fxpgap_out_t *mg,
                           const tr_fxymae_out_t *ym, const tr_fxsniper_out_t *sn, int emit_pvc,
                           int ray_on, int ray_sign, const double *ray_px, const double *ray_up,
                           const double *ray_dn, const int8_t *ray_dir,
                           int ob_valid, double ob_score, const tr_bar_status_t *frozen) {
    const tr_lr3_t *r = &p->lr3;
    const tr_score1m_t *sc = &p->score;
    const tr_market_t *m = &p->mkt;
    const tr_regmem_t *rm = &p->regmem;
    const tr_persist_t *ps = &p->persist;
    const tr_dalign2_output_t *fa = &p->dalign;
    /* 곡선회귀선_평탄: 회귀선을 틱 단위로 반올림 (tick_scale 헬퍼 참조) */
    double pscale = tick_scale(p);
    bool reg_valid_v = r->reg_valid;
    double line_v = r->line;
    double slope_v = r->slope;
    double r2_v = r->r2;
    double pred_v[3] = {r->v4.pred_price[0], r->v4.pred_price[1], r->v4.pred_price[2]};
    int pred_dir_v[3] = {r->v4.pred_dir[0], r->v4.pred_dir[1], r->v4.pred_dir[2]};
    double resid_v = r->residual;
    double pvol_v = r->v4.volatility;
    int score_v = sc->score;
    int ob_dir_v = sc->ob_dir;
    int mkt_valid_v = (p->mkt_on && m->valid) ? 1 : 0;
    double mkt_c = m->center, mkt_u1 = m->upper1, mkt_l1 = m->lower1, mkt_u2 = m->upper2, mkt_l2 = m->lower2;
    int mem_valid_v = rm->mem_valid ? 1 : 0;
    int mem_updated_v = rm->updated ? 1 : 0;
    int mem_dir_v = rm->mem_dir;
    double mem_price_v = rm->mem_price;
    double mem_t[3], mem_u[3], mem_l[3];
    memcpy(mem_t, rm->mem_target, sizeof(mem_t));
    memcpy(mem_u, rm->mem_upper, sizeof(mem_u));
    memcpy(mem_l, rm->mem_lower, sizeof(mem_l));
    int pst_saved_v = ps->streak == ps->cfg.persist_bars && ps->saved_valid ? 1 : 0;
    int pst_valid_v = ps->saved_valid ? 1 : 0;
    int pst_dir_v = ps->saved_dir;
    double pst_t[3], pst_u[3], pst_l[3];
    memcpy(pst_t, ps->target, sizeof(pst_t));
    memcpy(pst_u, ps->upper, sizeof(pst_u));
    memcpy(pst_l, ps->lower, sizeof(pst_l));
    int final_valid_v = fa->final_valid ? 1 : 0;
    int final_dir_v = fa->final_dir;
    int final_state_v = fa->final_state;
    int final_strength_v = (int)fa->final_strength;
    int sma_ok = (p->sma5.valid && p->sma20.valid && p->sma60.valid) ? 1 : 0;
    double sma_v[3] = {p->sma5.value, p->sma20.value, p->sma60.value};
    int64_t day_v = trading_day;
    int closed_v = closed ? 1 : 0;
    if (frozen != 0) {
        reg_valid_v = frozen->reg_valid;
        line_v = frozen->reg_line;
        slope_v = frozen->reg_slope;
        r2_v = frozen->reg_r2;
        memcpy(pred_v, frozen->pred, sizeof(pred_v));
        pred_dir_v[0] = frozen->pred_dir[0];
        pred_dir_v[1] = frozen->pred_dir[1];
        pred_dir_v[2] = frozen->pred_dir[2];
        resid_v = frozen->residual;
        pvol_v = frozen->pvol;
        score_v = frozen->score;
        ob_dir_v = (ob_score > 0.0) - (ob_score < 0.0);
        mkt_valid_v = frozen->mkt_valid ? 1 : 0;
        mkt_c = frozen->mkt_center;
        mkt_u1 = frozen->mkt_u1;
        mkt_l1 = frozen->mkt_l1;
        mkt_u2 = frozen->mkt_u2;
        mkt_l2 = frozen->mkt_l2;
        mem_valid_v = frozen->mem_valid ? 1 : 0;
        mem_updated_v = frozen->mem_updated ? 1 : 0;
        mem_dir_v = frozen->mem_dir;
        mem_price_v = frozen->mem_price;
        memcpy(mem_t, frozen->mem_target, sizeof(mem_t));
        memcpy(mem_u, frozen->mem_upper, sizeof(mem_u));
        memcpy(mem_l, frozen->mem_lower, sizeof(mem_l));
        pst_saved_v = frozen->pst_saved ? 1 : 0;
        pst_valid_v = frozen->pst_valid ? 1 : 0;
        pst_dir_v = frozen->pst_dir;
        memcpy(pst_t, frozen->pst_target, sizeof(pst_t));
        memcpy(pst_u, frozen->pst_upper, sizeof(pst_u));
        memcpy(pst_l, frozen->pst_lower, sizeof(pst_l));
        final_valid_v = frozen->final_valid;
        final_dir_v = frozen->final_dir;
        final_state_v = frozen->final_state;
        final_strength_v = frozen->final_strength;
        sma_ok = frozen->sma_valid;
        sma_v[0] = frozen->sma[0];
        sma_v[1] = frozen->sma[1];
        sma_v[2] = frozen->sma[2];
        day_v = frozen->trading_day;
        closed_v = frozen->closed ? 1 : closed_v;
    }
    double reg_flat = floor(line_v / pscale + 0.5) * pscale;
    /* fx는 shcode 앞에만 붙인다. 국내 봉은 빈 문자열이라 기존 키 순서가 그대로다 */
    char fxbuf[1024];
    fxbuf[0] = '\0';
    if (fx_on) {
        int m = snprintf(fxbuf, sizeof(fxbuf), "\"fx\":[%u", fx_mask);
        for (int k = 0; k < TR_FXMIRAE_PLOTS && m > 0 && (size_t)m < sizeof(fxbuf); k++) {
            m += snprintf(fxbuf + m, sizeof(fxbuf) - (size_t)m, ",%.10g", fx_plot[k]);
        }
        if (m > 0 && (size_t)m < sizeof(fxbuf)) {
            m += snprintf(fxbuf + m, sizeof(fxbuf) - (size_t)m, "],");
        }
        if (m < 0 || (size_t)m >= sizeof(fxbuf)) {
            return; /* 잘린 fx는 발행하지 않는다 */
        }
    }
    char fx3buf[3072];
    fx3buf[0] = '\0';
    if (fx3_on && fx3_n > 0 && fx3_id != 0 && fx3_v != 0 && fx3_rgb != 0 && fx3_w != 0) {
        int m = snprintf(fx3buf, sizeof(fx3buf), "\"fx3\":[");
        int nuse = fx3_n < TR_FX3_WIRE ? fx3_n : TR_FX3_WIRE;
        for (int i = 0; i < nuse && m > 0 && (size_t)m < sizeof(fx3buf); i++) {
            m += snprintf(fx3buf + m, sizeof(fx3buf) - (size_t)m, "%s[%u,%.10g,%u,%u]",
                          i > 0 ? "," : "", (unsigned)fx3_id[i], fx3_v[i], fx3_rgb[i],
                          (unsigned)fx3_w[i]);
        }
        if (m > 0 && (size_t)m < sizeof(fx3buf)) {
            m += snprintf(fx3buf + m, sizeof(fx3buf) - (size_t)m, "],");
        }
        if (m < 0 || (size_t)m >= sizeof(fx3buf)) {
            fx3buf[0] = '\0';
        }
    }
    char pgbuf[700];
    pgbuf[0] = '\0';
    if (pg3 != 0 && pg5 != 0) {
        const tr_fxpgap_out_t *pg[2] = {pg3, pg5};
        int m = snprintf(pgbuf, sizeof(pgbuf), "\"pgap\":[");
        for (int i = 0; i < 2 && m > 0 && (size_t)m < sizeof(pgbuf); i++) {
            const tr_fxpgap_out_t *o = pg[i];
            m += snprintf(pgbuf + m, sizeof(pgbuf) - (size_t)m,
                          "%s[%d,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%u,%d,%u,%d,%d,%u,%d]",
                          i > 0 ? "," : "", o->ready, o->gap, o->peak, o->prev_peak, o->ratio,
                          o->prev_ratio, o->cnt, o->rgb4, o->width4, o->rgb5, o->plot3, o->plot5,
                          o->union_rgb, o->union_w);
        }
        if (m > 0 && (size_t)m < sizeof(pgbuf)) {
            m += snprintf(pgbuf + m, sizeof(pgbuf) - (size_t)m, "],");
        }
        if (m < 0 || (size_t)m >= sizeof(pgbuf)) {
            pgbuf[0] = '\0';
        }
    }
    char rgbuf[360];
    rgbuf[0] = '\0';
    if (rg != 0) {
        int m = snprintf(rgbuf, sizeof(rgbuf),
                         "\"rgap\":[%d,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%u,%d,%u,%d,%d],",
                         rg->ready, rg->gap, rg->peak, rg->prev_peak, rg->ratio, rg->prev_ratio,
                         rg->cnt, rg->rgb4, rg->width4, rg->rgb5, rg->plot3, rg->plot5);
        if (m < 0 || (size_t)m >= sizeof(rgbuf)) {
            rgbuf[0] = '\0';
        }
    }
    char mgbuf[360];
    mgbuf[0] = '\0';
    if (mg != 0) {
        int m = snprintf(mgbuf, sizeof(mgbuf),
                         "\"mgap\":[%d,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%u,%d,%u,%d,%d],",
                         mg->ready, mg->gap, mg->peak, mg->prev_peak, mg->ratio, mg->prev_ratio,
                         mg->cnt, mg->rgb4, mg->width4, mg->rgb5, mg->plot3, mg->plot5);
        if (m < 0 || (size_t)m >= sizeof(mgbuf)) {
            mgbuf[0] = '\0';
        }
    }
    char ymbuf[420];
    ymbuf[0] = '\0';
    if (ym != 0) {
        int m = snprintf(ymbuf, sizeof(ymbuf),
                         "\"ymae\":[%d,%d,%.10g,%.10g,%.10g,%.10g,%d,%d,%d,%d,%.10g,%.10g,%.10g,%.10g,%d],",
                         ym->pos, ym->prev_valid, ym->prev_hi, ym->prev_lo, ym->two_hi, ym->two_lo,
                         ym->show_h1, ym->show_h2, ym->show_h3, ym->show_h4, ym->mark_h1, ym->mark_h2,
                         ym->mark_h3, ym->mark_h4, ym->show_first);
        if (m < 0 || (size_t)m >= sizeof(ymbuf)) {
            ymbuf[0] = '\0';
        }
    }
    char snbuf[280];
    snbuf[0] = '\0';
    if (sn != 0) {
        int m = snprintf(snbuf, sizeof(snbuf),
                         "\"sniper\":[%d,%d,%d,%d,%d,%.10g,%.10g,%u,%d,%d,%d,%d],",
                         sn->score, sn->score_ex, sn->ratio_score, sn->stage, sn->compound,
                         sn->target_ratio, sn->price_ratio, sn->rgb, sn->px_exit, sn->below,
                         sn->above, sn->session_reset);
        if (m < 0 || (size_t)m >= sizeof(snbuf)) {
            snbuf[0] = '\0';
        }
    }
    char pvcbuf[160];
    pvcbuf[0] = '\0';
    if (emit_pvc && ym != 0) {
        int m = snprintf(pvcbuf, sizeof(pvcbuf),
                         "\"pvc\":[%d,%.10g,%d,%.10g,%d],",
                         ym->show_price, ym->show_price_ratio, ym->show_vol, ym->show_vol_ratio,
                         ym->show_both);
        if (m < 0 || (size_t)m >= sizeof(pvcbuf)) {
            pvcbuf[0] = '\0';
        }
    }
    char raybuf[700];
    raybuf[0] = '\0';
    if (ray_on && ray_px != 0 && ray_up != 0 && ray_dn != 0 && ray_dir != 0) {
        int m = snprintf(raybuf, sizeof(raybuf), "\"rays\":[%d", ray_sign);
        for (int i = 0; i < 5 && m > 0 && (size_t)m < sizeof(raybuf); i++) {
            m += snprintf(raybuf + m, sizeof(raybuf) - (size_t)m, ",[%.10g,%.10g,%.10g,%d]",
                          ray_px[i], ray_up[i], ray_dn[i], (int)ray_dir[i]);
        }
        if (m > 0 && (size_t)m < sizeof(raybuf)) {
            m += snprintf(raybuf + m, sizeof(raybuf) - (size_t)m, "],");
        }
        if (m < 0 || (size_t)m >= sizeof(raybuf)) {
            raybuf[0] = '\0';
        }
    } else if (ray_px != 0) {
        snprintf(raybuf, sizeof(raybuf), "\"rays\":[],");
    }
    char payload[8192];
    int n = snprintf(payload, sizeof(payload),
        "{\"bar_open_time\":\"%lld\",\"closed\":%d,"
        "\"ohlc\":[%lld,%lld,%lld,%lld],"
        "\"reg_valid\":%d,\"reg_line\":%.10g,\"reg_slope\":%.10g,\"reg_r2\":%.10g,"
        "\"pred\":[%.10g,%.10g,%.10g],\"pred_dir\":[%d,%d,%d],"
        "\"resid\":%.10g,\"pvol\":%.10g,"
        "\"score\":%d,\"future_dir\":%.10g,\"market_dir\":%d,\"reg_dir\":%d,\"ob_dir\":%d,"
        "\"ob_valid\":%d,\"ob_score\":%.10g,\"generation\":%u,"
        "\"mkt\":[%d,%.10g,%.10g,%.10g,%.10g,%.10g],"
        "\"mem\":[%d,%d,%d,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g],"
        "\"pst\":[%d,%d,%d,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g],"
        "\"final\":[%d,%d,%d,%d],\"reg_flat\":%.10g,\"tick\":%g,\"day\":%lld,"
        "\"sma\":[%d,%.10g,%.10g,%.10g],%s%s%s%s%s%s%s%s%s\"shcode\":\"%s\"}",
        (long long)bar->open_time_us, closed_v,
        (long long)bar->open, (long long)bar->high, (long long)bar->low, (long long)bar->close,
        reg_valid_v ? 1 : 0, line_v, slope_v, r2_v,
        pred_v[0], pred_v[1], pred_v[2],
        pred_dir_v[0], pred_dir_v[1], pred_dir_v[2],
        resid_v, pvol_v,
        score_v, sc->future_dir, sc->market_dir, sc->reg_dir, ob_dir_v,
        ob_valid, ob_score,
        p->generation,
        mkt_valid_v, mkt_c, mkt_u1, mkt_l1, mkt_u2, mkt_l2,
        mem_valid_v, mem_updated_v, mem_dir_v, mem_price_v,
        mem_t[0], mem_t[1], mem_t[2],
        mem_u[0], mem_u[1], mem_u[2],
        mem_l[0], mem_l[1], mem_l[2],
        pst_saved_v, pst_valid_v, pst_dir_v,
        pst_t[0], pst_t[1], pst_t[2],
        pst_u[0], pst_u[1], pst_u[2],
        pst_l[0], pst_l[1], pst_l[2],
        final_valid_v, final_dir_v, final_state_v, final_strength_v,
        reg_flat, tick_scale(p), (long long)day_v,
        sma_ok, sma_v[0], sma_v[1], sma_v[2], fxbuf, fx3buf, pgbuf, rgbuf, mgbuf, ymbuf, snbuf, pvcbuf, raybuf, p->shcode);
    if (n <= 0 || (size_t)n >= sizeof(payload)) {
        return;
    }
    if (e->ipc != 0) {
        tr_ipc_publish(e->ipc, e->stream_id, e->status_seq, bar->close_time_us, payload);
    }
    if (e->status_cb != 0) {
        e->status_cb(e->status_cb_ctx, e->stream_id, e->status_seq, payload);
    }
    e->status_seq++;
}

int tr_bar_status_format_ind(const tr_bar_status_t *st, double ps_flat, bool first,
                             char *buf, size_t cap) {
    if (st == 0 || buf == 0 || cap == 0) {
        return 0;
    }
    int n = snprintf(buf, cap,
                     "%s[%d,%d,%.10g,%.10g,%.10g,%.10g,%.10g,%d,%d,%.10g,%.10g,%.10g,%d,%d,%d,"
                     "%d,%.10g,%.10g,%.10g,%.10g,%.10g,%d,%d,%d,%d,%.10g,%d,%lld,%d,%.10g,%.10g,%.10g",
                     first ? "" : ",",
                     st->closed ? 1 : 0, st->reg_valid ? 1 : 0, st->reg_line, st->reg_r2,
                     st->pred[0], st->pred[1], st->pred[2],
                     st->score, st->ob_valid ? 1 : 0, st->ob_score,
                     st->residual, st->pvol,
                     st->pred_dir[0], st->pred_dir[1], st->pred_dir[2],
                     st->mkt_valid ? 1 : 0, st->mkt_center, st->mkt_u1, st->mkt_l1,
                     st->mkt_u2, st->mkt_l2,
                     st->final_valid, st->final_dir, st->final_state, st->final_strength,
                     floor(st->reg_line / ps_flat + 0.5) * ps_flat,
                     (int)ps_flat, (long long)st->trading_day,
                     st->sma_valid, st->sma[0], st->sma[1], st->sma[2]);
    if (n < 0) {
        return n;
    }
    if (st->fx_on) {
        int m = 0;
        if ((size_t)n < cap) {
            m = snprintf(buf + n, cap - (size_t)n, ",%u", st->fx_mask);
        } else {
            m = snprintf(0, 0, ",%u", st->fx_mask);
        }
        if (m < 0) {
            return m;
        }
        n += m;
        for (int k = 0; k < TR_FXMIRAE_PLOTS; k++) {
            if ((size_t)n < cap) {
                m = snprintf(buf + n, cap - (size_t)n, ",%.10g", st->fx_plot[k]);
            } else {
                m = snprintf(0, 0, ",%.10g", st->fx_plot[k]);
            }
            if (m < 0) {
                return m;
            }
            n += m;
        }
    }
    if ((size_t)n < cap) {
        int m = snprintf(buf + n, cap - (size_t)n, "]");
        if (m < 0) {
            return m;
        }
        n += m;
    } else {
        n += 1; /* 닫는 괄호 */
    }
    return n;
}

int tr_bar_status_format_mem(const tr_bar_status_t *st, bool first, char *buf, size_t cap) {
    if (st == 0 || buf == 0 || cap == 0) {
        return 0;
    }
    /* 이벤트가 아닌 봉(updated도 세션 리셋도 아님)은 쓰지 않고 0을 돌려준다 */
    if (!st->mem_updated && !st->mem_reset) {
        return 0;
    }
    return snprintf(buf, cap,
                    "%s[%lld,%d,%d,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%d,%d,%d,%d]",
                    first ? "" : ",", (long long)st->open_time_us,
                    st->mem_valid ? 1 : 0, st->mem_dir, st->mem_price,
                    st->mem_target[0], st->mem_target[1], st->mem_target[2],
                    st->mem_upper[0], st->mem_upper[1], st->mem_upper[2],
                    st->mem_lower[0], st->mem_lower[1], st->mem_lower[2],
                    st->mem_show_targets ? 1 : 0, st->mem_show_upper ? 1 : 0,
                    st->mem_show_lower ? 1 : 0, st->mem_reset ? 1 : 0);
}

int tr_bar_status_format_fx3(const tr_bar_status_t *st, bool first, char *buf, size_t cap) {
    if (st == 0 || buf == 0 || cap == 0) {
        return 0;
    }
    int n = snprintf(buf, cap, "%s[", first ? "" : ",");
    if (n < 0) {
        return n;
    }
    int nuse = st->fx3_on ? st->fx3_n : 0;
    if (nuse > TR_FX3_WIRE) {
        nuse = TR_FX3_WIRE;
    }
    for (int i = 0; i < nuse; i++) {
        int m;
        if ((size_t)n < cap) {
            m = snprintf(buf + n, cap - (size_t)n, "%s[%u,%.10g,%u,%u]",
                         i > 0 ? "," : "", (unsigned)st->fx3_id[i], st->fx3_v[i],
                         st->fx3_rgb[i], (unsigned)st->fx3_w[i]);
        } else {
            m = snprintf(0, 0, "%s[%u,%.10g,%u,%u]",
                         i > 0 ? "," : "", (unsigned)st->fx3_id[i], st->fx3_v[i],
                         st->fx3_rgb[i], (unsigned)st->fx3_w[i]);
        }
        if (m < 0) {
            return m;
        }
        n += m;
    }
    if ((size_t)n < cap) {
        int m = snprintf(buf + n, cap - (size_t)n, "]");
        if (m < 0) {
            return m;
        }
        n += m;
    } else {
        n += 1;
    }
    return n;
}

/* 국내선물 Data2 점수를 기존 pgap/rgap/mgap/ymae/sniper 슬롯에 담는다.
 * 차트는 이 배열로 삼선·회귀·마켓·이탈을 그린다. */
static void ks_fill_scope(const tr_ksscore_out_t *o, tr_fxpgap_out_t pg[2], tr_fxpgap_out_t *rg,
                          tr_fxpgap_out_t *mg, tr_fxymae_out_t *ym, tr_fxsniper_out_t *sn) {
    memset(pg, 0, sizeof(tr_fxpgap_out_t) * 2);
    memset(rg, 0, sizeof(*rg));
    memset(mg, 0, sizeof(*mg));
    memset(ym, 0, sizeof(*ym));
    memset(sn, 0, sizeof(*sn));
    pg[0].ready = o->three_ready;
    pg[0].gap = o->three_gap;
    pg[0].peak = o->three_peak;
    pg[0].prev_peak = o->three_prev_peak;
    pg[0].ratio = o->three_ratio;
    pg[0].cnt = o->three_cnt;
    pg[0].rgb4 = o->three_rgb;
    pg[0].width4 = o->three_width;
    pg[1].ready = o->five_ready;
    pg[1].gap = o->five_gap;
    pg[1].peak = o->five_peak;
    pg[1].prev_peak = o->five_prev_peak;
    pg[1].ratio = o->five_ratio;
    pg[1].cnt = o->five_cnt;
    pg[1].rgb4 = o->five_rgb;
    rg->ready = o->reg_ready;
    rg->gap = o->reg_gap;
    rg->peak = o->reg_peak;
    rg->prev_peak = o->reg_prev_peak;
    rg->ratio = o->reg_ratio;
    rg->cnt = o->reg_cnt;
    mg->ready = o->mkt_ready;
    mg->gap = o->mkt_gap;
    mg->peak = o->mkt_peak;
    mg->prev_peak = o->mkt_prev_peak;
    mg->ratio = o->mkt_ratio;
    mg->cnt = o->mkt_cnt;
    ym->pos = o->pos;
    ym->prev_valid = o->prev_valid;
    ym->prev_hi = o->prev_hi;
    ym->prev_lo = o->prev_lo;
    ym->two_hi = o->two_hi;
    ym->two_lo = o->two_lo;
    ym->show_first = o->first_break;
    sn->calc_ready = o->calc_ready;
    sn->score = o->score;
    sn->score_ex = o->score_ex;
    sn->ratio_score = o->ratio_score;
    sn->compound = o->compound_show;
    sn->target_ratio = o->three_ratio;
    sn->price_ratio = o->price_ratio;
    sn->rgb = o->score_rgb;
    sn->px_exit = o->px_cond;
    sn->below = o->three_below;
    sn->above = o->three_above;
    sn->session_reset = o->session_reset;
}

/* 상태 링에서 이 봉의 슬롯을 찾는다. 최신→과거 순이고, 목표보다 과거로 내려가면 없다. */
static bool find_bar_status(const tr_pipeline_t *p, tr_time_us_t open, tr_bar_status_t *out) {
    if (p == 0 || out == 0 || !p->status_ring_on) {
        return false;
    }
    size_t nslot = tr_ring_count(&p->status_ring);
    for (size_t i = 0; i < nslot; i++) {
        tr_bar_status_t old;
        if (!tr_ring_at(&p->status_ring, i, &old) || old.open_time_us < open) {
            break;
        }
        if (old.open_time_us == open) {
            *out = old;
            return true;
        }
    }
    return false;
}

static void engine_on_bar(void *ctx, const tr_event_envelope_t *env, const tr_candle_t *bar) {
    tr_pipeline_t *p = (tr_pipeline_t *)ctx;
    tr_engine_t *e = p->engine;
    if (bar->timeframe_sec != e->cfg.timeframe_sec) {
        return;
    }
    if (e->candle_fn != 0 && bar->state == TR_CANDLE_CLOSED &&
        (bar->quality & TR_QUALITY_FILLED_EMPTY) == 0 &&
        (env->kind == TR_EVENT_CANDLE_CLOSED || env->kind == TR_EVENT_CANDLE_UPDATE)) {
        e->candle_fn(e->candle_fn_ctx, bar);
    }

    /* 지표용 내장 컨텍스트 계산: trading day / 당일 첫 봉 / 새 봉 여부
     * (종목별 세션 기준 — 파이프라인마다 다를 수 있다) */
    int64_t day = -1;
    tr_session_trading_day(&p->session, bar->open_time_us, &day);
    bool session_first = !p->has_prev_day || day != p->prev_trading_day;
    bool is_new_bar = !p->has_prev_bar || bar->open_time_us != p->prev_bar_open;
    /* 과거 봉 정정(늦은 틱): open_time이 스트림 위치보다 과거인 이벤트. 정정은 누적
     * 상태(회귀·이평·점수·마켓·기억·지속·⑤ 체인·위치 마커)를 전진시키지 않는다.
     * 그 봉에 이미 적어 둔 지표는 슬롯에 그대로 둔다. */
    bool is_correction = p->has_prev_bar && bar->open_time_us < p->prev_bar_open;
    bool closed = env->kind == TR_EVENT_CANDLE_CLOSED;
    tr_bar_status_t frozen;
    memset(&frozen, 0, sizeof(frozen));
    bool have_frozen = is_correction && find_bar_status(p, bar->open_time_us, &frozen);

    tr_ind_eval_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.bar = bar;
    ev.event_time_us = env->event_time_us;
    ev.trading_day = day;
    ev.is_new_bar = is_new_bar;
    ev.is_session_first = session_first;
    ev.compress = TR_COMPRESS_MIN;
    ev.bar_interval = e->cfg.timeframe_sec / 60;

    if (!is_correction) {
        tr_lr3_eval(&p->lr3, &ev);
        if (closed) {
            /* 확정 봉의 H/L/C를 V4의 ATR에 반영 (증분 계약) */
            tr_lp4_on_bar_closed(&p->lr3.v4, (double)bar->high, (double)bar->low, (double)bar->close);
        }
        tr_htf_curve_eval(&p->htf, (double)bar->high, (double)bar->low);
    }

    /* 이평선 5/20/60: 진행 봉 재호출은 현재 슬롯 덮어쓰기 (tr_sma_on_bar 계약).
     * 과거 봉 정정 이벤트는 창에서 제외한다 — 정정 봉은 is_new_bar=true로 보여
     * 게이트 없이 갱신하면 과거 종가가 새 슬롯으로 push되어 창이 오염된다 */
    if (!is_correction) {
        tr_sma_on_bar(&p->sma5, (double)bar->close, is_new_bar);
        tr_sma_on_bar(&p->sma20, (double)bar->close, is_new_bar);
        tr_sma_on_bar(&p->sma60, (double)bar->close, is_new_bar);
    }

    tr_score1m_input_t sin;
    memset(&sin, 0, sizeof(sin));
    sin.high = (double)bar->high;
    sin.low = (double)bar->low;
    sin.close = (double)bar->close;
    sin.htf_direction = p->htf.direction;
    sin.trading_day = day;
    sin.is_min_1 = e->cfg.timeframe_sec == 60;
    sin.reg_valid = p->lr3.reg_valid;
    sin.reg_r2 = p->lr3.r2;
    sin.reg_flat_line = p->lr3.line;
    /* 국내: 호가 유효할 때만. 해외선물 1분봉: 호가 자리에 OSF 결합 점수.
     * 정정 봉은 흐름을 밀지 않고, 그 봉 슬롯에 적어 둔 점수를 다시 쓴다. */
    int bar_ob_valid = 0;
    double bar_ob_score = 0.0;
    if (p->is_ovs && e->cfg.timeframe_sec == 60 && !is_correction) {
        tr_osf_flow_input_t oin = {
            .high = (double)bar->high,
            .low = (double)bar->low,
            .close = (double)bar->close,
            .volume = (double)bar->volume,
            .bar_open = bar->open_time_us,
        };
        tr_osf_combo_eval(&p->osf, &oin);
        bar_ob_valid = p->osf.valid ? 1 : 0;
        bar_ob_score = bar_ob_valid ? p->osf.score : 0.0;
    } else if (p->is_ovs && e->cfg.timeframe_sec == 60 && is_correction && p->status_ring_on) {
        size_t nslot = tr_ring_count(&p->status_ring);
        for (size_t i = 0; i < nslot; i++) {
            tr_bar_status_t old;
            if (!tr_ring_at(&p->status_ring, i, &old) || old.open_time_us < bar->open_time_us) {
                break;
            }
            if (old.open_time_us == bar->open_time_us) {
                bar_ob_valid = old.ob_valid ? 1 : 0;
                bar_ob_score = bar_ob_valid ? old.ob_score : 0.0;
                break;
            }
        }
    } else if (!p->is_ovs && p->obd2.validity == TR_VALIDITY_VALID) {
        bar_ob_valid = 1;
        bar_ob_score = p->obd2.score;
    }
    sin.ob_score = bar_ob_score;
    /* 정정은 점수 창에 과거 고저를 넣지 않는다. 그 봉의 점수는 슬롯 값을 유지한다. */
    if (!is_correction) {
        tr_score1m_on_bar(&p->score, &sin);
    } else if (have_frozen) {
        bar_ob_valid = frozen.ob_valid ? 1 : 0;
        bar_ob_score = frozen.ob_valid ? frozen.ob_score : 0.0;
    }

    /* ⑤ 매매 상태 체인 (원본: DataCompress==2 && BarInterval==1 게이트).
     * 봉당 1회 상태 진행: bar_index는 새 봉에서만 증가하고, 모듈은 세션 경계를
     * bar_index 게이트로 1회만 저장한다 (진행 봉 재호출은 현재 봉 H/L/C 집계만 갱신).
     * 과거 봉 정정(늦은 틱) 이벤트는 체인에서 제외한다 — 마지막 확정 final_* 값이 유지된다. */
    if (e->cfg.timeframe_sec == 60 && !is_correction) {
        if (is_new_bar) {
            p->bar_index++;
        }
        uint32_t bar_min = 0;
        tr_local_day_and_min(bar->open_time_us, p->session.utc_offset_min, 0, &bar_min);
        tr_dtl1_on_bar(&p->dtl1, (double)bar->high, (double)bar->low, (double)bar->close,
                       session_first, (int64_t)p->bar_index, true);
        tr_gap1_on_bar(&p->gap1, (double)bar->open, (double)bar->high, (double)bar->low,
                       (double)bar->close, (int32_t)bar_min, session_first,
                       (int64_t)p->bar_index, true);
        if (p->gap1.valid) {
            /* 원본 게이트: 운영갭유효==1 일 때만 일봉 정렬 평가 */
            tr_dalign2_input_t din;
            memset(&din, 0, sizeof(din));
            din.pred_dir[0] = p->lr3.v4.pred_dir[0];
            din.pred_dir[1] = p->lr3.v4.pred_dir[1];
            din.pred_dir[2] = p->lr3.v4.pred_dir[2];
            din.reg_valid = p->lr3.reg_valid;
            din.r2 = p->lr3.r2;
            din.trend_dir = p->dtl1.trend.dir;
            din.trend_state = p->dtl1.trend.state;
            din.trend_strength = p->dtl1.trend.strength;
            din.trend_valid = p->dtl1.trend.valid;
            din.gap_grade = p->gap1.gap_grade;
            din.daily_weight_in = p->gap1.daily_weight;
            din.elapsed_min = (double)p->gap1.elapsed_min;
            din.big_gap_reeval_min = (double)e->cfg.big_gap_reeval_min;
            din.min_r2 = (double)e->cfg.min_r2;
            din.min_final_strength = (double)e->cfg.min_final_strength;
            tr_dalign2_eval(&din, &p->dalign);
        } else {
            memset(&p->dalign, 0, sizeof(p->dalign)); /* 갭 무효 → 운영최종*=0 유지 */
        }
    }

    /* MTF상단/하단1~3 (원본 메인의 오차 띠 공식, 부채꼴·기억선 공용):
     * 기준오차 = max(회귀잔차, 예측변동성×0.25), 범위 = 기준오차 × √예측봉수 */
    double band_base = p->lr3.residual > p->lr3.v4.volatility * 0.25
                           ? p->lr3.residual
                           : p->lr3.v4.volatility * 0.25;
    double upper[3], lower[3];
    for (int i = 0; i < 3; i++) {
        double span = (double)e->cfg.predict_bars[i];
        double band = band_base * sqrt(span > 1.0 ? span : 1.0);
        upper[i] = p->lr3.v4.pred_price[i] + band;
        lower[i] = p->lr3.v4.pred_price[i] - band;
    }

    /* 곡선회귀선_평탄: 원본 틱 양자화 (tick_scale 헬퍼 참조) */
    double ps = tick_scale(p);
    double line_flat = floor(p->lr3.line / ps + 0.5) * ps;

    /* ⑧ 마켓 밴드. 정정 봉은 창을 밀지 않는다. */
    if (!is_correction && p->mkt_on) {
        tr_market_on_bar(&p->mkt, bar, session_first, line_flat);
    }

    /* 최근 5봉 H/L ([0]=현재) — 기억선 이탈 검사용 */
    double h5[5], l5[5];
    size_t hl_count = 0;
    {
        size_t nb = tr_ring_count(&p->bb.bars);
        if (nb > 5) {
            nb = 5;
        }
        for (size_t i = 0; i < nb; i++) {
            tr_candle_t c;
            if (tr_ring_at(&p->bb.bars, i, &c)) {
                h5[i] = (double)c.high;
                l5[i] = (double)c.low;
                hl_count++;
            }
        }
    }

    /* ⑥ 방향 기억 (운영최종방향 = ⑤ 일봉 체인 결과, 무효 시 회귀선_구분 부호 폴백).
     * 정정은 기억 세션을 밀지 않는다. */
    if (!is_correction) {
        tr_regmem_input_t rin;
        memset(&rin, 0, sizeof(rin));
        rin.reg_valid = p->lr3.reg_valid;
        rin.line_flat = line_flat;
        rin.line_sign = p->lr3.line_sign;
        rin.slope = p->lr3.slope;
        rin.final_dir = p->dalign.final_dir;
        rin.final_dir_valid = p->dalign.final_valid;
        rin.pred_price[0] = p->lr3.v4.pred_price[0];
        rin.pred_price[1] = p->lr3.v4.pred_price[1];
        rin.pred_price[2] = p->lr3.v4.pred_price[2];
        memcpy(rin.upper, upper, sizeof(upper));
        memcpy(rin.lower, lower, sizeof(lower));
        rin.session_no = (uint64_t)(day > 0 ? day : 0);
        rin.compress_min = true;
        rin.compress_min_le30 = e->cfg.timeframe_sec <= 1800;
        rin.ob_applicable = p->obd2.validity == TR_VALIDITY_VALID;
        rin.ob_state = p->score.ob_dir;
        rin.h5 = h5;
        rin.l5 = l5;
        rin.hl_count = hl_count;
        tr_regmem_on_bar(&p->regmem, &rin);
    }

    /* ⑦ 지속 사진. 정정은 유지 봉수를 세지 않는다. */
    if (!is_correction) {
        tr_persist_input_t pin;
        memset(&pin, 0, sizeof(pin));
        pin.reg_valid = p->lr3.reg_valid;
        pin.r2 = p->lr3.r2;
        pin.pred_dir2 = p->lr3.v4.pred_dir[1];
        pin.pred_price[0] = p->lr3.v4.pred_price[0];
        pin.pred_price[1] = p->lr3.v4.pred_price[1];
        pin.pred_price[2] = p->lr3.v4.pred_price[2];
        memcpy(pin.upper, upper, sizeof(upper));
        memcpy(pin.lower, lower, sizeof(lower));
        pin.h5 = h5;
        pin.l5 = l5;
        pin.hl_count = hl_count;
        tr_persist_on_bar(&p->persist, &pin);
    }

    /* 해외선물 미래곡선 V1. 1분봉만, 과거 봉 정정은 세션 상태를 밀지 않는다.
     * 직전 완료 봉은 링 [1] (콜백 시점에 현재 봉이 [0]이다). */
    int fx_on = 0;
    uint32_t fx_mask = 0;
    double fx_plot[TR_FXMIRAE_PLOTS];
    memset(fx_plot, 0, sizeof(fx_plot));
    int fx3_on = 0;
    int fx3_n = 0;
    uint16_t fx3_id[TR_FX3_WIRE];
    double fx3_v[TR_FX3_WIRE];
    uint32_t fx3_rgb[TR_FX3_WIRE];
    uint8_t fx3_w[TR_FX3_WIRE];
    memset(fx3_id, 0, sizeof(fx3_id));
    memset(fx3_v, 0, sizeof(fx3_v));
    memset(fx3_rgb, 0, sizeof(fx3_rgb));
    memset(fx3_w, 0, sizeof(fx3_w));
    tr_fxpgap_out_t pgout[2];
    tr_fxpgap_out_t rgout;
    tr_fxpgap_out_t mgout;
    tr_fxymae_out_t ymout;
    tr_fxsniper_out_t snout;
    int ray_on = 0;
    int ray_sign = 0;
    double ray_px[5], ray_up[5], ray_dn[5];
    int8_t ray_dir[5];
    memset(ray_px, 0, sizeof(ray_px));
    memset(ray_up, 0, sizeof(ray_up));
    memset(ray_dn, 0, sizeof(ray_dn));
    memset(ray_dir, 0, sizeof(ray_dir));
    memset(pgout, 0, sizeof(pgout));
    memset(&rgout, 0, sizeof(rgout));
    memset(&mgout, 0, sizeof(mgout));
    memset(&ymout, 0, sizeof(ymout));
    memset(&snout, 0, sizeof(snout));
    if (p->is_ovs && e->cfg.timeframe_sec == 60 && !is_correction) {
        tr_fxmirae_input_t fin;
        memset(&fin, 0, sizeof(fin));
        civil_stamp(bar->open_time_us, p->session.utc_offset_min, &fin.bar.date, &fin.bar.time);
        fin.bar.bar_open = bar->open_time_us;
        fin.bar.high = (double)bar->high;
        fin.bar.low = (double)bar->low;
        fin.bar.close = (double)bar->close;
        fin.bar.volume = (double)bar->volume;
        tr_candle_t prev;
        if (tr_ring_count(&p->bb.bars) >= 2 && tr_ring_at(&p->bb.bars, 1, &prev)) {
            fin.has_prev = true;
            civil_stamp(prev.open_time_us, p->session.utc_offset_min, &fin.prev_date, &fin.prev_time);
            fin.prev_h = (double)prev.high;
            fin.prev_l = (double)prev.low;
            fin.prev_c = (double)prev.close;
            fin.prev_v = (double)prev.volume;
        }
        tr_fxmirae_eval(&p->fx, &fin);
        fx_on = 1;
        for (int k = 0; k < TR_FXMIRAE_PLOTS; k++) {
            if (p->fx.plots[k].on) {
                fx_mask |= 1u << k;
            }
            fx_plot[k] = p->fx.plots[k].value;
        }
        int64_t ymd = 0, tod = 0;
        civil_stamp(bar->open_time_us, p->session.utc_offset_min, &ymd, &tod);
        tr_fxv3_run_eval(&p->fx3, ymd, tod, (double)bar->open, (double)bar->high,
                         (double)bar->low, (double)bar->close, (double)bar->volume,
                         bar->open_time_us);
        const tr_fxv3_t *v3 = tr_fxv3_run_view(&p->fx3);
        fx3_on = 1;
        if (v3 != 0) {
            for (int k = 1; k < TR_FXV3_PLOT_N && fx3_n < TR_FX3_WIRE; k++) {
                if (!v3->plots[k].on) {
                    continue;
                }
                fx3_id[fx3_n] = (uint16_t)k;
                fx3_v[fx3_n] = v3->plots[k].value;
                fx3_rgb[fx3_n] = v3->plots[k].rgb;
                fx3_w[fx3_n] = (uint8_t)v3->plots[k].width;
                fx3_n++;
            }
        }
        int64_t key = tr_fx_session_key_v1(ymd, tod);
        bool reset = !p->pgap_has_key || key != p->pgap_key;
        tr_fxpgap_input_t gin;
        memset(&gin, 0, sizeof(gin));
        gin.session_reset = reset;
        gin.bar_open = bar->open_time_us;
        gin.high = (double)bar->high;
        gin.low = (double)bar->low;
        gin.close = (double)bar->close;
        for (int k = 0; k < 5; k++) {
            gin.target[k] = p->fx.fv.out.persist_target[k];
        }
        tr_fxpgap_eval(&p->pgap3, &gin);
        tr_fxpgap_eval(&p->pgap5, &gin);
        p->pgap_key = key;
        p->pgap_has_key = true;
        pgout[0] = p->pgap3.out;
        pgout[1] = p->pgap5.out;
        tr_fxpgap_input_t rin;
        memset(&rin, 0, sizeof(rin));
        rin.session_reset = reset;
        rin.bar_open = bar->open_time_us;
        rin.high = (double)bar->high;
        rin.low = (double)bar->low;
        rin.target[0] = p->fx.fv.out.reg_flat;
        rin.target[1] = p->fx.syn5.out_reg_valid ? p->fx.syn5.out_reg : 0.0;
        rin.target[2] = p->fx.syn15.out_reg_valid ? p->fx.syn15.out_reg : 0.0;
        rin.target[3] = p->fx.syn30.out_reg_valid ? p->fx.syn30.out_reg : 0.0;
        tr_fxpgap_eval(&p->rgap, &rin);
        rgout = p->rgap.out;
        tr_fxpgap_input_t min;
        memset(&min, 0, sizeof(min));
        min.session_reset = reset;
        min.bar_open = bar->open_time_us;
        min.high = (double)bar->high;
        min.low = (double)bar->low;
        min.close = (double)bar->close;
        min.target[0] = p->fx.fv.out.market_center;
        min.target[1] = p->fx.syn5.out_mkt_valid ? p->fx.syn5.out_mkt : 0.0;
        min.target[2] = p->fx.syn15.out_mkt_valid ? p->fx.syn15.out_mkt : 0.0;
        min.target[3] = p->fx.syn30.out_mkt_valid ? p->fx.syn30.out_mkt : 0.0;
        tr_fxpgap_eval(&p->mgap, &min);
        tr_fxunion_paint(&p->pgap3, 1, p->rgap.out.ready, p->rgap.out.ratio, p->mgap.out.ready,
                         p->mgap.out.ratio, (double)bar->high, (double)bar->low);
        tr_fxunion_paint(&p->pgap5, 0, 0, 0, 0, 0, (double)bar->high, (double)bar->low);
        pgout[0] = p->pgap3.out;
        pgout[1] = p->pgap5.out;
        mgout = p->mgap.out;
        tr_fxymae_input_t yin;
        memset(&yin, 0, sizeof(yin));
        yin.session_reset = reset;
        yin.targets_ready = gin.target[0] != 0.0 && gin.target[1] != 0.0 && gin.target[2] != 0.0;
        yin.bar_open = bar->open_time_us;
        yin.high = (double)bar->high;
        yin.low = (double)bar->low;
        yin.close = (double)bar->close;
        yin.volume = (double)bar->volume;
        yin.t1 = gin.target[0];
        yin.t2 = gin.target[1];
        yin.t3 = gin.target[2];
        yin.union_w = p->pgap3.out.union_w;
        yin.reg_ready = p->rgap.out.ready;
        yin.mkt_ready = p->mgap.out.ready;
        yin.reg_ratio = p->rgap.out.ratio;
        yin.mkt_ratio = p->mgap.out.ratio;
        tr_fxymae_eval(&p->ymae, &yin);
        ymout = p->ymae.out;
        tr_fxsniper_input_t snin;
        memset(&snin, 0, sizeof(snin));
        snin.session_reset = reset;
        snin.bar_open = bar->open_time_us;
        snin.high = (double)bar->high;
        snin.low = (double)bar->low;
        snin.close = (double)bar->close;
        for (int k = 0; k < 5; k++) {
            snin.target[k] = gin.target[k];
        }
        snin.break_ready = ymout.ready && p->ymae.two_max > 0.0;
        snin.prev_range_valid = ymout.prev_valid;
        snin.pos = ymout.pos;
        snin.first_break = ymout.first_break;
        snin.price_ratio = 100.0;
        if (p->ymae.two_max > 0.0) {
            double span = ymout.two_hi - ymout.two_lo;
            snin.price_ratio = span / p->ymae.two_max * 100.0;
        }
        snin.three_ready = pgout[0].ready;
        snin.reg_ready = rgout.ready;
        snin.mkt_ready = mgout.ready;
        snin.three_ratio = pgout[0].ratio;
        snin.reg_ratio = rgout.ratio;
        snin.mkt_ratio = mgout.ratio;
        snin.below = p->pgap3.below;
        snin.above = p->pgap3.above;
        tr_fxsniper_eval(&p->sniper, &snin);
        snout = p->sniper.out;
        if (!reset && p->fx3.reg.reg_valid) {
            double base = p->fx3.reg.residual;
            if (p->fx3.pred.volatility * 0.25 > base) {
                base = p->fx3.pred.volatility * 0.25;
            }
            ray_on = 1;
            ray_sign = p->fx3.reg.line_sign;
            for (int k = 0; k < 5; k++) {
                double horizon = (double)p->fx3.view.cfg.predict_bars[k];
                if (horizon < 1.0) {
                    horizon = 1.0;
                }
                double span = base * sqrt(horizon);
                ray_px[k] = p->fx3.pred.pred_price[k];
                ray_up[k] = ray_px[k] + span;
                ray_dn[k] = ray_px[k] - span;
                ray_dir[k] = (int8_t)p->fx3.pred.pred_dir[k];
            }
        }
    } else if (p->is_futures && !p->is_ovs && e->cfg.timeframe_sec == 60 && !is_correction) {
        /* 국내선물만. 날짜가 바뀌면 당일 봉 순번을 0으로 되돌린다. */
        if (is_new_bar) {
            int64_t ymd = 0;
            int64_t tod = 0;
            civil_stamp(bar->open_time_us, p->session.utc_offset_min, &ymd, &tod);
            if (!p->ks_has_date || ymd != p->ks_bdate) {
                p->ks_day_index = 0;
                p->ks_bdate = ymd;
                p->ks_has_date = true;
            } else {
                p->ks_day_index++;
            }
            p->ks_current++;
        }
        int64_t ymd = 0;
        int64_t tod = 0;
        civil_stamp(bar->open_time_us, p->session.utc_offset_min, &ymd, &tod);
        tr_ksscore_input_t kin;
        memset(&kin, 0, sizeof(kin));
        kin.bdate = ymd;
        kin.day_index = p->ks_day_index;
        kin.current_bar = p->ks_current > 0 ? p->ks_current : 1;
        kin.bar_open = bar->open_time_us;
        kin.cur_time = tod;
        kin.high = (double)bar->high;
        kin.low = (double)bar->low;
        kin.close = (double)bar->close;
        kin.volume = (double)bar->volume;
        tr_ksscore_eval(&p->ks, &kin);
        ks_fill_scope(&p->ks.out, pgout, &rgout, &mgout, &ymout, &snout);
    } else if (is_correction && p->status_ring_on) {
        /* 정정 봉은 모듈을 다시 돌리지 않는다. 그 슬롯에 이미 있던 fx를 유지한다 */
        size_t nslot = tr_ring_count(&p->status_ring);
        for (size_t i = 0; i < nslot; i++) {
            tr_bar_status_t old;
            if (!tr_ring_at(&p->status_ring, i, &old) || old.open_time_us < bar->open_time_us) {
                break;
            }
            if (old.open_time_us == bar->open_time_us) {
                fx_on = old.fx_on;
                fx_mask = old.fx_mask;
                memcpy(fx_plot, old.fx_plot, sizeof(fx_plot));
                fx3_on = old.fx3_on;
                fx3_n = old.fx3_n;
                memcpy(fx3_id, old.fx3_id, sizeof(fx3_id));
                memcpy(fx3_v, old.fx3_v, sizeof(fx3_v));
                memcpy(fx3_rgb, old.fx3_rgb, sizeof(fx3_rgb));
                memcpy(fx3_w, old.fx3_w, sizeof(fx3_w));
                for (int pi = 0; pi < 2; pi++) {
                    pgout[pi].ready = old.pg_ready[pi];
                    pgout[pi].gap = old.pg_v[pi][0];
                    pgout[pi].peak = old.pg_v[pi][1];
                    pgout[pi].prev_peak = old.pg_v[pi][2];
                    pgout[pi].ratio = old.pg_v[pi][3];
                    pgout[pi].prev_ratio = old.pg_v[pi][4];
                    pgout[pi].cnt = old.pg_v[pi][5];
                    pgout[pi].rgb4 = old.pg_c4[pi];
                    pgout[pi].rgb5 = old.pg_c5[pi];
                    pgout[pi].width4 = old.pg_w4[pi];
                    pgout[pi].plot3 = old.pg_plot3[pi];
                    pgout[pi].plot5 = old.pg_plot5[pi];
                }
                rgout.ready = old.rg_ready;
                rgout.gap = old.rg_v[0];
                rgout.peak = old.rg_v[1];
                rgout.prev_peak = old.rg_v[2];
                rgout.ratio = old.rg_v[3];
                rgout.prev_ratio = old.rg_v[4];
                rgout.cnt = old.rg_v[5];
                rgout.rgb4 = old.rg_c4;
                rgout.rgb5 = old.rg_c5;
                rgout.width4 = old.rg_w4;
                rgout.plot3 = old.rg_plot3;
                rgout.plot5 = old.rg_plot5;
                mgout.ready = old.mg_ready;
                mgout.gap = old.mg_v[0];
                mgout.peak = old.mg_v[1];
                mgout.prev_peak = old.mg_v[2];
                mgout.ratio = old.mg_v[3];
                mgout.prev_ratio = old.mg_v[4];
                mgout.cnt = old.mg_v[5];
                mgout.rgb4 = old.mg_c4;
                mgout.rgb5 = old.mg_c5;
                mgout.width4 = old.mg_w4;
                mgout.plot3 = old.mg_plot3;
                mgout.plot5 = old.mg_plot5;
                pgout[0].union_rgb = old.pg_union_rgb[0];
                pgout[1].union_rgb = old.pg_union_rgb[1];
                pgout[0].union_w = old.pg_union_w[0];
                pgout[1].union_w = old.pg_union_w[1];
                ymout.pos = old.ym_pos;
                ymout.prev_valid = old.ym_prev_valid;
                ymout.prev_hi = old.ym_prev_hi;
                ymout.prev_lo = old.ym_prev_lo;
                ymout.two_hi = old.ym_two_hi;
                ymout.two_lo = old.ym_two_lo;
                ymout.show_h1 = old.ym_show_h1;
                ymout.show_h2 = old.ym_show_h2;
                ymout.show_h3 = old.ym_show_h3;
                ymout.show_h4 = old.ym_show_h4;
                ymout.show_first = old.ym_show_first;
                ymout.mark_h1 = old.ym_mark_h1;
                ymout.mark_h2 = old.ym_mark_h2;
                ymout.mark_h3 = old.ym_mark_h3;
                ymout.mark_h4 = old.ym_mark_h4;
                snout.score = old.sn_score;
                snout.score_ex = old.sn_ex;
                snout.ratio_score = old.sn_ratio;
                snout.stage = old.sn_stage;
                snout.compound = old.sn_compound;
                snout.target_ratio = old.sn_tgt;
                snout.price_ratio = old.sn_px;
                snout.rgb = old.sn_rgb;
                snout.px_exit = old.sn_px_exit;
                snout.below = old.sn_below;
                snout.above = old.sn_above;
                snout.session_reset = old.sn_reset;
                ymout.show_price = old.pvc_price_on;
                ymout.show_price_ratio = old.pvc_price;
                ymout.show_vol = old.pvc_vol_on;
                ymout.show_vol_ratio = old.pvc_vol;
                ymout.show_both = old.pvc_both;
                ray_on = old.ray_on;
                ray_sign = old.ray_sign;
                memcpy(ray_px, old.ray_px, sizeof(ray_px));
                memcpy(ray_up, old.ray_up, sizeof(ray_up));
                memcpy(ray_dn, old.ray_dn, sizeof(ray_dn));
                memcpy(ray_dir, old.ray_dir, sizeof(ray_dir));
                break;
            }
        }
    }

    /* 봉 링과 open_time 기준으로 정합을 맞춰 봉별 지표를 기록한다 (스냅샷 복원용).
     * 새 봉이면 push, 현재 봉 갱신이면 최신 교체, 늦은 정정은 해당 슬롯만 고친다. */
    if (p->status_ring_on) {
        tr_bar_status_t st;
        memset(&st, 0, sizeof(st));
        st.open_time_us = bar->open_time_us;
        st.closed = closed;
        st.reg_valid = p->lr3.reg_valid;
        st.reg_line = p->lr3.line;
        st.reg_slope = p->lr3.slope;
        st.reg_r2 = p->lr3.r2;
        st.pred[0] = p->lr3.v4.pred_price[0];
        st.pred[1] = p->lr3.v4.pred_price[1];
        st.pred[2] = p->lr3.v4.pred_price[2];
        st.pred_dir[0] = p->lr3.v4.pred_dir[0];
        st.pred_dir[1] = p->lr3.v4.pred_dir[1];
        st.pred_dir[2] = p->lr3.v4.pred_dir[2];
        st.residual = p->lr3.residual;
        st.pvol = p->lr3.v4.volatility;
        memcpy(st.upper, upper, sizeof(upper));
        memcpy(st.lower, lower, sizeof(lower));
        st.score = p->score.score;
        st.ob_valid = bar_ob_valid;
        st.ob_score = bar_ob_score;
        st.mkt_valid = p->mkt_on && p->mkt.valid;
        st.mkt_center = p->mkt.center;
        st.mkt_u1 = p->mkt.upper1;
        st.mkt_l1 = p->mkt.lower1;
        st.mkt_u2 = p->mkt.upper2;
        st.mkt_l2 = p->mkt.lower2;
        st.mem_valid = p->regmem.mem_valid;
        st.mem_updated = p->regmem.updated;
        st.mem_reset = p->regmem.session_reset;
        st.mem_dir = p->regmem.mem_dir;
        st.mem_price = p->regmem.mem_price;
        memcpy(st.mem_target, p->regmem.mem_target, sizeof(st.mem_target));
        memcpy(st.mem_upper, p->regmem.mem_upper, sizeof(st.mem_upper));
        memcpy(st.mem_lower, p->regmem.mem_lower, sizeof(st.mem_lower));
        st.mem_show_targets = p->regmem.show_targets;
        st.mem_show_upper = p->regmem.show_upper;
        st.mem_show_lower = p->regmem.show_lower;
        st.pst_saved = p->persist.streak == p->persist.cfg.persist_bars &&
                       p->persist.saved_valid;
        st.pst_valid = p->persist.saved_valid;
        st.pst_dir = p->persist.saved_dir;
        memcpy(st.pst_target, p->persist.target, sizeof(st.pst_target));
        memcpy(st.pst_upper, p->persist.upper, sizeof(st.pst_upper));
        memcpy(st.pst_lower, p->persist.lower, sizeof(st.pst_lower));
        st.final_valid = p->dalign.final_valid ? 1 : 0;
        st.final_dir = p->dalign.final_dir;
        st.final_state = p->dalign.final_state;
        st.final_strength = (int)p->dalign.final_strength;
        st.trading_day = day;
        st.sma_valid = p->sma5.valid && p->sma20.valid && p->sma60.valid;
        st.sma[0] = p->sma5.value;
        st.sma[1] = p->sma20.value;
        st.sma[2] = p->sma60.value;
        st.fx_on = fx_on;
        st.fx_mask = fx_mask;
        memcpy(st.fx_plot, fx_plot, sizeof(st.fx_plot));
        st.fx3_on = fx3_on;
        st.fx3_n = fx3_n;
        memcpy(st.fx3_id, fx3_id, sizeof(st.fx3_id));
        memcpy(st.fx3_v, fx3_v, sizeof(st.fx3_v));
        memcpy(st.fx3_rgb, fx3_rgb, sizeof(st.fx3_rgb));
        memcpy(st.fx3_w, fx3_w, sizeof(st.fx3_w));
        for (int pi = 0; pi < 2; pi++) {
            st.pg_ready[pi] = pgout[pi].ready;
            st.pg_v[pi][0] = pgout[pi].gap;
            st.pg_v[pi][1] = pgout[pi].peak;
            st.pg_v[pi][2] = pgout[pi].prev_peak;
            st.pg_v[pi][3] = pgout[pi].ratio;
            st.pg_v[pi][4] = pgout[pi].prev_ratio;
            st.pg_v[pi][5] = pgout[pi].cnt;
            st.pg_c4[pi] = pgout[pi].rgb4;
            st.pg_c5[pi] = pgout[pi].rgb5;
            st.pg_w4[pi] = (uint8_t)pgout[pi].width4;
            st.pg_plot3[pi] = (uint8_t)pgout[pi].plot3;
            st.pg_plot5[pi] = (uint8_t)pgout[pi].plot5;
        }
        st.rg_ready = rgout.ready;
        st.rg_v[0] = rgout.gap;
        st.rg_v[1] = rgout.peak;
        st.rg_v[2] = rgout.prev_peak;
        st.rg_v[3] = rgout.ratio;
        st.rg_v[4] = rgout.prev_ratio;
        st.rg_v[5] = rgout.cnt;
        st.rg_c4 = rgout.rgb4;
        st.rg_c5 = rgout.rgb5;
        st.rg_w4 = (uint8_t)rgout.width4;
        st.rg_plot3 = (uint8_t)rgout.plot3;
        st.rg_plot5 = (uint8_t)rgout.plot5;
        st.mg_ready = mgout.ready;
        st.mg_v[0] = mgout.gap;
        st.mg_v[1] = mgout.peak;
        st.mg_v[2] = mgout.prev_peak;
        st.mg_v[3] = mgout.ratio;
        st.mg_v[4] = mgout.prev_ratio;
        st.mg_v[5] = mgout.cnt;
        st.mg_c4 = mgout.rgb4;
        st.mg_c5 = mgout.rgb5;
        st.mg_w4 = (uint8_t)mgout.width4;
        st.mg_plot3 = (uint8_t)mgout.plot3;
        st.mg_plot5 = (uint8_t)mgout.plot5;
        st.pg_union_rgb[0] = pgout[0].union_rgb;
        st.pg_union_rgb[1] = pgout[1].union_rgb;
        st.pg_union_w[0] = (uint8_t)pgout[0].union_w;
        st.pg_union_w[1] = (uint8_t)pgout[1].union_w;
        st.ym_pos = ymout.pos;
        st.ym_prev_valid = ymout.prev_valid;
        st.ym_show_h1 = ymout.show_h1;
        st.ym_show_h2 = ymout.show_h2;
        st.ym_show_h3 = ymout.show_h3;
        st.ym_show_h4 = ymout.show_h4;
        st.ym_show_first = ymout.show_first;
        st.ym_prev_hi = ymout.prev_hi;
        st.ym_prev_lo = ymout.prev_lo;
        st.ym_two_hi = ymout.two_hi;
        st.ym_two_lo = ymout.two_lo;
        st.ym_mark_h1 = ymout.mark_h1;
        st.ym_mark_h2 = ymout.mark_h2;
        st.ym_mark_h3 = ymout.mark_h3;
        st.ym_mark_h4 = ymout.mark_h4;
        st.sn_ready = snout.calc_ready;
        st.sn_score = snout.score;
        st.sn_ex = snout.score_ex;
        st.sn_ratio = snout.ratio_score;
        st.sn_stage = snout.stage;
        st.sn_compound = snout.compound;
        st.sn_tgt = snout.target_ratio;
        st.sn_px = snout.price_ratio;
        st.sn_rgb = snout.rgb;
        st.sn_px_exit = snout.px_exit;
        st.sn_below = snout.below;
        st.sn_above = snout.above;
        st.sn_reset = snout.session_reset;
        st.pvc_price_on = ymout.show_price;
        st.pvc_price = ymout.show_price_ratio;
        st.pvc_vol_on = ymout.show_vol;
        st.pvc_vol = ymout.show_vol_ratio;
        st.pvc_both = ymout.show_both;
        st.ray_on = ray_on;
        st.ray_sign = ray_sign;
        memcpy(st.ray_px, ray_px, sizeof(st.ray_px));
        memcpy(st.ray_up, ray_up, sizeof(st.ray_up));
        memcpy(st.ray_dn, ray_dn, sizeof(st.ray_dn));
        memcpy(st.ray_dir, ray_dir, sizeof(st.ray_dir));
        /* 정정은 지금 계산으로 과거 슬롯을 덮지 않는다. 적어 둔 지표를 그대로 둔다. */
        if (have_frozen) {
            st = frozen;
            st.open_time_us = bar->open_time_us;
        }
        tr_bar_status_t newest;
        if (tr_ring_count(&p->status_ring) == 0 ||
            (tr_ring_at(&p->status_ring, 0, &newest) && bar->open_time_us > newest.open_time_us)) {
            tr_ring_push(&p->status_ring, &st);
        } else if (bar->open_time_us == newest.open_time_us) {
            /* 같은 봉 재평가 덮어쓰기: 세션 리셋은 그 봉의 첫 평가에서만 서고 이후
             * 평가에서는 내려가므로(세션 번호가 이미 갱신됨), 봉 안에서 한 번 선
             * mem_reset은 그 봉의 슬롯이 다음 봉으로 넘어갈 때까지 유지한다.
             * 그래야 스냅샷 mem 이벤트가 세션 경계 봉을 놓치지 않는다 */
            st.mem_reset = st.mem_reset || newest.mem_reset;
            tr_ring_update_newest(&p->status_ring, &st);
        } else {
            for (size_t i = 1; i < tr_ring_count(&p->status_ring); i++) {
                tr_bar_status_t old;
                if (!tr_ring_at(&p->status_ring, i, &old) || old.open_time_us < bar->open_time_us) {
                    break;
                }
                if (old.open_time_us == bar->open_time_us) {
                    /* 해당 과거 슬롯만 정정한다 (구조는 바꾸지 않음). 덮어쓰기 전에
                     * mem_reset을 래치한다 — 최신 슬롯 덮어쓰기(위)와 같이, 세션 경계
                     * 봉에 한 번 선 리셋 표시가 늦은 틱 정정으로 지워지지 않게 한다.
                     * 그래야 이후 스냅샷의 mem 이벤트가 경계 봉을 놓치지 않는다 */
                    tr_bar_status_t *slot = (tr_bar_status_t *)tr_ring_get_mut(&p->status_ring, i);
                    if (slot != 0) {
                        st.mem_reset = st.mem_reset || slot->mem_reset;
                        *slot = st;
                    }
                    break;
                }
            }
        }
    }

    /* 비율 배열은 해외 1분과 국내선물 1분이 같이 쓴다. 미래 광선은 해외만. */
    int ovs_scope = p->is_ovs && e->cfg.timeframe_sec == 60;
    int ks_scope = p->is_futures && !p->is_ovs && e->cfg.timeframe_sec == 60;
    int scope_on = ovs_scope || ks_scope;
    publish_status(e, p, bar, closed, day, fx_on, fx_mask, fx_plot,
                   fx3_on, fx3_n, fx3_id, fx3_v, fx3_rgb, fx3_w,
                   scope_on ? &pgout[0] : 0,
                   scope_on ? &pgout[1] : 0,
                   scope_on ? &rgout : 0,
                   scope_on ? &mgout : 0,
                   scope_on ? &ymout : 0,
                   scope_on ? &snout : 0,
                   ovs_scope,
                   ovs_scope ? ray_on : 0, ray_sign,
                   ovs_scope ? ray_px : 0,
                   ovs_scope ? ray_up : 0,
                   ovs_scope ? ray_dn : 0,
                   ovs_scope ? ray_dir : 0,
                   bar_ob_valid, bar_ob_score, have_frozen ? &frozen : 0);

    /* 스트림 위치 마커는 현재 봉 이벤트에서만 전진한다. 과거 봉 정정(늦은 틱)이
     * trading day·봉 위치를 되돌리면 다음 현재 봉이 세션 첫 봉/새 봉으로 오인되어
     * ⑤ 일봉 체인이 중간 집계를 완성 일봉으로 오저장할 수 있다 */
    if (!is_correction) {
        p->prev_trading_day = day;
        p->has_prev_day = true;
        p->prev_bar_open = bar->open_time_us;
        p->has_prev_bar = true;
    }
}

tr_bb_status_t tr_engine_on_tick(tr_engine_t *e, const tr_event_envelope_t *env, const tr_tick_t *tick) {
    if (e == 0 || tick == 0) {
        return TR_BB_ERROR;
    }
    tr_pipeline_t *p = tr_engine_pipe_find(e, tick->instrument_id);
    if (p == 0) {
        return TR_BB_ERROR; /* 라우팅할 파이프라인이 없다 (구독 해지 채널의 지연 메시지 등) */
    }
    return tr_bar_builder_on_tick(&p->bb, env, tick);
}

void tr_engine_on_timer(tr_engine_t *e, tr_time_us_t now_us) {
    if (e == 0) {
        return;
    }
    for (int i = 0; i < e->pipe_count; i++) {
        tr_bar_builder_on_timer(&e->pipes[i]->bb, now_us);
    }
}

void tr_engine_set_candle_hook(tr_engine_t *e, tr_engine_candle_fn fn, void *ctx) {
    if (e == 0) {
        return;
    }
    e->candle_fn = fn;
    e->candle_fn_ctx = ctx;
}

bool tr_engine_inject_bar(tr_engine_t *e, const tr_candle_t *bar) {
    if (e == 0) {
        return false;
    }
    tr_pipeline_t *p = e->pipes[0];
    if (bar != 0) {
        tr_pipeline_t *found = tr_engine_pipe_find(e, bar->instrument_id);
        if (found != 0) {
            p = found;
        }
    }
    return tr_bar_builder_inject_bar(&p->bb, bar != 0 ? bar->close_time_us : 0, bar);
}

size_t tr_engine_pipe_merge_bars(tr_engine_t *e, uint64_t instrument_id,
                                 const tr_candle_t *bars, size_t n, tr_time_us_t now_us,
                                 tr_candle_t *bar_scratch, size_t bar_scratch_cap,
                                 tr_bar_status_t *st_scratch, size_t st_scratch_cap) {
    if (e == 0 || bars == 0 || n == 0) {
        return 0;
    }
    tr_pipeline_t *p = tr_engine_pipe_find(e, instrument_id);
    if (p == 0) {
        return 0;
    }
    tr_ring *bring = &p->bb.bars;
    if (bar_scratch == 0 || bar_scratch_cap < bring->capacity ||
        (void *)bar_scratch == bring->storage) {
        return 0;
    }
    tr_ring *sring = &p->status_ring;
    if (p->status_ring_on &&
        (st_scratch == 0 || st_scratch_cap < sring->capacity ||
         sring->capacity < bring->capacity)) {
        return 0; /* 상태 링 정합을 맞출 수 없으면 병합하지 않는다 */
    }

    /* 1) 기존 봉을 오름차순으로 비워낸다 (back_index count-1 = 가장 오래된 봉) */
    size_t m = tr_ring_count(bring);
    for (size_t k = 0; k < m; k++) {
        tr_ring_at(bring, m - 1 - k, &bar_scratch[k]);
    }

    /* 2) 정체된 OPEN 봉 처리: 조회 결과에 그보다 새로운 확정 봉이 있으면 서버 측에서
     * 그 봉의 창은 이미 닫혔다는 뜻이다. 제자리에서 닫아 두어야 병합 후에도 링 최신이
     * 확정 봉이 되고, 다음 라이브 틱이 새 봉을 정상으로 연다.
     * 수용 필터는 병합 루프와 같다 — 세션 밖·미확정 행에 OPEN 봉이 닫히면 안 된다 */
    bool force_closed = false;
    if (p->bb.has_open && m > 0) {
        tr_candle_t *open_bar = &bar_scratch[m - 1];
        for (size_t j = 0; j < n; j++) {
            const tr_candle_t *b = &bars[j];
            if (b->instrument_id == p->instrument_id &&
                b->timeframe_sec == p->bb.cfg.timeframe_sec &&
                b->close_time_us <= now_us &&
                b->open_time_us > open_bar->open_time_us &&
                tr_session_span(&p->session, b->open_time_us, 0, 0)) {
                open_bar->state = TR_CANDLE_CLOSED;
                p->bb.has_open = false;
                p->bb.last_close = open_bar->close;
                p->bb.has_last_close = true;
                force_closed = true;
                break;
            }
        }
    }

    /* 3) 뒤(최신)에서부터 병합해 저장소 끝에서 앞으로 채운다 — 용량 초과 시 가장
     * 오래된 봉이 밀리는 링 계약과 같다. floor는 조회 행의 오름차순·중복을 강제한다 */
    size_t cap = bring->capacity;
    size_t out = cap;
    size_t i = m, j = n;
    size_t inserted = 0;
    tr_time_us_t floor = INT64_MAX;
    while (out > 0 && (i > 0 || j > 0)) {
        /* 조회 후보 미리보기: 수용 불가 행(다른 종목·주기, 역순·중복, 미확정, 세션 밖)은
         * 여기서 소비하며 건너뛴다 */
        const tr_candle_t *f = 0;
        while (j > 0) {
            const tr_candle_t *cand = &bars[j - 1];
            if (cand->instrument_id == p->instrument_id &&
                cand->timeframe_sec == p->bb.cfg.timeframe_sec &&
                cand->open_time_us < floor &&
                cand->close_time_us <= now_us &&
                tr_session_span(&p->session, cand->open_time_us, 0, 0)) {
                f = cand;
                break;
            }
            j--;
        }
        if (i > 0 && (f == 0 || bar_scratch[i - 1].open_time_us >= f->open_time_us)) {
            const tr_candle_t *ex = &bar_scratch[--i];
            if (f != 0 && f->open_time_us == ex->open_time_us) {
                floor = f->open_time_us;
                j--; /* 중복 조회 봉 폐기 — 기존 봉 보존 */
            }
            out--;
            memcpy((char *)bring->storage + out * bring->elem_size, ex, bring->elem_size);
        } else if (f != 0) {
            floor = f->open_time_us;
            j--;
            out--;
            memcpy((char *)bring->storage + out * bring->elem_size, f, bring->elem_size);
            inserted++;
        } else {
            break;
        }
    }
    if (inserted == 0) {
        /* 삽입이 없으면 병합 쓰기는 기존 봉의 재배치였다 — 비워둔 사본으로 되돌려
         * 링을 원래 내용 그대로(head=0 정규화) 복원한다. 래핑된 링(head != 0)에서
         * 그대로 반환하면 저장소 상단에 쓴 사본과 메타가 어긋나 링이 깨진다 */
        memcpy(bring->storage, bar_scratch, m * bring->elem_size);
        bring->head = 0;
        bring->count = m;
        return 0; /* generation도 그대로 */
    }
    size_t new_count = cap - out;
    if (out > 0) {
        memmove(bring->storage, (const char *)bring->storage + out * bring->elem_size,
                new_count * bring->elem_size);
    }
    bring->head = 0;
    bring->count = new_count;

    /* 4) 상태 링 재구성: 병합된 봉 순서에 맞춰 기존 슬롯을 open_time으로 매칭하고,
     * 새로 들어온 봉 자리에는 지표 무효 슬롯을 둔다 (인덱스 정합 유지) */
    if (p->status_ring_on) {
        size_t sm = tr_ring_count(sring);
        for (size_t k = 0; k < sm; k++) {
            tr_ring_at(sring, sm - 1 - k, &st_scratch[k]);
        }
        if (force_closed && sm > 0 &&
            st_scratch[sm - 1].open_time_us == bar_scratch[m - 1].open_time_us) {
            st_scratch[sm - 1].closed = true; /* 강제 확정된 OPEN 봉의 슬롯 갱신 */
        }
        size_t si = 0;
        for (size_t k = 0; k < new_count; k++) {
            const tr_candle_t *b =
                (const tr_candle_t *)((const char *)bring->storage + k * bring->elem_size);
            tr_bar_status_t st;
            while (si < sm && st_scratch[si].open_time_us < b->open_time_us) {
                si++;
            }
            if (si < sm && st_scratch[si].open_time_us == b->open_time_us) {
                st = st_scratch[si++];
            } else {
                memset(&st, 0, sizeof(st));
                st.open_time_us = b->open_time_us;
                st.closed = true;
                tr_session_trading_day(&p->session, b->open_time_us, &st.trading_day);
            }
            memcpy((char *)sring->storage + k * sring->elem_size, &st, sring->elem_size);
        }
        sring->head = 0;
        sring->count = new_count;
    }

    p->generation++; /* 대시보드가 스냅샷을 다시 가져가도록 (feed.noteGeneration) */
    return inserted;
}

size_t tr_engine_pipe_find_gaps(const tr_engine_t *e, uint64_t instrument_id,
                                size_t from, size_t take, tr_time_us_t (*out)[2], size_t cap) {
    if (e == 0 || out == 0 || cap == 0) {
        return 0;
    }
    const tr_pipeline_t *p = 0;
    for (int i = 0; i < e->pipe_count; i++) {
        if (e->pipes[i]->instrument_id == instrument_id) {
            p = e->pipes[i];
            break;
        }
    }
    if (p == 0) {
        return 0;
    }
    size_t n = tr_ring_count(&p->bb.bars);
    if (from >= n) {
        return 0;
    }
    if (take > n - from) {
        take = n - from;
    }
    const tr_time_us_t tf_us = (tr_time_us_t)p->bb.cfg.timeframe_sec * TR_US_PER_SEC;
    /* 창의 각 봉 k(시간상 뒤)와 직전 봉 k+1(시간상 앞)의 쌍을 본다. k를 내림차순으로
     * 돌면 쌍은 시각 오름차순이 된다. 창의 가장 오래된 봉(k = from+take-1)의 쌍은
     * 직전 봉이 창 밖(다음 페이지)에 있어도 링에 남아 있으면 검사한다 — 페이지를
     * 이어 붙여 시딩하는 쪽에서 경계에 걸친 구멍이 빠지지 않게 하기 위해서다. */
    size_t m = 0;
    for (size_t k = from + take; k-- > from && m < cap;) {
        if (k + 1 >= n) {
            continue; /* 링에 더 오래된 봉이 없으면 쌍을 만들 수 없다 */
        }
        tr_candle_t newer, older;
        tr_ring_at(&p->bb.bars, k, &newer);
        tr_ring_at(&p->bb.bars, k + 1, &older);
        tr_time_us_t open_a, open_b;
        if (newer.open_time_us - older.open_time_us > tf_us &&
            tr_session_span(&p->session, older.open_time_us, &open_a, 0) &&
            tr_session_span(&p->session, newer.open_time_us, &open_b, 0) &&
            open_a == open_b) {
            out[m][0] = older.open_time_us + tf_us; /* 첫 빈 분 */
            out[m][1] = newer.open_time_us - tf_us; /* 마지막 빈 분 */
            m++;
        }
    }
    return m;
}

void tr_engine_on_orderbook(tr_engine_t *e, uint64_t instrument_id,
                            int64_t event_time_us, double bids, double asks) {
    if (e == 0) {
        return;
    }
    tr_pipeline_t *p = tr_engine_pipe_find(e, instrument_id);
    if (p == 0) {
        return; /* 라우팅할 파이프라인이 없다 (구독 해지 채널의 지연 메시지 등) */
    }
    int64_t day = -1;
    if (!tr_session_trading_day(&p->session, event_time_us, &day)) {
        day = p->has_prev_day ? p->prev_trading_day : -1;
    }
    tr_obd2_eval(&p->obd2, bids, asks, day);
}

bool tr_engine_select_symbol(tr_engine_t *e, uint64_t instrument_id, bool is_futures,
                             const char *shcode, const tr_session_policy_t *session,
                             double tick_raw, bool is_ovs) {
    /* 델타 기록: 파이프라인 분리 전에는 이 함수가 tr_engine_init을 경유해 status_seq가
     * 1로 재시작했다. 이제 파이프라인 0만 재초기화하므로 스트림 시퀀스는 계속 증가한다
     * — 구독자 입장에서 seq 역행이 없어 이 동작을 유지한다. */
    if (e == 0 || instrument_id == 0 || session == 0) {
        return false;
    }
    tr_pipeline_t *p0 = e->pipes[0];
    uint32_t gen = p0->generation;
    /* init는 출력 연결(ipc/callback)을 초기화하므로 보존한다 */
    tr_ipc_t *ipc = e->ipc;
    const char *stream_id = e->stream_id;
    tr_engine_status_fn cb = e->status_cb;
    void *cb_ctx = e->status_cb_ctx;
    tr_engine_config_t cfg = e->cfg;
    cfg.instrument_id = instrument_id;
    sanitize_shcode(cfg.shcode, shcode);
    cfg.is_futures = is_futures;
    cfg.session = *session; /* 새 종목의 세션 정책 (시장이 다르면 바뀐다) */
    cfg.tick_raw = tick_raw;  /* 새 종목의 틱 크기 (0이면 자동 — 해외선물만 명시) */
    cfg.is_ovs = is_ovs;
    /* 파이프라인 0의 지표 상태를 새 종목 기준으로 재구성한다. 링 저장소는 그대로 재사용한다 */
    if (!pipe_init(e, p0, &cfg, p0->bb_storage, p0->bb_capacity,
                   p0->score_mid_storage, p0->score_mid_capacity)) {
        return false;
    }
    e->cfg = cfg; /* 공유 cfg의 선택 종목 정보도 파이프라인 0과 맞춘다 */
    e->ipc = ipc;
    e->stream_id = stream_id;
    e->status_cb = cb;
    e->status_cb_ctx = cb_ctx;
    p0->generation = gen + 1;
    return true;
}

bool tr_engine_pipe_resize(tr_engine_t *e, tr_pipeline_t *p, size_t bb_capacity) {
    if (e == 0 || p == 0 || p->bb_storage == 0 || p->score_mid_storage == 0 || bb_capacity == 0) {
        return false;
    }
    uint32_t gen = p->generation;
    tr_engine_config_t cfg = e->cfg;
    cfg.instrument_id = p->instrument_id;
    sanitize_shcode(cfg.shcode, p->shcode);
    cfg.is_futures = p->is_futures;
    cfg.session = p->session;
    cfg.tick_raw = p->tick_raw;
    cfg.is_ovs = p->is_ovs;
    if (!pipe_init(e, p, &cfg, p->bb_storage, bb_capacity, p->score_mid_storage,
                   p->score_mid_capacity)) {
        return false;
    }
    p->generation = gen + 1;
    if (p->generation == 0) {
        p->generation = 1;
    }
    return true;
}
