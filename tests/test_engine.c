/* 엔진 런타임 통합 테스트: replay 경로 (틱→봉→지표→상태 발행) */

#include "test_util.h"

#include <string.h>

#include "runtime/engine.h"
#include "core/model/civil_time.h"

#define BB_CAP 64
static tr_candle_t g_bb_storage[BB_CAP];
static double g_score_mid[32];

#define KST 540

static tr_time_us_t kst(unsigned h, unsigned mi, unsigned s) {
    tr_civil_t c = {2024, 1, 2, h, mi, s};
    tr_time_us_t t = 0;
    tr_time_us_from_civil(&c, KST, &t);
    return t;
}

typedef struct {
    char payloads[64][768];
    int n;
} capture_t;

static void capture_cb(void *ctx, const char *stream_id, uint64_t seq, const char *payload) {
    capture_t *c = (capture_t *)ctx;
    TR_CHECK(c->n < 64);
    snprintf(c->payloads[c->n], sizeof(c->payloads[0]), "%.700s", payload);
    c->n++;
    (void)stream_id;
    (void)seq;
}

static void init_engine(tr_engine_t *e, capture_t *cap) {
    tr_engine_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.engine_instance_id = 1;
    cfg.instrument_id = 1;
    cfg.session = (tr_session_policy_t){KST, 540, 930, TR_SESSION_WEEKDAYS};
    cfg.timeframe_sec = 60;
    cfg.no_trade = TR_NO_TRADE_SKIP;
    cfg.predict_bars[0] = 5;
    cfg.predict_bars[1] = 10;
    cfg.predict_bars[2] = 15;
    cfg.htf_ticks = 10;
    cfg.min_r2 = 0.40;
    cfg.market_period = 20;
    cfg.is_futures = true; /* 호가 부호 규칙 테스트 기본값 (매수 우세 = 양수) */
    memset(cap, 0, sizeof(*cap));
    TR_CHECK(tr_engine_init(e, &cfg, g_bb_storage, BB_CAP, g_score_mid, 32));
    tr_engine_attach_status_cb(e, capture_cb, cap);
}

static void feed(tr_engine_t *e, unsigned mi, unsigned s, tr_price_t price, uint64_t id) {
    tr_event_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.kind = TR_EVENT_TICK;
    env.event_time_us = kst(9, mi, s);
    env.received_time_us = env.event_time_us;
    tr_tick_t tk;
    memset(&tk, 0, sizeof(tk));
    tk.instrument_id = e->cfg.instrument_id;
    tk.price = price;
    tk.qty = 10;
    tk.source_exec_id = id;
    tk.volume_meaning = TR_TICK_VOLUME_PER_TRADE;
    TR_CHECK(tr_engine_on_tick(e, &env, &tk) != TR_BB_ERROR);
}

static bool any_payload_contains(const capture_t *cap, const char *needle) {
    for (int i = 0; i < cap->n; i++) {
        if (strstr(cap->payloads[i], needle) != 0) {
            return true;
        }
    }
    return false;
}

static void test_replay_pipeline(void) {
    tr_engine_t e;
    capture_t cap;
    init_engine(&e, &cap);

    /* 8개 봉, 봉마다 저·고 2틱. 최종 중간값 = 98+2i (완전 직선), TR=4 */
    for (int i = 1; i <= 8; i++) {
        int mid = 98 + 2 * i;
        feed(&e, (unsigned)i, 0, mid - 2, (uint64_t)(i * 2 - 1));
        feed(&e, (unsigned)i, 30, mid + 2, (uint64_t)(i * 2));
    }

    /* 16틱 평가 + 경계 확정 7건 (봉 2~8 진입 시) */
    TR_CHECK(cap.n == 23);

    /* 초기(봉 4까지)에는 회귀 무효가 발행된다 */
    TR_CHECK(any_payload_contains(&cap, "\"reg_valid\":0"));

    /* 봉 5의 두 번째 평가: 표본 {100,102,104,106,108} 완전 직선 */
    TR_CHECK(any_payload_contains(&cap, "\"reg_valid\":1"));
    TR_CHECK(any_payload_contains(&cap, "\"reg_line\":108"));
    TR_CHECK(any_payload_contains(&cap, "\"reg_r2\":1"));
    /* ATR=4, adj_slope=2, pred1 = 108 + 2*5 = 118 */
    TR_CHECK(any_payload_contains(&cap, "\"pred\":[118"));
    TR_CHECK(any_payload_contains(&cap, "\"score\":"));
    TR_CHECK(any_payload_contains(&cap, "\"future_dir\":"));

    /* 마지막 봉 강제 확정: 세션 종료 시각 타이머 */
    tr_engine_on_timer(&e, kst(15, 30, 1));
    const char *last = cap.payloads[cap.n - 1];
    TR_CHECK(strstr(last, "\"closed\":1") != 0);
}

static void test_session_first_reset(void) {
    tr_engine_t e;
    capture_t cap;
    init_engine(&e, &cap);

    /* 당일 첫 봉(09:00): 점수의 미래방향은 0 → 기본 항 −2 */
    feed(&e, 0, 0, 100, 1);
    TR_CHECK(cap.n == 1);
    TR_CHECK(strstr(cap.payloads[0], "\"future_dir\":0") != 0);
    TR_CHECK(strstr(cap.payloads[0], "\"score\":") != 0);
}

static void test_orderbook_path(void) {
    tr_engine_t e;
    capture_t cap;
    init_engine(&e, &cap);

    /* 호가가 없는 초기에는 ob_valid=0 */
    feed(&e, 0, 0, 100, 1);
    TR_CHECK(any_payload_contains(&cap, "\"ob_valid\":0"));

    /* 매수 우세 호가(선물 부호 규칙): bids >> asks → 양수 점수가 ob_score에 반영된다 */
    for (int i = 0; i < 6; i++) {
        tr_engine_on_orderbook(&e, kst(9, 0, (unsigned)(10 + i)), 800000.0, 200000.0);
    }
    feed(&e, 0, 30, 101, 2);
    TR_CHECK(any_payload_contains(&cap, "\"ob_valid\":1"));
    TR_CHECK(any_payload_contains(&cap, "\"ob_dir\":1"));
}

static void test_select_symbol_generation(void) {
    tr_engine_t e;
    capture_t cap;
    init_engine(&e, &cap);
    TR_CHECK(e.generation == 1);

    for (int i = 1; i <= 6; i++) {
        feed(&e, (unsigned)i, 0, 100 + i, (uint64_t)i);
    }
    TR_CHECK(e.lr3.reg_valid); /* 워밍업 완료 상태 */

    /* 종목 전환: 상태가 리셋되고 generation이 오른다 */
    TR_CHECK(tr_engine_select_symbol(&e, 999, false));
    TR_CHECK(e.generation == 2);
    TR_CHECK(e.cfg.instrument_id == 999);
    TR_CHECK(!e.lr3.reg_valid); /* 지표는 새 종목 기준으로 다시 워밍업 */
    TR_CHECK(e.status_cb == capture_cb); /* 출력 연결은 보존 */
    TR_CHECK(tr_engine_select_symbol(&e, 1000, true));
    TR_CHECK(e.generation == 3);

    /* 전환 후에도 상태 발행이 계속된다 */
    feed(&e, 1, 0, 200, 100);
    TR_CHECK(any_payload_contains(&cap, "\"generation\":3"));
}

int main(void) {
    test_replay_pipeline();
    test_session_first_reset();
    test_orderbook_path();
    test_select_symbol_generation();
    TR_TEST_SUMMARY();
}
