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
    e->prev_trading_day = 0;
    e->has_prev_day = false;
    e->prev_bar_open = 0;
    e->has_prev_bar = false;

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

static void publish_status(tr_engine_t *e, const tr_candle_t *bar, bool closed) {
    const tr_lr3_t *r = &e->lr3;
    const tr_score1m_t *sc = &e->score;
    char payload[768];
    int n = snprintf(payload, sizeof(payload),
        "{\"bar_open_time\":\"%lld\",\"closed\":%d,"
        "\"reg_valid\":%d,\"reg_line\":%.10g,\"reg_slope\":%.10g,\"reg_r2\":%.10g,"
        "\"pred\":[%.10g,%.10g,%.10g],\"pred_dir\":[%d,%d,%d],"
        "\"score\":%d,\"future_dir\":%.10g,\"market_dir\":%d,\"reg_dir\":%d,\"ob_dir\":%d}",
        (long long)bar->open_time_us, closed ? 1 : 0,
        r->reg_valid ? 1 : 0, r->line, r->slope, r->r2,
        r->v4.pred_price[0], r->v4.pred_price[1], r->v4.pred_price[2],
        r->v4.pred_dir[0], r->v4.pred_dir[1], r->v4.pred_dir[2],
        sc->score, sc->future_dir, sc->market_dir, sc->reg_dir, sc->ob_dir);
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
    sin.ob_score = 0.0; /* 호가 입력이 없는 경로: 구성요소 미지원 → 0 (유효로 위장하지 않음) */
    tr_score1m_on_bar(&e->score, &sin);

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
