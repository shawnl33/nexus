#include "core/market/bar_builder.h"

#include <string.h>

#include "core/model/civil_time.h"

static void make_envelope(const tr_bar_builder_t *bb, tr_event_kind_t kind,
                          tr_time_us_t event_time, tr_quality_flags_t quality,
                          tr_event_envelope_t *out) {
    memset(out, 0, sizeof(*out));
    out->kind = kind;
    out->schema_version = 1;
    out->engine_instance_id = bb->cfg.engine_instance_id;
    out->sequence = bb->next_sequence;
    out->source_id = bb->cfg.source_id;
    out->event_time_us = event_time;
    out->received_time_us = event_time;
    out->quality = quality;
}

static void emit(tr_bar_builder_t *bb, tr_event_kind_t kind, tr_time_us_t event_time, const tr_candle_t *bar) {
    if (bb->cfg.on_event == 0) {
        return;
    }
    tr_event_envelope_t env;
    make_envelope(bb, kind, event_time, bar->quality, &env);
    bb->next_sequence++;
    bb->cfg.on_event(bb->cfg.on_event_ctx, &env, bar);
}

/* 세션 개장 기준으로 t가 속한 봉 구간 [open, close) 를 구한다. 세션 밖이면 false. */
static bool bar_interval(const tr_bar_builder_t *bb, tr_time_us_t t,
                         tr_time_us_t *out_open, tr_time_us_t *out_close,
                         tr_time_us_t *out_session_close) {
    tr_time_us_t session_open, session_close;
    if (!tr_session_span(&bb->cfg.session, t, &session_open, &session_close)) {
        return false;
    }
    int64_t tf_us = (int64_t)bb->cfg.timeframe_sec * TR_US_PER_SEC;
    int64_t k = (t - session_open) / tf_us; /* t >= session_open 이므로 음수 아님 */
    tr_time_us_t open = session_open + k * tf_us;
    tr_time_us_t close = open + tf_us;
    if (close > session_close) {
        close = session_close; /* 세션 마지막 봉은 짧을 수 있다 */
    }
    *out_open = open;
    *out_close = close;
    if (out_session_close != 0) {
        *out_session_close = session_close;
    }
    return true;
}

static void apply_tick_to_bar(tr_candle_t *bar, const tr_tick_t *tick) {
    if (tick->price > bar->high) {
        bar->high = tick->price;
    }
    if (tick->price < bar->low) {
        bar->low = tick->price;
    }
    bar->close = tick->price;
    bar->volume += tick->qty;
}

bool tr_bar_builder_init(tr_bar_builder_t *bb, const tr_bar_builder_config_t *cfg) {
    if (bb == 0 || cfg == 0 || cfg->ring_storage == 0 || cfg->ring_capacity == 0) {
        return false;
    }
    if (cfg->instrument_id == 0 || cfg->timeframe_sec == 0) {
        return false;
    }
    if (!tr_session_policy_validate(&cfg->session)) {
        return false;
    }
    memset(&bb->bars, 0, sizeof(bb->bars));
    bb->cfg = *cfg;
    if (!tr_ring_init(&bb->bars, cfg->ring_storage, sizeof(tr_candle_t), cfg->ring_capacity)) {
        return false;
    }
    bb->has_open = false;
    bb->last_close = 0;
    bb->has_last_close = false;
    bb->last_source_exec_id = 0;
    bb->next_sequence = 1;
    bb->n_duplicates = 0;
    bb->n_late_corrected = 0;
    bb->n_late_dropped = 0;
    bb->n_out_of_session = 0;
    bb->n_filled_empty = 0;
    return true;
}

static void close_current(tr_bar_builder_t *bb, tr_time_us_t event_time) {
    tr_candle_t *cur = (tr_candle_t *)tr_ring_get_mut(&bb->bars, 0);
    cur->state = TR_CANDLE_CLOSED;
    bb->has_open = false;
    bb->last_close = cur->close;
    bb->has_last_close = true;
    emit(bb, TR_EVENT_CANDLE_CLOSED, event_time, cur);
}

/* [from_open, limit_us) 구간의 무거래 봉을 전 종가로 채운다. session_close를 넘지 않는다. */
static void fill_empty_bars(tr_bar_builder_t *bb, tr_time_us_t from_open, tr_time_us_t limit_us,
                            tr_time_us_t session_close, tr_time_us_t event_time) {
    if (bb->cfg.no_trade != TR_NO_TRADE_FILL || !bb->has_last_close) {
        return;
    }
    int64_t tf_us = (int64_t)bb->cfg.timeframe_sec * TR_US_PER_SEC;
    tr_time_us_t next_open = from_open;
    while (next_open < session_close && next_open + tf_us <= limit_us) {
        tr_candle_t fill;
        memset(&fill, 0, sizeof(fill));
        fill.instrument_id = bb->cfg.instrument_id;
        fill.timeframe_sec = bb->cfg.timeframe_sec;
        fill.open_time_us = next_open;
        fill.close_time_us = next_open + tf_us;
        if (fill.close_time_us > session_close) {
            fill.close_time_us = session_close;
        }
        fill.open = bb->last_close;
        fill.high = bb->last_close;
        fill.low = bb->last_close;
        fill.close = bb->last_close;
        fill.volume = 0;
        fill.state = TR_CANDLE_CLOSED; /* 무거래 확정 봉 */
        fill.revision = 0;
        fill.source_id = bb->cfg.source_id;
        fill.quality = TR_QUALITY_FILLED_EMPTY;
        tr_ring_push(&bb->bars, &fill);
        bb->n_filled_empty++;
        emit(bb, TR_EVENT_CANDLE_CLOSED, event_time, &fill);
        next_open = fill.close_time_us;
    }
}

static tr_bb_status_t handle_late_tick(tr_bar_builder_t *bb, const tr_event_envelope_t *env,
                                       const tr_tick_t *tick, tr_time_us_t open, tr_time_us_t close) {
    /* 확정된 과거 봉을 찾아 정정한다 */
    for (size_t i = 0; i < bb->bars.count; i++) {
        tr_candle_t *bar = (tr_candle_t *)tr_ring_get_mut(&bb->bars, i);
        if (bar->open_time_us == open && bar->close_time_us == close) {
            apply_tick_to_bar(bar, tick);
            bar->revision++;
            bar->quality |= TR_QUALITY_CORRECTED | TR_QUALITY_LATE;
            emit(bb, TR_EVENT_CANDLE_UPDATE, env->event_time_us, bar);
            bb->n_late_corrected++;
            return TR_BB_LATE_CORRECTED;
        }
    }
    bb->n_late_dropped++; /* 조용히 버리지 않고 기록한다 */
    return TR_BB_LATE_DROPPED;
}

tr_bb_status_t tr_bar_builder_on_tick(tr_bar_builder_t *bb, const tr_event_envelope_t *env, const tr_tick_t *tick) {
    if (bb == 0 || env == 0 || tick == 0 || tick->instrument_id != bb->cfg.instrument_id || tick->qty < 0) {
        return TR_BB_ERROR;
    }
    tr_time_us_t t = env->event_time_us;

    /* 중복 체결 감지. exec_id가 단조 증가하는 출처를 가정한다. */
    if (tick->source_exec_id != 0) {
        if (tick->source_exec_id <= bb->last_source_exec_id) {
            bb->n_duplicates++;
            return TR_BB_DUPLICATE;
        }
        bb->last_source_exec_id = tick->source_exec_id;
    }

    tr_time_us_t open, close;
    if (!bar_interval(bb, t, &open, &close, 0)) {
        bb->n_out_of_session++;
        return TR_BB_REJECTED_OUT_OF_SESSION;
    }

    bool closed_prev = false;
    if (bb->has_open) {
        const tr_candle_t *cur = (const tr_candle_t *)tr_ring_get_mut(&bb->bars, 0);
        if (t >= cur->open_time_us && t < cur->close_time_us) {
            apply_tick_to_bar((tr_candle_t *)cur, tick);
            emit(bb, TR_EVENT_CANDLE_UPDATE, t, cur);
            return TR_BB_ACCEPTED;
        }
        if (t < cur->open_time_us) {
            return handle_late_tick(bb, env, tick, open, close);
        }
        /* t >= cur->close_time_us: 경계를 넘었으므로 확정 후 새 봉 */
        tr_time_us_t closed_close = cur->close_time_us;
        tr_time_us_t session_close;
        bool have_session = tr_session_span(&bb->cfg.session, cur->open_time_us, 0, &session_close);
        close_current(bb, t);
        closed_prev = true;
        /* 틱이 여러 구간을 건너뛰었으면 사이의 무거래 봉을 채운다 */
        if (have_session) {
            fill_empty_bars(bb, closed_close, open, session_close, t);
        }
    } else if (bb->bars.count > 0) {
        /* 마지막 확정 봉 이후 타이머 없이 건어뛴 구간도 채운다 */
        tr_candle_t last;
        if (tr_ring_at(&bb->bars, 0, &last) && last.state == TR_CANDLE_CLOSED &&
            last.close_time_us < open) {
            tr_time_us_t session_close;
            if (tr_session_span(&bb->cfg.session, last.open_time_us, 0, &session_close)) {
                fill_empty_bars(bb, last.close_time_us, open, session_close, t);
            }
        }
    }

    tr_candle_t bar;    memset(&bar, 0, sizeof(bar));
    bar.instrument_id = tick->instrument_id;
    bar.timeframe_sec = bb->cfg.timeframe_sec;
    bar.open_time_us = open;
    bar.close_time_us = close;
    bar.open = tick->price;
    bar.high = tick->price;
    bar.low = tick->price;
    bar.close = tick->price;
    bar.volume = tick->qty;
    bar.state = TR_CANDLE_OPEN;
    bar.revision = 0;
    bar.source_id = bb->cfg.source_id;
    bar.quality = env->quality;
    tr_ring_push(&bb->bars, &bar);
    bb->has_open = true;
    emit(bb, TR_EVENT_CANDLE_UPDATE, t, &bar);
    return closed_prev ? TR_BB_ACCEPTED_NEW_BAR : TR_BB_ACCEPTED;
}

void tr_bar_builder_on_timer(tr_bar_builder_t *bb, tr_time_us_t now_us) {
    if (bb == 0) {
        return;
    }
    if (bb->has_open) {
        const tr_candle_t *cur = (const tr_candle_t *)tr_ring_get_mut(&bb->bars, 0);
        tr_time_us_t session_close;
        if (tr_session_span(&bb->cfg.session, cur->open_time_us, 0, &session_close) && now_us >= session_close) {
            close_current(bb, session_close); /* 세션 폐장에 의한 강제 확정 */
        } else if (now_us >= cur->close_time_us) {
            close_current(bb, now_us); /* 봉 경계에 의한 확정 */
        }
    }

    if (bb->cfg.no_trade != TR_NO_TRADE_FILL || !bb->has_last_close) {
        return;
    }
    /* 마지막 확정 봉 이후의 빈 구간을 채운다 */
    tr_candle_t last;
    if (!tr_ring_at(&bb->bars, 0, &last) || last.state != TR_CANDLE_CLOSED) {
        return;
    }
    tr_time_us_t session_close;
    if (!tr_session_span(&bb->cfg.session, last.open_time_us, 0, &session_close)) {
        return;
    }
    fill_empty_bars(bb, last.close_time_us, now_us, session_close, now_us);
}

const tr_candle_t *tr_bar_builder_current(const tr_bar_builder_t *bb) {
    if (bb == 0 || !bb->has_open) {
        return 0;
    }
    return (const tr_candle_t *)tr_ring_get_mut((tr_ring *)&bb->bars, 0);
}

bool tr_bar_builder_inject_bar(tr_bar_builder_t *bb, tr_time_us_t event_time, const tr_candle_t *bar) {
    if (bb == 0 || bar == 0 || bar->instrument_id != bb->cfg.instrument_id ||
        bar->timeframe_sec != bb->cfg.timeframe_sec) {
        return false;
    }
    if (bb->has_open) {
        return false; /* OPEN 봉이 있는 상태에서의 과거 주입은 허용하지 않는다 */
    }
    tr_time_us_t session_open, session_close;
    if (!tr_session_span(&bb->cfg.session, bar->open_time_us, &session_open, &session_close)) {
        bb->n_out_of_session++;
        return false;
    }
    if (bb->bars.count > 0) {
        tr_candle_t last;
        tr_ring_at(&bb->bars, 0, &last);
        if (bar->open_time_us <= last.open_time_us) {
            bb->n_late_dropped++; /* 역순·중복 주입은 버리고 카운트 */
            return false;
        }
    }
    tr_candle_t c = *bar;
    c.state = TR_CANDLE_CLOSED;
    tr_ring_push(&bb->bars, &c);
    bb->last_close = c.close;
    bb->has_last_close = true;
    emit(bb, TR_EVENT_CANDLE_CLOSED, event_time != 0 ? event_time : c.close_time_us, &c);
    return true;
}

bool tr_bar_builder_at(const tr_bar_builder_t *bb, size_t back_index, tr_candle_t *out) {
    if (bb == 0 || out == 0) {
        return false;
    }
    return tr_ring_at(&bb->bars, back_index, out);
}
