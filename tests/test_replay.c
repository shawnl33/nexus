/* 과거 입력 재생 테스트 (계획서 §26: 동일 입력 재생) */

#include "test_util.h"

#include <string.h>

#include "adapters/history/replay.h"
#include "core/model/civil_time.h"

#define KST 540
#define CAP 32
#define N_EVENTS 12

static tr_candle_t storage_a[CAP];
static tr_candle_t storage_b[CAP];

static const tr_session_policy_t KRX = {KST, 540, 930, TR_SESSION_WEEKDAYS};

typedef struct {
    tr_candle_t closed[128];
    size_t n_closed;
} collect_t;

static void on_evt(void *ctx, const tr_event_envelope_t *env, const tr_candle_t *bar) {
    collect_t *c = (collect_t *)ctx;
    if (env->kind == TR_EVENT_CANDLE_CLOSED) {
        TR_CHECK(c->n_closed < 128);
        c->closed[c->n_closed++] = *bar;
    }
}

static tr_time_us_t kst(unsigned h, unsigned mi, unsigned s) {
    tr_civil_t c = {2024, 1, 2, h, mi, s};
    tr_time_us_t t = 0;
    tr_time_us_from_civil(&c, KST, &t);
    return t;
}

/* 재생 입력을 만든다. 중복 틱·무거래 구간을 포함한다. */
static size_t make_events(tr_replay_tick_t *ev) {
    static const struct {
        unsigned h, mi, s;
        tr_price_t p;
        tr_qty_t q;
        uint64_t id;
    } raw[N_EVENTS] = {
        {9, 0, 10, 100, 10, 1},
        {9, 0, 20, 105, 5, 2},
        {9, 0, 20, 105, 5, 2},  /* 재접속 중복 */
        {9, 0, 55, 103, 2, 3},
        {9, 1, 0, 110, 1, 4},
        {9, 1, 30, 112, 3, 5},
        {9, 5, 0, 120, 7, 6},   /* 무거래 구간 건어뜀 */
        {9, 5, 30, 118, 4, 7},
        {9, 6, 10, 119, 2, 8},
        {15, 29, 0, 130, 10, 9},
        {15, 29, 30, 131, 10, 10},
        {15, 30, 1, 132, 10, 11} /* 세션 밖 → 거절 예정 */
    };
    for (size_t i = 0; i < N_EVENTS; i++) {
        memset(&ev[i], 0, sizeof(ev[i]));
        ev[i].env.kind = TR_EVENT_TICK;
        ev[i].env.event_time_us = kst(raw[i].h, raw[i].mi, raw[i].s);
        ev[i].env.received_time_us = ev[i].env.event_time_us;
        ev[i].tick.instrument_id = 1;
        ev[i].tick.price = raw[i].p;
        ev[i].tick.qty = raw[i].q;
        ev[i].tick.source_exec_id = raw[i].id;
        ev[i].tick.volume_meaning = TR_TICK_VOLUME_PER_TRADE;
    }
    return N_EVENTS;
}

static void init_builder(tr_bar_builder_t *bb, collect_t *c, tr_candle_t *storage) {
    tr_bar_builder_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.instrument_id = 1;
    cfg.timeframe_sec = 60;
    cfg.session = KRX;
    cfg.no_trade = TR_NO_TRADE_SKIP;
    cfg.ring_storage = storage;
    cfg.ring_capacity = CAP;
    cfg.engine_instance_id = 1;
    cfg.source_id = 7;
    cfg.on_event = on_evt;
    cfg.on_event_ctx = c;
    memset(c, 0, sizeof(*c));
    TR_CHECK(tr_bar_builder_init(bb, &cfg));
}

static bool bars_equal(const tr_candle_t *a, const tr_candle_t *b) {
    return a->open_time_us == b->open_time_us && a->close_time_us == b->close_time_us &&
           a->open == b->open && a->high == b->high && a->low == b->low && a->close == b->close &&
           a->volume == b->volume && a->state == b->state && a->quality == b->quality &&
           a->revision == b->revision;
}

static void test_same_input_same_output(void) {
    tr_replay_tick_t ev[N_EVENTS];
    size_t n = make_events(ev);

    tr_bar_builder_t bb_a, bb_b;
    collect_t ca, cb;
    init_builder(&bb_a, &ca, storage_a);
    init_builder(&bb_b, &cb, storage_b);

    tr_replay_result_t ra, rb;
    TR_CHECK(tr_replay_run(&bb_a, ev, n, kst(15, 31, 0), &ra));
    TR_CHECK(tr_replay_run(&bb_b, ev, n, kst(15, 31, 0), &rb));

    TR_CHECK(ra.fed == rb.fed);
    TR_CHECK(ra.rejected == rb.rejected);
    TR_CHECK(ra.rejected == 1); /* 세션 밖 틱 1건 */

    TR_CHECK(ca.n_closed == cb.n_closed);
    TR_CHECK(ca.n_closed == 5); /* 09:00, 09:01, 09:05, 09:06, 15:29 (무거래 구간은 SKIP) */
    for (size_t i = 0; i < ca.n_closed; i++) {
        TR_CHECK(bars_equal(&ca.closed[i], &cb.closed[i]));
    }

    /* 내용 검증: 09:00 봉은 중복이 제거되어야 한다 */
    TR_CHECK(ca.closed[0].open == 100 && ca.closed[0].high == 105);
    TR_CHECK(ca.closed[0].low == 100 && ca.closed[0].close == 103 && ca.closed[0].volume == 17);
    /* 15:29 봉은 세션 폐장으로 확정된다 */
    TR_CHECK(ca.closed[ca.n_closed - 1].close_time_us == kst(15, 30, 0));
}

static void test_out_of_order_rejected(void) {
    tr_replay_tick_t ev[N_EVENTS];
    size_t n = make_events(ev);
    /* 두 이벤트를 바꿔 순서 역전을 만든다 */
    tr_replay_tick_t tmp = ev[0];
    ev[0] = ev[1];
    ev[1] = tmp;

    tr_bar_builder_t bb;
    collect_t c;
    init_builder(&bb, &c, storage_a);
    tr_replay_result_t r;
    TR_CHECK(!tr_replay_run(&bb, ev, n, kst(15, 31, 0), &r));
    TR_CHECK(r.first_bad_index == 1);
}

int main(void) {
    test_same_input_same_output();
    test_out_of_order_rejected();
    TR_TEST_SUMMARY();
}
