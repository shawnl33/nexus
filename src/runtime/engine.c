#include "runtime/engine.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/model/civil_time.h"

static void engine_on_bar(void *ctx, const tr_event_envelope_t *env, const tr_candle_t *bar);

/* 원본 PriceScale 대응: raw 가격 단위의 1틱 — 선물 0.05pt×100=5, 주식 1원×100=100
 * (raw는 주식·선물 모두 실제 × 100 스케일: ls_chart.c parse_price_scaled) */
static double tick_scale(const tr_pipeline_t *p) {
    return p->is_futures ? 5.0 : 100.0;
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
        /* score의 mid_hist(yl_var)는 relink 불필요: 저장소가 기증 슬롯 남부가 아니라
         * 호출자 소유 외부 버퍼(score_mid_storage)라 복사된 포인터가 그대로 올바르고,
         * 슬롯 재사용(add)도 새 파이프라인 자신의 저장소 인자를 쓴다 (score_1m.h 참조) */
        e->cfg.instrument_id = e->pipe0.instrument_id;
        e->cfg.is_futures = e->pipe0.is_futures;
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
                           bool closed, int64_t trading_day) {
    const tr_lr3_t *r = &p->lr3;
    const tr_score1m_t *sc = &p->score;
    const tr_market_t *m = &p->mkt;
    const tr_regmem_t *rm = &p->regmem;
    const tr_persist_t *ps = &p->persist;
    const tr_dalign2_output_t *fa = &p->dalign;
    /* 곡선회귀선_평탄: 회귀선을 틱 단위로 반올림 (tick_scale 헬퍼 참조) */
    double pscale = tick_scale(p);
    double reg_flat = floor(p->lr3.line / pscale + 0.5) * pscale;
    int sma_ok = (p->sma5.valid && p->sma20.valid && p->sma60.valid) ? 1 : 0;
    char payload[1792];
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
        "\"sma\":[%d,%.10g,%.10g,%.10g],\"shcode\":\"%s\"}",
        (long long)bar->open_time_us, closed ? 1 : 0,
        (long long)bar->open, (long long)bar->high, (long long)bar->low, (long long)bar->close,
        r->reg_valid ? 1 : 0, r->line, r->slope, r->r2,
        r->v4.pred_price[0], r->v4.pred_price[1], r->v4.pred_price[2],
        r->v4.pred_dir[0], r->v4.pred_dir[1], r->v4.pred_dir[2],
        r->residual, r->v4.volatility,
        sc->score, sc->future_dir, sc->market_dir, sc->reg_dir, sc->ob_dir,
        p->obd2.validity == TR_VALIDITY_VALID ? 1 : 0, p->obd2.score, p->generation,
        (p->mkt_on && m->valid) ? 1 : 0, m->center, m->upper1, m->lower1, m->upper2, m->lower2,
        rm->mem_valid ? 1 : 0, rm->updated ? 1 : 0, rm->mem_dir, rm->mem_price,
        rm->mem_target[0], rm->mem_target[1], rm->mem_target[2],
        rm->mem_upper[0], rm->mem_upper[1], rm->mem_upper[2],
        rm->mem_lower[0], rm->mem_lower[1], rm->mem_lower[2],
        ps->streak == ps->cfg.persist_bars && ps->saved_valid ? 1 : 0,
        ps->saved_valid ? 1 : 0, ps->saved_dir,
        ps->target[0], ps->target[1], ps->target[2],
        ps->upper[0], ps->upper[1], ps->upper[2],
        ps->lower[0], ps->lower[1], ps->lower[2],
        fa->final_valid ? 1 : 0, fa->final_dir, fa->final_state, (int)fa->final_strength,
        reg_flat, tick_scale(p), (long long)trading_day,
        sma_ok, p->sma5.value, p->sma20.value, p->sma60.value, p->shcode);
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
    return snprintf(buf, cap,
                    "%s[%d,%d,%.10g,%.10g,%.10g,%.10g,%.10g,%d,%d,%.10g,%.10g,%.10g,%d,%d,%d,"
                    "%d,%.10g,%.10g,%.10g,%.10g,%.10g,%d,%d,%d,%d,%.10g,%d,%lld,%d,%.10g,%.10g,%.10g]",
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

static void engine_on_bar(void *ctx, const tr_event_envelope_t *env, const tr_candle_t *bar) {
    tr_pipeline_t *p = (tr_pipeline_t *)ctx;
    tr_engine_t *e = p->engine;
    if (bar->timeframe_sec != e->cfg.timeframe_sec) {
        return;
    }

    /* 지표용 내장 컨텍스트 계산: trading day / 당일 첫 봉 / 새 봉 여부
     * (종목별 세션 기준 — 파이프라인마다 다를 수 있다) */
    int64_t day = -1;
    tr_session_trading_day(&p->session, bar->open_time_us, &day);
    bool session_first = !p->has_prev_day || day != p->prev_trading_day;
    bool is_new_bar = !p->has_prev_bar || bar->open_time_us != p->prev_bar_open;
    /* 과거 봉 정정(늦은 틱): open_time이 스트림 위치보다 과거인 이벤트. 정정은 누적
     * 상태(이평선 창·⑤ 체인·위치 마커)를 전진시키지 않는다 (⑤ 체인 게이트와 동일) */
    bool is_correction = p->has_prev_bar && bar->open_time_us < p->prev_bar_open;
    bool closed = env->kind == TR_EVENT_CANDLE_CLOSED;

    tr_ind_eval_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.bar = bar;
    ev.event_time_us = env->event_time_us;
    ev.trading_day = day;
    ev.is_new_bar = is_new_bar;
    ev.is_session_first = session_first;
    ev.compress = TR_COMPRESS_MIN;
    ev.bar_interval = e->cfg.timeframe_sec / 60;

    tr_lr3_eval(&p->lr3, &ev);
    if (closed) {
        /* 확정 봉의 H/L/C를 V4의 ATR에 반영 (증분 계약) */
        tr_lp4_on_bar_closed(&p->lr3.v4, (double)bar->high, (double)bar->low, (double)bar->close);
    }
    tr_htf_curve_eval(&p->htf, (double)bar->high, (double)bar->low);

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
    /* 호가 유효할 때만 점수를 사용한다. 없는 경로(파일 재생)에서는 0 (유효로 위장하지 않음) */
    sin.ob_score = p->obd2.validity == TR_VALIDITY_VALID ? p->obd2.score : 0.0;
    tr_score1m_on_bar(&p->score, &sin);

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

    /* ⑧ 마켓 밴드 */
    if (p->mkt_on) {
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

    /* ⑥ 방향 기억 (운영최종방향 = ⑤ 일봉 체인 결과, 무효 시 회귀선_구분 부호 폴백) */
    {
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

    /* ⑦ 지속 사진 */
    {
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

    /* 봉 링과 open_time 기준으로 정합을 맞춰 봉별 지표를 기록한다 (스냅샷 복원용).
     * 새 봉이면 push, 현재 봉 갱신이면 최신 교체, 늦은 정정은 해당 슬롯만 고친다. */
    if (p->status_ring_on) {
        tr_bar_status_t st;
        memset(&st, 0, sizeof(st));
        st.open_time_us = bar->open_time_us;
        st.closed = closed;
        st.reg_valid = p->lr3.reg_valid;
        st.reg_line = p->lr3.line;
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
        st.ob_valid = p->obd2.validity == TR_VALIDITY_VALID;
        st.ob_score = p->obd2.score;
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

    publish_status(e, p, bar, closed, day);

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
                             const char *shcode, const tr_session_policy_t *session) {
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
