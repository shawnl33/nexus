/* 봉 빌더 테스트 (계획서 §26: 중복 틱, 경계 시각, 무거래·누락 구분, 늦은 입력, 세션 종료) */

#include "test_util.h"

#include <string.h>

#include "core/market/bar_builder.h"
#include "core/model/civil_time.h"

#define KST 540
#define CAP 16

static tr_candle_t storage[CAP];

static const tr_session_policy_t KRX = {KST, 540, 930, TR_SESSION_WEEKDAYS};

typedef struct {
    tr_candle_t closed[64];
    size_t n_closed;
    size_t n_updates;
} collect_t;

static void on_evt(void *ctx, const tr_event_envelope_t *env, const tr_candle_t *bar) {
    collect_t *c = (collect_t *)ctx;
    if (env->kind == TR_EVENT_CANDLE_CLOSED) {
        TR_CHECK(c->n_closed < 64);
        c->closed[c->n_closed++] = *bar;
    } else {
        c->n_updates++;
    }
}

static tr_time_us_t kst(unsigned h, unsigned mi, unsigned s) {
    tr_civil_t c = {2024, 1, 2, h, mi, s}; /* 화요일 */
    tr_time_us_t t = 0;
    tr_time_us_from_civil(&c, KST, &t);
    return t;
}

static void make_env(tr_event_envelope_t *env, tr_time_us_t t) {
    memset(env, 0, sizeof(*env));
    env->kind = TR_EVENT_TICK;
    env->event_time_us = t;
    env->received_time_us = t;
}

static tr_tick_t tick(tr_price_t p, tr_qty_t q, uint64_t exec_id) {
    tr_tick_t tk;
    memset(&tk, 0, sizeof(tk));
    tk.instrument_id = 1;
    tk.price = p;
    tk.qty = q;
    tk.source_exec_id = exec_id;
    tk.volume_meaning = TR_TICK_VOLUME_PER_TRADE;
    return tk;
}

static void init_builder(tr_bar_builder_t *bb, collect_t *c, tr_no_trade_policy_t nt) {
    tr_bar_builder_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.instrument_id = 1;
    cfg.timeframe_sec = 60;
    cfg.session = KRX;
    cfg.no_trade = nt;
    cfg.ring_storage = storage;
    cfg.ring_capacity = CAP;
    cfg.engine_instance_id = 1;
    cfg.source_id = 7;
    cfg.on_event = on_evt;
    cfg.on_event_ctx = c;
    memset(c, 0, sizeof(*c));
    TR_CHECK(tr_bar_builder_init(bb, &cfg));
}

static void feed(tr_bar_builder_t *bb, tr_time_us_t t, tr_price_t p, tr_qty_t q, uint64_t id, tr_bb_status_t expect) {
    tr_event_envelope_t env;
    make_env(&env, t);
    tr_tick_t tk = tick(p, q, id);
    TR_CHECK(tr_bar_builder_on_tick(bb, &env, &tk) == expect);
}

static void test_basic_ohlcv_and_close(void) {
    tr_bar_builder_t bb;
    collect_t c;
    init_builder(&bb, &c, TR_NO_TRADE_SKIP);

    feed(&bb, kst(9, 0, 10), 100, 10, 1, TR_BB_ACCEPTED);
    feed(&bb, kst(9, 0, 20), 105, 5, 2, TR_BB_ACCEPTED);
    feed(&bb, kst(9, 0, 40), 95, 3, 3, TR_BB_ACCEPTED);

    const tr_candle_t *cur = tr_bar_builder_current(&bb);
    TR_CHECK(cur != 0 && cur->state == TR_CANDLE_OPEN);
    TR_CHECK(cur->open == 100 && cur->high == 105 && cur->low == 95 && cur->close == 95 && cur->volume == 18);

    /* 경계 시각 09:01:00 정각의 틱은 새 봉에 속한다 */
    feed(&bb, kst(9, 1, 0), 110, 2, 4, TR_BB_ACCEPTED_NEW_BAR);
    TR_CHECK(c.n_closed == 1);
    TR_CHECK(c.closed[0].state == TR_CANDLE_CLOSED);
    TR_CHECK(c.closed[0].open == 100 && c.closed[0].high == 105);
    TR_CHECK(c.closed[0].low == 95 && c.closed[0].close == 95 && c.closed[0].volume == 18);
    TR_CHECK(c.closed[0].open_time_us == kst(9, 0, 0) && c.closed[0].close_time_us == kst(9, 1, 0));

    cur = tr_bar_builder_current(&bb);
    TR_CHECK(cur != 0 && cur->open == 110 && cur->volume == 2);
}

static void test_duplicate_ticks(void) {
    tr_bar_builder_t bb;
    collect_t c;
    init_builder(&bb, &c, TR_NO_TRADE_SKIP);

    feed(&bb, kst(9, 0, 10), 100, 10, 5, TR_BB_ACCEPTED);
    feed(&bb, kst(9, 0, 10), 100, 10, 5, TR_BB_DUPLICATE);   /* 같은 exec_id 재수신 */
    feed(&bb, kst(9, 0, 15), 100, 10, 3, TR_BB_DUPLICATE);  /* 역순(재접속 재전송) */

    const tr_candle_t *cur = tr_bar_builder_current(&bb);
    TR_CHECK(cur != 0 && cur->volume == 10); /* 중복이 봉에 두 번 반영되지 않음 */
    TR_CHECK(bb.n_duplicates == 2);
}

static void test_late_correction(void) {
    tr_bar_builder_t bb;
    collect_t c;
    init_builder(&bb, &c, TR_NO_TRADE_SKIP);

    feed(&bb, kst(9, 0, 10), 100, 10, 1, TR_BB_ACCEPTED);
    feed(&bb, kst(9, 1, 0), 110, 2, 2, TR_BB_ACCEPTED_NEW_BAR); /* 09:00 봉 확정 */

    /* 확정된 09:00 봉에 늦은 체결 도착: 정정 처리 */
    feed(&bb, kst(9, 0, 30), 80, 4, 3, TR_BB_LATE_CORRECTED);
    tr_candle_t prev;
    TR_CHECK(tr_bar_builder_at(&bb, 1, &prev));
    TR_CHECK(prev.low == 80 && prev.close == 80 && prev.volume == 14);
    TR_CHECK(prev.revision == 1);
    TR_CHECK((prev.quality & TR_QUALITY_CORRECTED) != 0);
    TR_CHECK(bb.n_late_corrected == 1);

    /* 현재 봉은 영향 없음 */
    const tr_candle_t *cur = tr_bar_builder_current(&bb);
    TR_CHECK(cur != 0 && cur->open == 110 && cur->volume == 2);
}

static void test_late_dropped_when_out_of_buffer(void) {
    tr_bar_builder_t bb;
    collect_t c;
    init_builder(&bb, &c, TR_NO_TRADE_SKIP);

    feed(&bb, kst(9, 0, 0), 100, 1, 1, TR_BB_ACCEPTED);
    for (uint64_t i = 1; i <= CAP + 2; i++) {
        /* 매분 새 봉을 만들어 09:00 봉을 버퍼 밖으로 민다 */
        feed(&bb, kst(9, (unsigned)i, 0), 100, 1, i + 1, TR_BB_ACCEPTED_NEW_BAR);
    }
    feed(&bb, kst(9, 0, 30), 90, 1, CAP + 10, TR_BB_LATE_DROPPED);
    TR_CHECK(bb.n_late_dropped == 1);
}

static void test_fill_empty_bars(void) {
    tr_bar_builder_t bb;
    collect_t c;
    init_builder(&bb, &c, TR_NO_TRADE_FILL);

    feed(&bb, kst(9, 0, 10), 100, 10, 1, TR_BB_ACCEPTED);
    tr_bar_builder_on_timer(&bb, kst(9, 4, 0));

    /* 09:00 확정 + 09:01,09:02,09:03 채움 */
    TR_CHECK(c.n_closed == 4);
    TR_CHECK(bb.n_filled_empty == 3);
    for (size_t i = 1; i < 4; i++) {
        TR_CHECK((c.closed[i].quality & TR_QUALITY_FILLED_EMPTY) != 0);
        TR_CHECK(c.closed[i].volume == 0);
        TR_CHECK(c.closed[i].open == 100 && c.closed[i].close == 100);
        TR_CHECK(c.closed[i].state == TR_CANDLE_CLOSED);
    }
    /* 채움 봉은 수신 누락(GAP)이 아니다 */
    TR_CHECK((c.closed[1].quality & TR_QUALITY_GAP) == 0);

    /* 건어뛴 구간이 있어도 다음 틱이 오면 그 사이가 채워진다 */
    feed(&bb, kst(9, 7, 30), 120, 5, 2, TR_BB_ACCEPTED);
    /* 09:04,09:05,09:06 채움 + 09:07 새 봉 */
    TR_CHECK(bb.n_filled_empty == 6);
    const tr_candle_t *cur = tr_bar_builder_current(&bb);
    TR_CHECK(cur != 0 && cur->open == 120);
}

static void test_session_force_close(void) {
    tr_bar_builder_t bb;
    collect_t c;
    init_builder(&bb, &c, TR_NO_TRADE_SKIP);

    feed(&bb, kst(15, 29, 30), 100, 10, 1, TR_BB_ACCEPTED);
    tr_bar_builder_on_timer(&bb, kst(15, 30, 1)); /* 다음 체결이 없어도 폐장으로 확정 */
    TR_CHECK(c.n_closed == 1);
    TR_CHECK(c.closed[0].state == TR_CANDLE_CLOSED);
    TR_CHECK(tr_bar_builder_current(&bb) == 0);

    /* 폐장 이후 틱은 거절 */
    feed(&bb, kst(15, 30, 1), 101, 1, 2, TR_BB_REJECTED_OUT_OF_SESSION);
    TR_CHECK(bb.n_out_of_session == 1);
}

static void test_error_cases(void) {
    tr_bar_builder_t bb;
    collect_t c;
    init_builder(&bb, &c, TR_NO_TRADE_SKIP);

    tr_event_envelope_t env;
    make_env(&env, kst(9, 0, 0));
    tr_tick_t tk = tick(100, 1, 1);
    tk.instrument_id = 999; /* 종목 불일치 */
    TR_CHECK(tr_bar_builder_on_tick(&bb, &env, &tk) == TR_BB_ERROR);
    TR_CHECK(tr_bar_builder_on_tick(&bb, 0, &tk) == TR_BB_ERROR);
}

static void test_inject_bar(void) {
    tr_bar_builder_t bb;
    collect_t c;
    init_builder(&bb, &c, TR_NO_TRADE_SKIP);

    /* 실제 OHLC를 가진 과거 확정 봉을 주입한다 — 납작해지지 않아야 한다 */
    tr_candle_t b1;
    memset(&b1, 0, sizeof(b1));
    b1.instrument_id = 1;
    b1.timeframe_sec = 60;
    b1.open_time_us = kst(9, 0, 0);
    b1.close_time_us = kst(9, 1, 0);
    b1.open = 100;
    b1.high = 110;
    b1.low = 95;
    b1.close = 105;
    b1.volume = 42;
    TR_CHECK(tr_bar_builder_inject_bar(&bb, b1.close_time_us, &b1));

    tr_candle_t got;
    TR_CHECK(tr_ring_at(&bb.bars, 0, &got));
    TR_CHECK(got.open == 100 && got.high == 110 && got.low == 95 && got.close == 105);
    TR_CHECK(got.state == TR_CANDLE_CLOSED);
    TR_CHECK(c.n_closed == 1); /* 확정 이벤트가 발행되어 지표가 워밍업된다 */

    /* 다음 봉 주입, 역순·중복은 거부 */
    tr_candle_t b2 = b1;
    b2.open_time_us = kst(9, 1, 0);
    b2.close_time_us = kst(9, 2, 0);
    TR_CHECK(tr_bar_builder_inject_bar(&bb, b2.close_time_us, &b2));
    TR_CHECK(!tr_bar_builder_inject_bar(&bb, b1.close_time_us, &b1)); /* 중복 */
    TR_CHECK(tr_ring_count(&bb.bars) == 2);

    /* 세션 밖 시각은 거부 */
    tr_candle_t b3 = b1;
    b3.open_time_us = kst(16, 0, 0);
    b3.close_time_us = kst(16, 1, 0);
    TR_CHECK(!tr_bar_builder_inject_bar(&bb, b3.close_time_us, &b3));

    /* 주입 후 라이브 틱이 정상으로 새 봉을 연다 */
    tr_event_envelope_t env;
    make_env(&env, kst(9, 2, 10));
    tr_tick_t tk = tick(108, 3, 1);
    TR_CHECK(tr_bar_builder_on_tick(&bb, &env, &tk) == TR_BB_ACCEPTED);
    TR_CHECK(bb.has_open);
    const tr_candle_t *cur = tr_bar_builder_current(&bb);
    TR_CHECK(cur != 0 && cur->open == 108 && cur->open_time_us == kst(9, 2, 0));
    /* OPEN 상태에서는 주입 거부 */
    TR_CHECK(!tr_bar_builder_inject_bar(&bb, b1.close_time_us, &b1));
}

int main(void) {
    test_basic_ohlcv_and_close();
    test_duplicate_ticks();
    test_late_correction();
    test_late_dropped_when_out_of_buffer();
    test_fill_empty_bars();
    test_session_force_close();
    test_inject_bar();
    test_error_cases();
    TR_TEST_SUMMARY();
}
