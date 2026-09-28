#include "runtime/engine.h"

#include <stdio.h>
#include <string.h>

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
    return tr_obd2_init(&e->obd2, 10.0, 35.0, false, cfg->is_futures);
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

static void publish_status(tr_engine_t *e, const tr_candle_t *bar, bool closed) {
    const tr_lr3_t *r = &e->lr3;
    const tr_score1m_t *sc = &e->score;
    char payload[768];
    int n = snprintf(payload, sizeof(payload),
        "{\"bar_open_time\":\"%lld\",\"closed\":%d,"
        "\"ohlc\":[%lld,%lld,%lld,%lld],"
        "\"reg_valid\":%d,\"reg_line\":%.10g,\"reg_slope\":%.10g,\"reg_r2\":%.10g,"
        "\"pred\":[%.10g,%.10g,%.10g],\"pred_dir\":[%d,%d,%d],"
        "\"score\":%d,\"future_dir\":%.10g,\"market_dir\":%d,\"reg_dir\":%d,\"ob_dir\":%d,"
        "\"ob_valid\":%d,\"ob_score\":%.10g,\"generation\":%u}",
        (long long)bar->open_time_us, closed ? 1 : 0,
        (long long)bar->open, (long long)bar->high, (long long)bar->low, (long long)bar->close,
        r->reg_valid ? 1 : 0, r->line, r->slope, r->r2,
        r->v4.pred_price[0], r->v4.pred_price[1], r->v4.pred_price[2],
        r->v4.pred_dir[0], r->v4.pred_dir[1], r->v4.pred_dir[2],
        sc->score, sc->future_dir, sc->market_dir, sc->reg_dir, sc->ob_dir,
        e->obd2.validity == TR_VALIDITY_VALID ? 1 : 0, e->obd2.score, e->generation);
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
        st.score = e->score.score;
        st.ob_valid = e->obd2.validity == TR_VALIDITY_VALID;
        st.ob_score = e->obd2.score;
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

    e->prev_trading_day = day;
    e->has_prev_day = true;
    e->prev_bar_open = bar->open_time_us;
    e->has_prev_bar = true;
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
