#include "runtime/engine.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/model/civil_time.h"

static void engine_on_bar(void *ctx, const tr_event_envelope_t *env, const tr_candle_t *bar);

bool tr_engine_init(tr_engine_t *e, const tr_engine_config_t *cfg,
                    tr_candle_t *bb_storage, size_t bb_capacity,
                    double *score_mid_storage, size_t score_mid_capacity) {
    if (e == 0 || cfg == 0 || bb_storage == 0 || score_mid_storage == 0 ||
        bb_capacity == 0 || score_mid_capacity < cfg->market_period) {
        return false;
    }
    memset(&e->bb, 0, sizeof(e->bb));
    e->cfg = *cfg;
    e->bb_storage = bb_storage;
    e->bb_capacity = bb_capacity;
    e->score_mid_storage = score_mid_storage;
    e->score_mid_capacity = score_mid_capacity;
    e->ipc = 0;
    e->status_cb = 0;
    e->status_cb_ctx = 0;
    e->stream_id = "display";
    e->status_seq = 1;
    e->generation = 1;
    e->prev_trading_day = 0;
    e->has_prev_day = false;
    e->prev_bar_open = 0;
    e->has_prev_bar = false;
    e->status_ring_on = false;
    e->mkt_on = false; /* 미부착 기본값 (attach_market 성공 시에만 true) */

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
    bbcfg.on_event_ctx = e;
    if (!tr_bar_builder_init(&e->bb, &bbcfg)) {
        return false;
    }
    if (!tr_lr3_init(&e->lr3, TR_COMPRESS_MIN, cfg->timeframe_sec / 60,
                     cfg->predict_bars[0], cfg->predict_bars[1], cfg->predict_bars[2])) {
        return false;
    }
    if (!tr_htf_curve_init(&e->htf, cfg->htf_ticks)) {
        return false;
    }
    if (!tr_score1m_init(&e->score, cfg->market_period, cfg->min_r2,
                         score_mid_storage, score_mid_capacity)) {
        return false;
    }
    /* 호가 지표: 원본 기본값 관심 10 / 강세 35, 부호반전 없음 */
    if (!tr_obd2_init(&e->obd2, 10.0, 35.0, false, cfg->is_futures)) {
        return false;
    }
    /* ⑥ 방향 기억·⑦ 지속 사진 (메인 원본 기본값: 가파른기울기틱 4, 추가확인봉 1, 지속봉수 5).
     * price_scale은 원본 틱 양자화 대응 — raw ×100 단위에서 선물 1틱=1, 주식 1원=100 */
    {
        double ps = cfg->is_futures ? 1.0 : 100.0;
        tr_regmem_config_t rcfg = {ps, 4.0, 1, true};
        tr_regmem_init(&e->regmem, &rcfg);
        tr_persist_config_t pcfg = {ps, 5, cfg->min_r2};
        tr_persist_init(&e->persist, &pcfg);
        /* ⑤ 매매 상태 체인 (원본 기본값: 갭 변동기간 10은 원본 호출부 고정 리터럴) */
        tr_dtl1_config_t dcfg = {cfg->daily_reg_period, cfg->min_r2, ps};
        tr_dtl1_init(&e->dtl1, &dcfg);
        tr_gap1_config_t gcfg = {10, cfg->gap_mid, cfg->gap_big};
        tr_gap1_init(&e->gap1, &gcfg);
        memset(&e->dalign, 0, sizeof(e->dalign));
        e->bar_index = 0;
    }
    return true;
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

bool tr_engine_attach_status_ring(tr_engine_t *e, tr_bar_status_t *storage, size_t capacity) {
    if (e == 0 || storage == 0 || capacity == 0) {
        return false;
    }
    if (!tr_ring_init(&e->status_ring, storage, sizeof(tr_bar_status_t), capacity)) {
        return false;
    }
    e->status_ring_on = true;
    return true;
}

size_t tr_engine_status_count(const tr_engine_t *e) {
    return (e != 0 && e->status_ring_on) ? tr_ring_count(&e->status_ring) : 0;
}

bool tr_engine_status_at(const tr_engine_t *e, size_t back_index, tr_bar_status_t *out) {
    if (e == 0 || out == 0 || !e->status_ring_on) {
        return false;
    }
    return tr_ring_at(&e->status_ring, back_index, out);
}

bool tr_engine_attach_market(tr_engine_t *e, tr_candle_t *storage, size_t capacity) {
    if (e == 0 || storage == 0 || capacity == 0) {
        return false;
    }
    double ps = e->cfg.is_futures ? 1.0 : 100.0;
    if (!tr_market_init(&e->mkt, e->cfg.market_period, 1.0, ps, storage, capacity)) {
        return false;
    }
    e->mkt_on = true;
    return true;
}

static void publish_status(tr_engine_t *e, const tr_candle_t *bar, bool closed) {
    const tr_lr3_t *r = &e->lr3;
    const tr_score1m_t *sc = &e->score;
    const tr_market_t *m = &e->mkt;
    const tr_regmem_t *rm = &e->regmem;
    const tr_persist_t *ps = &e->persist;
    const tr_dalign2_output_t *fa = &e->dalign;
    /* 곡선회귀선_평탄: 회귀선을 틱 단위로 반올림 (raw ×100 단위에서 선물 1틱=1, 주식 1원=100) */
    double pscale = e->cfg.is_futures ? 1.0 : 100.0;
    double reg_flat = floor(e->lr3.line / pscale + 0.5) * pscale;
    char payload[1664];
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
        "\"final\":[%d,%d,%d,%d],\"reg_flat\":%.10g}",
        (long long)bar->open_time_us, closed ? 1 : 0,
        (long long)bar->open, (long long)bar->high, (long long)bar->low, (long long)bar->close,
        r->reg_valid ? 1 : 0, r->line, r->slope, r->r2,
        r->v4.pred_price[0], r->v4.pred_price[1], r->v4.pred_price[2],
        r->v4.pred_dir[0], r->v4.pred_dir[1], r->v4.pred_dir[2],
        r->residual, r->v4.volatility,
        sc->score, sc->future_dir, sc->market_dir, sc->reg_dir, sc->ob_dir,
        e->obd2.validity == TR_VALIDITY_VALID ? 1 : 0, e->obd2.score, e->generation,
        (e->mkt_on && m->valid) ? 1 : 0, m->center, m->upper1, m->lower1, m->upper2, m->lower2,
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
        reg_flat);
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

static void engine_on_bar(void *ctx, const tr_event_envelope_t *env, const tr_candle_t *bar) {
    tr_engine_t *e = (tr_engine_t *)ctx;
    if (bar->timeframe_sec != e->cfg.timeframe_sec) {
        return;
    }

    /* 지표용 내장 컨텍스트 계산: trading day / 당일 첫 봉 / 새 봉 여부 */
    int64_t day = -1;
    tr_session_trading_day(&e->cfg.session, bar->open_time_us, &day);
    bool session_first = !e->has_prev_day || day != e->prev_trading_day;
    bool is_new_bar = !e->has_prev_bar || bar->open_time_us != e->prev_bar_open;
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

    tr_lr3_eval(&e->lr3, &ev);
    if (closed) {
        /* 확정 봉의 H/L/C를 V4의 ATR에 반영 (증분 계약) */
        tr_lp4_on_bar_closed(&e->lr3.v4, (double)bar->high, (double)bar->low, (double)bar->close);
    }
    tr_htf_curve_eval(&e->htf, (double)bar->high, (double)bar->low);

    tr_score1m_input_t sin;
    memset(&sin, 0, sizeof(sin));
    sin.high = (double)bar->high;
    sin.low = (double)bar->low;
    sin.close = (double)bar->close;
    sin.htf_direction = e->htf.direction;
    sin.trading_day = day;
    sin.is_min_1 = e->cfg.timeframe_sec == 60;
    sin.reg_valid = e->lr3.reg_valid;
    sin.reg_r2 = e->lr3.r2;
    sin.reg_flat_line = e->lr3.line;
    /* 호가 유효할 때만 점수를 사용한다. 없는 경로(파일 재생)에서는 0 (유효로 위장하지 않음) */
    sin.ob_score = e->obd2.validity == TR_VALIDITY_VALID ? e->obd2.score : 0.0;
    tr_score1m_on_bar(&e->score, &sin);

    /* ⑤ 매매 상태 체인 (원본: DataCompress==2 && BarInterval==1 게이트).
     * 봉당 1회 상태 진행: bar_index는 새 봉에서만 증가하고, 모듈은 세션 경계를
     * bar_index 게이트로 1회만 저장한다 (진행 봉 재호출은 현재 봉 H/L/C 집계만 갱신).
     * 과거 봉 정정(늦은 틱) 이벤트는 체인에서 제외한다 — 마지막 확정 final_* 값이 유지된다. */
    if (e->cfg.timeframe_sec == 60 &&
        (!e->has_prev_bar || bar->open_time_us >= e->prev_bar_open)) {
        if (is_new_bar) {
            e->bar_index++;
        }
        uint32_t bar_min = 0;
        tr_local_day_and_min(bar->open_time_us, e->cfg.session.utc_offset_min, 0, &bar_min);
        tr_dtl1_on_bar(&e->dtl1, (double)bar->high, (double)bar->low, (double)bar->close,
                       session_first, (int64_t)e->bar_index, true);
        tr_gap1_on_bar(&e->gap1, (double)bar->open, (double)bar->high, (double)bar->low,
                       (double)bar->close, (int32_t)bar_min, session_first,
                       (int64_t)e->bar_index, true);
        if (e->gap1.valid) {
            /* 원본 게이트: 운영갭유효==1 일 때만 일봉 정렬 평가 */
            tr_dalign2_input_t din;
            memset(&din, 0, sizeof(din));
            din.pred_dir[0] = e->lr3.v4.pred_dir[0];
            din.pred_dir[1] = e->lr3.v4.pred_dir[1];
            din.pred_dir[2] = e->lr3.v4.pred_dir[2];
            din.reg_valid = e->lr3.reg_valid;
            din.r2 = e->lr3.r2;
            din.trend_dir = e->dtl1.trend.dir;
            din.trend_state = e->dtl1.trend.state;
            din.trend_strength = e->dtl1.trend.strength;
            din.trend_valid = e->dtl1.trend.valid;
            din.gap_grade = e->gap1.gap_grade;
            din.daily_weight_in = e->gap1.daily_weight;
            din.elapsed_min = (double)e->gap1.elapsed_min;
            din.big_gap_reeval_min = (double)e->cfg.big_gap_reeval_min;
            din.min_r2 = e->cfg.min_r2;
            din.min_final_strength = (double)e->cfg.min_final_strength;
            tr_dalign2_eval(&din, &e->dalign);
        } else {
            memset(&e->dalign, 0, sizeof(e->dalign)); /* 갭 무효 → 운영최종*=0 유지 */
        }
    }

    /* MTF상단/하단1~3 (원본 메인의 오차 띠 공식, 부채꼴·기억선 공용):
     * 기준오차 = max(회귀잔차, 예측변동성×0.25), 범위 = 기준오차 × √예측봉수 */
    double band_base = e->lr3.residual > e->lr3.v4.volatility * 0.25
                           ? e->lr3.residual
                           : e->lr3.v4.volatility * 0.25;
    double upper[3], lower[3];
    for (int i = 0; i < 3; i++) {
        double span = (double)e->cfg.predict_bars[i];
        double band = band_base * sqrt(span > 1.0 ? span : 1.0);
        upper[i] = e->lr3.v4.pred_price[i] + band;
        lower[i] = e->lr3.v4.pred_price[i] - band;
    }

    /* 곡선회귀선_평탄: 원본 틱 양자화 */
    double ps = e->cfg.is_futures ? 1.0 : 100.0;
    double line_flat = floor(e->lr3.line / ps + 0.5) * ps;

    /* ⑧ 마켓 밴드 */
    if (e->mkt_on) {
        tr_market_on_bar(&e->mkt, bar, session_first, line_flat);
    }

    /* 최근 5봉 H/L ([0]=현재) — 기억선 이탈 검사용 */
    double h5[5], l5[5];
    size_t hl_count = 0;
    {
        size_t nb = tr_ring_count(&e->bb.bars);
        if (nb > 5) {
            nb = 5;
        }
        for (size_t i = 0; i < nb; i++) {
            tr_candle_t c;
            if (tr_ring_at(&e->bb.bars, i, &c)) {
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
        rin.reg_valid = e->lr3.reg_valid;
        rin.line_flat = line_flat;
        rin.line_sign = e->lr3.line_sign;
        rin.slope = e->lr3.slope;
        rin.final_dir = e->dalign.final_dir;
        rin.final_dir_valid = e->dalign.final_valid;
        rin.pred_price[0] = e->lr3.v4.pred_price[0];
        rin.pred_price[1] = e->lr3.v4.pred_price[1];
        rin.pred_price[2] = e->lr3.v4.pred_price[2];
        memcpy(rin.upper, upper, sizeof(upper));
        memcpy(rin.lower, lower, sizeof(lower));
        rin.session_no = (uint64_t)(day > 0 ? day : 0);
        rin.compress_min = true;
        rin.compress_min_le30 = e->cfg.timeframe_sec <= 1800;
        rin.ob_applicable = e->obd2.validity == TR_VALIDITY_VALID;
        rin.ob_state = e->score.ob_dir;
        rin.h5 = h5;
        rin.l5 = l5;
        rin.hl_count = hl_count;
        tr_regmem_on_bar(&e->regmem, &rin);
    }

    /* ⑦ 지속 사진 */
    {
        tr_persist_input_t pin;
        memset(&pin, 0, sizeof(pin));
        pin.reg_valid = e->lr3.reg_valid;
        pin.r2 = e->lr3.r2;
        pin.pred_dir2 = e->lr3.v4.pred_dir[1];
        pin.pred_price[0] = e->lr3.v4.pred_price[0];
        pin.pred_price[1] = e->lr3.v4.pred_price[1];
        pin.pred_price[2] = e->lr3.v4.pred_price[2];
        memcpy(pin.upper, upper, sizeof(upper));
        memcpy(pin.lower, lower, sizeof(lower));
        pin.h5 = h5;
        pin.l5 = l5;
        pin.hl_count = hl_count;
        tr_persist_on_bar(&e->persist, &pin);
    }

    /* 봉 링과 open_time 기준으로 정합을 맞춰 봉별 지표를 기록한다 (스냅샷 복원용).
     * 새 봉이면 push, 현재 봉 갱신이면 최신 교체, 늦은 정정은 해당 슬롯만 고친다. */
    if (e->status_ring_on) {
        tr_bar_status_t st;
        memset(&st, 0, sizeof(st));
        st.open_time_us = bar->open_time_us;
        st.closed = closed;
        st.reg_valid = e->lr3.reg_valid;
        st.reg_line = e->lr3.line;
        st.reg_r2 = e->lr3.r2;
        st.pred[0] = e->lr3.v4.pred_price[0];
        st.pred[1] = e->lr3.v4.pred_price[1];
        st.pred[2] = e->lr3.v4.pred_price[2];
        st.pred_dir[0] = e->lr3.v4.pred_dir[0];
        st.pred_dir[1] = e->lr3.v4.pred_dir[1];
        st.pred_dir[2] = e->lr3.v4.pred_dir[2];
        st.residual = e->lr3.residual;
        st.pvol = e->lr3.v4.volatility;
        memcpy(st.upper, upper, sizeof(upper));
        memcpy(st.lower, lower, sizeof(lower));
        st.score = e->score.score;
        st.ob_valid = e->obd2.validity == TR_VALIDITY_VALID;
        st.ob_score = e->obd2.score;
        st.mkt_valid = e->mkt_on && e->mkt.valid;
        st.mkt_center = e->mkt.center;
        st.mkt_u1 = e->mkt.upper1;
        st.mkt_l1 = e->mkt.lower1;
        st.mkt_u2 = e->mkt.upper2;
        st.mkt_l2 = e->mkt.lower2;
        st.mem_valid = e->regmem.mem_valid;
        st.mem_updated = e->regmem.updated;
        st.mem_dir = e->regmem.mem_dir;
        st.mem_price = e->regmem.mem_price;
        memcpy(st.mem_target, e->regmem.mem_target, sizeof(st.mem_target));
        memcpy(st.mem_upper, e->regmem.mem_upper, sizeof(st.mem_upper));
        memcpy(st.mem_lower, e->regmem.mem_lower, sizeof(st.mem_lower));
        st.mem_show_targets = e->regmem.show_targets;
        st.mem_show_upper = e->regmem.show_upper;
        st.mem_show_lower = e->regmem.show_lower;
        st.pst_saved = e->persist.streak == e->persist.cfg.persist_bars &&
                       e->persist.saved_valid;
        st.pst_valid = e->persist.saved_valid;
        st.pst_dir = e->persist.saved_dir;
        memcpy(st.pst_target, e->persist.target, sizeof(st.pst_target));
        memcpy(st.pst_upper, e->persist.upper, sizeof(st.pst_upper));
        memcpy(st.pst_lower, e->persist.lower, sizeof(st.pst_lower));
        st.final_valid = e->dalign.final_valid ? 1 : 0;
        st.final_dir = e->dalign.final_dir;
        st.final_state = e->dalign.final_state;
        st.final_strength = (int)e->dalign.final_strength;
        tr_bar_status_t newest;
        if (tr_ring_count(&e->status_ring) == 0 ||
            (tr_ring_at(&e->status_ring, 0, &newest) && bar->open_time_us > newest.open_time_us)) {
            tr_ring_push(&e->status_ring, &st);
        } else if (bar->open_time_us == newest.open_time_us) {
            tr_ring_update_newest(&e->status_ring, &st);
        } else {
            for (size_t i = 1; i < tr_ring_count(&e->status_ring); i++) {
                tr_bar_status_t old;
                if (!tr_ring_at(&e->status_ring, i, &old) || old.open_time_us < bar->open_time_us) {
                    break;
                }
                if (old.open_time_us == bar->open_time_us) {
                    /* 해당 과거 슬롯만 정정한다 (구조는 바꾸지 않음) */
                    tr_bar_status_t *slot = (tr_bar_status_t *)tr_ring_get_mut(&e->status_ring, i);
                    if (slot != 0) {
                        *slot = st;
                    }
                    break;
                }
            }
        }
    }

    publish_status(e, bar, closed);

    /* 스트림 위치 마커는 현재 봉 이벤트에서만 전진한다. 과거 봉 정정(늦은 틱)이
     * trading day·봉 위치를 되돌리면 다음 현재 봉이 세션 첫 봉/새 봉으로 오인되어
     * ⑤ 일봉 체인이 중간 집계를 완성 일봉으로 오저장할 수 있다 */
    if (!e->has_prev_bar || bar->open_time_us >= e->prev_bar_open) {
        e->prev_trading_day = day;
        e->has_prev_day = true;
        e->prev_bar_open = bar->open_time_us;
        e->has_prev_bar = true;
    }
}

tr_bb_status_t tr_engine_on_tick(tr_engine_t *e, const tr_event_envelope_t *env, const tr_tick_t *tick) {
    if (e == 0) {
        return TR_BB_ERROR;
    }
    return tr_bar_builder_on_tick(&e->bb, env, tick);
}

void tr_engine_on_timer(tr_engine_t *e, tr_time_us_t now_us) {
    if (e == 0) {
        return;
    }
    tr_bar_builder_on_timer(&e->bb, now_us);
}

bool tr_engine_inject_bar(tr_engine_t *e, const tr_candle_t *bar) {
    if (e == 0) {
        return false;
    }
    return tr_bar_builder_inject_bar(&e->bb, bar != 0 ? bar->close_time_us : 0, bar);
}

void tr_engine_on_orderbook(tr_engine_t *e, int64_t event_time_us, double bids, double asks) {
    if (e == 0) {
        return;
    }
    int64_t day = -1;
    if (!tr_session_trading_day(&e->cfg.session, event_time_us, &day)) {
        day = e->has_prev_day ? e->prev_trading_day : -1;
    }
    tr_obd2_eval(&e->obd2, bids, asks, day);
}

bool tr_engine_select_symbol(tr_engine_t *e, uint64_t instrument_id, bool is_futures) {
    if (e == 0 || instrument_id == 0) {
        return false;
    }
    uint32_t gen = e->generation;
    /* init는 출력 연결(ipc/callback)을 초기화하므로 보존한다 */
    tr_ipc_t *ipc = e->ipc;
    const char *stream_id = e->stream_id;
    tr_engine_status_fn cb = e->status_cb;
    void *cb_ctx = e->status_cb_ctx;
    tr_engine_config_t cfg = e->cfg;
    cfg.instrument_id = instrument_id;
    cfg.is_futures = is_futures;
    /* 지표 상태를 새 종목 기준으로 재구성한다. 링 저장소는 그대로 재사용한다 */
    if (!tr_engine_init(e, &cfg, e->bb_storage, e->bb_capacity,
                        e->score_mid_storage, e->score_mid_capacity)) {
        return false;
    }
    e->ipc = ipc;
    e->stream_id = stream_id;
    e->status_cb = cb;
    e->status_cb_ctx = cb_ctx;
    e->generation = gen + 1;
    return true;
}
