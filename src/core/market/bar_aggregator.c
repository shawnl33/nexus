#include "core/market/bar_aggregator.h"

#include <string.h>

#include "core/model/civil_time.h"

bool tr_bar_aggregator_init(tr_bar_aggregator_t *agg, const tr_bar_aggregator_config_t *cfg) {
    if (agg == 0 || cfg == 0 || cfg->instrument_id == 0 || cfg->lower_tf_sec == 0) {
        return false;
    }
    if (cfg->higher_tf_sec != TR_TF_DAY) {
        if (cfg->higher_tf_sec == 0 || cfg->higher_tf_sec <= cfg->lower_tf_sec ||
            cfg->higher_tf_sec % cfg->lower_tf_sec != 0) {
            return false;
        }
    }
    if (!tr_session_policy_validate(&cfg->session)) {
        return false;
    }
    agg->cfg = *cfg;
    agg->has_open = false;
    memset(&agg->current, 0, sizeof(agg->current));
    agg->next_sequence = 1;
    agg->n_closed = 0;
    return true;
}

static void emit(tr_bar_aggregator_t *agg, tr_event_kind_t kind, tr_time_us_t event_time, const tr_candle_t *bar) {
    if (agg->cfg.on_event == 0) {
        return;
    }
    tr_event_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.kind = kind;
    env.schema_version = 1;
    env.engine_instance_id = agg->cfg.engine_instance_id;
    env.sequence = agg->next_sequence;
    env.source_id = agg->cfg.source_id;
    env.event_time_us = event_time;
    env.received_time_us = event_time;
    env.quality = bar->quality;
    agg->next_sequence++;
    agg->cfg.on_event(agg->cfg.on_event_ctx, &env, bar);
}

/* 하위 봉이 속한 상위 봉 구간 [open, close) 를 세션 개장 기준으로 구한다. */
static bool higher_interval(const tr_bar_aggregator_t *agg, const tr_candle_t *lower,
                            tr_time_us_t *out_open, tr_time_us_t *out_close) {
    tr_time_us_t session_open, session_close;
    if (!tr_session_span(&agg->cfg.session, lower->open_time_us, &session_open, &session_close)) {
        return false;
    }
    if (agg->cfg.higher_tf_sec == TR_TF_DAY) {
        *out_open = session_open;
        *out_close = session_close;
        return true;
    }
    int64_t tf_us = (int64_t)agg->cfg.higher_tf_sec * TR_US_PER_SEC;
    int64_t k = (lower->open_time_us - session_open) / tf_us;
    tr_time_us_t open = session_open + k * tf_us;
    tr_time_us_t close = open + tf_us;
    if (close > session_close) {
        close = session_close;
    }
    *out_open = open;
    *out_close = close;
    return true;
}

static void merge_lower(tr_candle_t *cur, const tr_candle_t *lower) {
    if (lower->high > cur->high) {
        cur->high = lower->high;
    }
    if (lower->low < cur->low) {
        cur->low = lower->low;
    }
    cur->close = lower->close;
    cur->volume += lower->volume;
    cur->quality |= lower->quality;
}

bool tr_bar_aggregator_on_lower_closed(tr_bar_aggregator_t *agg, const tr_event_envelope_t *env, const tr_candle_t *lower) {
    if (agg == 0 || env == 0 || lower == 0) {
        return false;
    }
    if (lower->state != TR_CANDLE_CLOSED || lower->instrument_id != agg->cfg.instrument_id ||
        lower->timeframe_sec != agg->cfg.lower_tf_sec) {
        return false;
    }
    tr_time_us_t open, close;
    if (!higher_interval(agg, lower, &open, &close)) {
        return false;
    }

    if (agg->has_open && agg->current.open_time_us == open) {
        merge_lower(&agg->current, lower);
        emit(agg, TR_EVENT_CANDLE_UPDATE, env->event_time_us, &agg->current);
        return true;
    }

    if (agg->has_open) {
        agg->current.state = TR_CANDLE_CLOSED;
        emit(agg, TR_EVENT_CANDLE_CLOSED, env->event_time_us, &agg->current);
        agg->n_closed++;
    }

    agg->current = *lower;
    agg->current.timeframe_sec = agg->cfg.higher_tf_sec;
    agg->current.open_time_us = open;
    agg->current.close_time_us = close;
    agg->current.state = TR_CANDLE_OPEN;
    agg->current.revision = 0;
    agg->has_open = true;
    emit(agg, TR_EVENT_CANDLE_UPDATE, env->event_time_us, &agg->current);
    return true;
}

const tr_candle_t *tr_bar_aggregator_current(const tr_bar_aggregator_t *agg) {
    return (agg != 0 && agg->has_open) ? &agg->current : 0;
}
