/* 상위 봉 집계 테스트 (계획서 §26: 정렬·부분 봉·확정 시점, 야간·날짜 변경, 미래 정보 차단) */

#include "test_util.h"

#include <string.h>

#include "core/market/bar_aggregator.h"
#include "core/model/civil_time.h"

#define KST 540

/* 개장 09:01 KST. 5분봉이 UTC 나머지가 아니라 세션 개장에 정렬됨을 보이기 위한 의도적 비정렬. */
static const tr_session_policy_t SESS = {KST, 541, 930, TR_SESSION_WEEKDAYS};
/* 야간 세션: 18:00–익일 05:00 */
static const tr_session_policy_t NIGHT = {KST, 1080, 300, TR_SESSION_WEEKDAYS};

typedef struct {
    tr_candle_t closed[32];
    size_t n_closed;
    size_t n_updates;
} collect_t;

static void on_evt(void *ctx, const tr_event_envelope_t *env, const tr_candle_t *bar) {
    collect_t *c = (collect_t *)ctx;
    if (env->kind == TR_EVENT_CANDLE_CLOSED) {
        TR_CHECK(c->n_closed < 32);
        c->closed[c->n_closed++] = *bar;
    } else {
        c->n_updates++;
    }
}

static tr_time_us_t kst(unsigned h, unsigned mi) {
    tr_civil_t c = {2024, 1, 2, h, mi, 0}; /* 화요일 */
    tr_time_us_t t = 0;
    tr_time_us_from_civil(&c, KST, &t);
    return t;
}

static tr_time_us_t kst_day(unsigned d, unsigned h, unsigned mi) {
    tr_civil_t c = {2024, 1, d, h, mi, 0};
    tr_time_us_t t = 0;
    tr_time_us_from_civil(&c, KST, &t);
    return t;
}

static void make_lower(tr_candle_t *bar, tr_time_us_t open, tr_price_t o, tr_price_t h,
                       tr_price_t l, tr_price_t cl, tr_qty_t v, tr_candle_state_t state) {
    memset(bar, 0, sizeof(*bar));
    bar->instrument_id = 1;
    bar->timeframe_sec = 60;
    bar->open_time_us = open;
    bar->close_time_us = open + 60 * TR_US_PER_SEC;
    bar->open = o;
    bar->high = h;
    bar->low = l;
    bar->close = cl;
    bar->volume = v;
    bar->state = state;
}

static void feed_lower(tr_bar_aggregator_t *agg, tr_time_us_t open, tr_price_t o, tr_price_t h,
                       tr_price_t l, tr_price_t cl, tr_qty_t v) {
    tr_candle_t bar;
    make_lower(&bar, open, o, h, l, cl, v, TR_CANDLE_CLOSED);
    tr_event_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.kind = TR_EVENT_CANDLE_CLOSED;
    env.event_time_us = bar.close_time_us;
    TR_CHECK(tr_bar_aggregator_on_lower_closed(agg, &env, &bar));
}

static void init_agg(tr_bar_aggregator_t *agg, collect_t *c, uint32_t higher, tr_session_policy_t s) {
    tr_bar_aggregator_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.instrument_id = 1;
    cfg.lower_tf_sec = 60;
    cfg.higher_tf_sec = higher;
    cfg.session = s;
    cfg.engine_instance_id = 1;
    cfg.source_id = 7;
    cfg.on_event = on_evt;
    cfg.on_event_ctx = c;
    memset(c, 0, sizeof(*c));
    TR_CHECK(tr_bar_aggregator_init(agg, &cfg));
}

static void test_session_alignment_not_utc(void) {
    tr_bar_aggregator_t agg;
    collect_t c;
    init_agg(&agg, &c, 300, SESS); /* 1분 → 5분 */

    feed_lower(&agg, kst(9, 1), 100, 110, 100, 105, 10); /* 첫 1분봉: 세션 첫 봉 */

    const tr_candle_t *cur = tr_bar_aggregator_current(&agg);
    TR_CHECK(cur != 0 && cur->state == TR_CANDLE_OPEN);
    /* 상위 봉 시작은 세션 개장 09:01이지, UTC 5분 나머지(09:00/09:05)가 아니다 */
    TR_CHECK(cur->open_time_us == kst(9, 1));
    TR_CHECK(cur->close_time_us == kst(9, 6));
    TR_CHECK(cur->timeframe_sec == 300);
}

static void test_merge_and_close(void) {
    tr_bar_aggregator_t agg;
    collect_t c;
    init_agg(&agg, &c, 300, SESS);

    feed_lower(&agg, kst(9, 1), 100, 110, 100, 105, 10);
    feed_lower(&agg, kst(9, 2), 105, 115, 103, 112, 20);
    feed_lower(&agg, kst(9, 3), 112, 112, 90, 95, 30);

    /* 아직 5분 구간 안: 확정된 상위 봉 없음 (미래 정보 없이 진행 봉만 존재) */
    TR_CHECK(c.n_closed == 0);
    const tr_candle_t *cur = tr_bar_aggregator_current(&agg);
    TR_CHECK(cur != 0 && cur->state == TR_CANDLE_OPEN);
    TR_CHECK(cur->open == 100 && cur->high == 115 && cur->low == 90 && cur->close == 95 && cur->volume == 60);

    feed_lower(&agg, kst(9, 4), 95, 97, 94, 96, 5);
    feed_lower(&agg, kst(9, 5), 96, 99, 95, 98, 6); /* 같은 5분 구간의 마지막 봉 */
    TR_CHECK(c.n_closed == 0); /* 구간이 끝나도 다음 구간 봉이 와야 확정된다 */

    feed_lower(&agg, kst(9, 6), 98, 100, 97, 99, 7); /* 다음 구간 첫 봉 → 이전 5분봉 확정 */
    TR_CHECK(c.n_closed == 1);
    TR_CHECK(c.closed[0].state == TR_CANDLE_CLOSED);
    TR_CHECK(c.closed[0].open_time_us == kst(9, 1) && c.closed[0].close_time_us == kst(9, 6));
    TR_CHECK(c.closed[0].open == 100 && c.closed[0].high == 115);
    TR_CHECK(c.closed[0].low == 90 && c.closed[0].close == 98 && c.closed[0].volume == 71);

    cur = tr_bar_aggregator_current(&agg);
    TR_CHECK(cur != 0 && cur->open_time_us == kst(9, 6));
    TR_CHECK(cur->open == 98 && cur->volume == 7);
}

static void test_rejects_unclosed_lower(void) {
    tr_bar_aggregator_t agg;
    collect_t c;
    init_agg(&agg, &c, 300, SESS);

    tr_candle_t bar;
    make_lower(&bar, kst(9, 1), 100, 100, 100, 100, 1, TR_CANDLE_OPEN); /* 미확정 하위 봉 */
    tr_event_envelope_t env;
    memset(&env, 0, sizeof(env));
    TR_CHECK(!tr_bar_aggregator_on_lower_closed(&agg, &env, &bar));
    TR_CHECK(tr_bar_aggregator_current(&agg) == 0); /* 미확정 입력이 새지 않음 */
}

static void test_day_bar_overnight(void) {
    tr_bar_aggregator_t agg;
    collect_t c;
    init_agg(&agg, &c, TR_TF_DAY, NIGHT);

    /* 화요일 18:00 개장 세션: 화 23:59와 수 00:01은 같은 트레이딩 데이 */
    feed_lower(&agg, kst_day(2, 23, 59), 100, 101, 99, 100, 10);
    feed_lower(&agg, kst_day(3, 0, 1), 100, 105, 98, 104, 20);
    feed_lower(&agg, kst_day(3, 4, 59), 104, 106, 102, 103, 30);

    TR_CHECK(c.n_closed == 0); /* 자정을 넘겨도 일봉은 갈라지지 않음 */
    const tr_candle_t *cur = tr_bar_aggregator_current(&agg);
    TR_CHECK(cur != 0 && cur->state == TR_CANDLE_OPEN);
    TR_CHECK(cur->open_time_us == kst_day(2, 18, 0));
    TR_CHECK(cur->close_time_us == kst_day(3, 5, 0));
    TR_CHECK(cur->open == 100 && cur->high == 106 && cur->low == 98 && cur->close == 103 && cur->volume == 60);

    /* 다음 트레이딩 데이(수요일 18:00 개장) 첫 봉 → 이전 일봉 확정 */
    feed_lower(&agg, kst_day(3, 18, 0), 200, 201, 199, 200, 5);
    TR_CHECK(c.n_closed == 1);
    TR_CHECK(c.closed[0].open_time_us == kst_day(2, 18, 0));
    TR_CHECK(c.closed[0].volume == 60);
    cur = tr_bar_aggregator_current(&agg);
    TR_CHECK(cur != 0 && cur->open_time_us == kst_day(3, 18, 0));
    TR_CHECK(cur->open == 200);
}

static void test_invalid_config(void) {
    tr_bar_aggregator_t agg;
    collect_t c;
    tr_bar_aggregator_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.instrument_id = 1;
    cfg.lower_tf_sec = 60;
    cfg.session = SESS;
    cfg.on_event = on_evt;
    cfg.on_event_ctx = &c;

    cfg.higher_tf_sec = 60;  /* 상위 <= 하위 */
    TR_CHECK(!tr_bar_aggregator_init(&agg, &cfg));
    cfg.higher_tf_sec = 90;  /* 배수 아님 */
    TR_CHECK(!tr_bar_aggregator_init(&agg, &cfg));
    cfg.higher_tf_sec = 300;
    TR_CHECK(tr_bar_aggregator_init(&agg, &cfg));
    cfg.higher_tf_sec = TR_TF_DAY;
    TR_CHECK(tr_bar_aggregator_init(&agg, &cfg));
}

int main(void) {
    test_session_alignment_not_utc();
    test_merge_and_close();
    test_rejects_unclosed_lower();
    test_day_bar_overnight();
    test_invalid_config();
    TR_TEST_SUMMARY();
}
