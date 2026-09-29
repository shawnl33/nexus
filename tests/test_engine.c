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

static void test_status_ring_alignment(void) {
    tr_engine_t e;
    capture_t cap;
    init_engine(&e, &cap);
    static tr_bar_status_t ring[BB_CAP];
    TR_CHECK(tr_engine_attach_status_ring(&e, ring, BB_CAP));

    /* 8개 봉, 봉마다 저·고 2틱 (갱신 포함) — 상태 링도 8개로 정합이어야 한다 */
    for (int i = 1; i <= 8; i++) {
        int mid = 98 + 2 * i;
        feed(&e, (unsigned)i, 0, mid - 2, (uint64_t)(i * 2 - 1));
        feed(&e, (unsigned)i, 30, mid + 2, (uint64_t)(i * 2));
    }
    TR_CHECK(tr_engine_status_count(&e) == 8);
    TR_CHECK(tr_ring_count(&e.bb.bars) == 8);

    /* open_time이 봉 링과 1:1로 정렬된다 */
    for (size_t i = 0; i < 8; i++) {
        tr_candle_t c;
        tr_bar_status_t st;
        TR_CHECK(tr_ring_at(&e.bb.bars, i, &c));
        TR_CHECK(tr_engine_status_at(&e, i, &st));
        TR_CHECK(st.open_time_us == c.open_time_us);
    }

    /* 회귀가 유효해진 이후 봉에는 값이 기록된다 (봉 8: 창 {106,108,110,112,114} 우측 끝 114) */
    tr_bar_status_t last;
    TR_CHECK(tr_engine_status_at(&e, 0, &last));
    TR_CHECK(last.reg_valid);
    TR_CHECK(last.reg_line == 114.0);

    /* SMA: 8봉으로 5일선만 창 완성 (최근 5봉 종가 108,110,112,114,116 → 112) */
    TR_CHECK(last.sma_valid == 0);
    TR_CHECK(last.sma[0] == 112.0);

    /* 미부착 엔진은 0/false */
    tr_engine_t e2;
    capture_t cap2;
    init_engine(&e2, &cap2);
    tr_bar_status_t st;
    TR_CHECK(tr_engine_status_count(&e2) == 0);
    TR_CHECK(!tr_engine_status_at(&e2, 0, &st));
}

/* ⑤ 매매 상태 체인 통합: 11개 세션을 공급해 운영최종유효 전이를 확인한다.
 * 세션 k 봉 i 중간값 = 1000 + 20k + 2i (저·고 ±2) — 세션 중간값은 1007+20k 완전 직선
 * (일봉 기울기 20, r2=1), 갭 = 2/19.8 ≈ 0.10 (일반장), 분봉 회귀는 세션 5봉째부터 유효 */
typedef struct {
    char last[1792]; /* 엔진 페이로드 버퍼와 같은 크기 */
    int n;
} last_capture_t;

static void last_capture_cb(void *ctx, const char *stream_id, uint64_t seq, const char *payload) {
    last_capture_t *c = (last_capture_t *)ctx;
    snprintf(c->last, sizeof(c->last), "%s", payload);
    c->n++;
    (void)stream_id;
    (void)seq;
}

static void feed_day(tr_engine_t *e, int day, unsigned mi, unsigned s, tr_price_t price, uint64_t id) {
    tr_event_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.kind = TR_EVENT_TICK;
    tr_civil_t c = {2024, 1, (unsigned)(2 + day), 9, mi, s};
    tr_time_us_from_civil(&c, KST, &env.event_time_us);
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

static void feed_session_bars(tr_engine_t *e, int k, int from_bar, int to_bar, uint64_t *id) {
    for (int i = from_bar; i <= to_bar; i++) {
        tr_price_t mid = 1000 + 20 * k + 2 * i;
        feed_day(e, k, (unsigned)i, 0, mid - 2, (*id)++);
        feed_day(e, k, (unsigned)i, 30, mid + 2, (*id)++);
    }
}

static void test_daily_chain(void) {
    tr_engine_t e;
    last_capture_t cap;
    memset(&cap, 0, sizeof(cap));
    static tr_bar_status_t ring[BB_CAP];

    tr_engine_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.engine_instance_id = 1;
    cfg.instrument_id = 1;
    cfg.session = (tr_session_policy_t){KST, 540, 930, TR_SESSION_EVERYDAY};
    cfg.timeframe_sec = 60;
    cfg.no_trade = TR_NO_TRADE_SKIP;
    cfg.predict_bars[0] = 5;
    cfg.predict_bars[1] = 10;
    cfg.predict_bars[2] = 15;
    cfg.htf_ticks = 10;
    cfg.min_r2 = 0.40;
    cfg.market_period = 20;
    cfg.is_futures = true;
    cfg.daily_reg_period = 10;
    cfg.gap_mid = 0.35;
    cfg.gap_big = 0.75;
    cfg.big_gap_reeval_min = 30;
    cfg.min_final_strength = 40;
    TR_CHECK(tr_engine_init(&e, &cfg, g_bb_storage, BB_CAP, g_score_mid, 32));
    tr_engine_attach_status_cb(&e, last_capture_cb, &cap);
    TR_CHECK(tr_engine_attach_status_ring(&e, ring, BB_CAP));

    /* 10개 세션: 완성 일봉·TR이 10개 미만이라 체인은 무효를 유지한다 */
    uint64_t id = 1;
    for (int k = 0; k < 10; k++) {
        feed_session_bars(&e, k, 0, 7, &id);
    }
    TR_CHECK(strstr(cap.last, "\"final\":[0,0,0,0]") != 0);
    TR_CHECK(!e.gap1.valid && !e.dalign.final_valid);

    /* 11번째 세션 진입: 첫 봉에서 10개 완성 일봉·TR이 확정된다 */
    feed_session_bars(&e, 10, 0, 3, &id);
    TR_CHECK(e.gap1.valid);
    TR_CHECK(e.gap1.gap_grade == 0);
    TR_CHECK(e.dtl1.link_valid);
    TR_CHECK(e.dtl1.trend.valid && e.dtl1.trend.dir == 1);
    TR_CHECK(strstr(cap.last, "\"final\":[0,0,0,0]") != 0); /* 분봉 회귀 워밍업(5봉) 전 */

    /* 5봉째부터 분봉 회귀 유효 → 일봉 상승 추세와 3/3 합의 → 운영최종유효 1 */
    feed_session_bars(&e, 10, 4, 7, &id);
    TR_CHECK(e.dalign.final_valid);
    TR_CHECK(e.dalign.final_dir == 1);
    TR_CHECK(e.dalign.final_state == 2);
    TR_CHECK(strstr(cap.last, "\"final\":[1,1,2,100]") != 0);
    TR_CHECK(strstr(cap.last, "\"reg_flat\":1215") != 0); /* 회귀선 틱 반올림 (선물 1틱=5 raw) */
    TR_CHECK(e.bar_index == 88); /* 11세션 × 8봉, 봉당 1회씩만 진행 */

    /* 상태 링: 최신 봉은 유효, 링에 남은 가장 오래된 봉(세션 3 첫 봉)은 무효 */
    tr_bar_status_t st;
    TR_CHECK(tr_engine_status_at(&e, 0, &st));
    TR_CHECK(st.final_valid == 1 && st.final_dir == 1 && st.final_state == 2 &&
             st.final_strength == 100);
    TR_CHECK(tr_engine_status_at(&e, 63, &st));
    TR_CHECK(st.final_valid == 0);
}

/* SMA 페이로드: 워밍업 [0,0,0,0] → 5일선만 완성 → 61봉에서 5/20/60 모두 유효.
 * 진행 봉 덮어쓰기 계약 덕에 봉당 1슬롯만 쌓이는 것을 값으로 증명한다. */
static void feed_min1(tr_engine_t *e, int min_of_day, tr_price_t price, uint64_t id) {
    tr_event_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.kind = TR_EVENT_TICK;
    tr_civil_t c = {2024, 1, 2, (unsigned)(9 + min_of_day / 60), (unsigned)(min_of_day % 60), 0};
    tr_time_us_from_civil(&c, KST, &env.event_time_us);
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

static void test_sma_payload(void) {
    tr_engine_t e;
    capture_t dummy;
    last_capture_t cap;
    memset(&cap, 0, sizeof(cap));
    init_engine(&e, &dummy);
    tr_engine_attach_status_cb(&e, last_capture_cb, &cap);

    /* 봉 i(9시+i분) 종가 = 100+i */
    uint64_t id = 1;
    for (int i = 1; i <= 4; i++) {
        feed_min1(&e, i, 100 + i, id++);
    }
    TR_CHECK(strstr(cap.last, "\"sma\":[0,0,0,0]") != 0); /* 워밍업: 5봉 미만 */

    feed_min1(&e, 5, 105, id++);
    TR_CHECK(strstr(cap.last, "\"sma\":[0,103,0,0]") != 0); /* SMA5만 창 완성 (101..105) */

    for (int i = 6; i <= 61; i++) {
        feed_min1(&e, i, 100 + i, id++);
    }
    /* 최근 종가 5개 157..161 → 159, 20개 142..161 → 151.5, 60개 102..161 → 131.5 */
    TR_CHECK(strstr(cap.last, "\"sma\":[1,159,151.5,131.5]") != 0);
}

/* 다중 파이프라인 슬롯 재사용: 중간 파이프라인을 제거한 뒤 추가해도 살아있는
 * 파이프라인의 저장소를 덮어쓰지 않아야 한다 (swap-remove 슬롯 별칭 회귀) */
static void feed_pipe(tr_engine_t *e, uint64_t instrument_id, unsigned mi, unsigned s,
                      tr_price_t price, uint64_t id) {
    tr_event_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.kind = TR_EVENT_TICK;
    env.event_time_us = kst(9, mi, s);
    env.received_time_us = env.event_time_us;
    tr_tick_t tk;
    memset(&tk, 0, sizeof(tk));
    tk.instrument_id = instrument_id;
    tk.price = price;
    tk.qty = 10;
    tk.source_exec_id = id;
    tk.volume_meaning = TR_TICK_VOLUME_PER_TRADE;
    TR_CHECK(tr_engine_on_tick(e, &env, &tk) != TR_BB_ERROR);
}

static void test_pipe_slot_reuse(void) {
    tr_engine_t e;
    capture_t cap;
    init_engine(&e, &cap);
    static tr_candle_t bb_a[BB_CAP], bb_b[BB_CAP], bb_c[BB_CAP], bb_d[BB_CAP];
    static double mid_a[32], mid_b[32], mid_c[32], mid_d[32];
    tr_pipeline_t *pa = tr_engine_pipe_add(&e, 100, true, bb_a, BB_CAP, mid_a, 32);
    tr_pipeline_t *pb = tr_engine_pipe_add(&e, 200, true, bb_b, BB_CAP, mid_b, 32);
    tr_pipeline_t *pc = tr_engine_pipe_add(&e, 300, true, bb_c, BB_CAP, mid_c, 32);
    TR_CHECK(pa != 0 && pb != 0 && pc != 0);
    TR_CHECK(e.pipe_count == 4);
    /* 저장소 인자 검증: NULL·용량 부족은 거부 */
    TR_CHECK(tr_engine_pipe_add(&e, 500, true, 0, BB_CAP, mid_b, 32) == 0);
    TR_CHECK(tr_engine_pipe_add(&e, 500, true, bb_b, BB_CAP, mid_b, 4) == 0);
    TR_CHECK(e.pipe_count == 4);

    /* C에 먼저 상태를 쌓아 둔다 (OPEN 봉 1개) */
    feed_pipe(&e, 300, 0, 0, 1000, 1);
    TR_CHECK(tr_ring_count(&pc->bb.bars) == 1);

    /* B를 제거하고 D를 추가해도 C의 저장소·포인터는 그대로여야 한다 */
    TR_CHECK(tr_engine_pipe_remove(&e, 200));
    TR_CHECK(e.pipe_count == 3);
    TR_CHECK(tr_engine_pipe_find(&e, 200) == 0);
    tr_pipeline_t *pd = tr_engine_pipe_add(&e, 400, true, bb_d, BB_CAP, mid_d, 32);
    TR_CHECK(pd != 0);
    TR_CHECK(e.pipe_count == 4);
    /* D는 독립 저장소: 어느 활성 파이프라인과도 주소가 다르다 (on_timer 이중 호출 방지) */
    TR_CHECK(pd != pa && pd != pc && pd != &e.pipe0);
    TR_CHECK(e.pipes[0] == &e.pipe0);
    TR_CHECK(e.pipes[1] == pa && e.pipes[2] == pc && e.pipes[3] == pd); /* 순서 보존 */
    TR_CHECK(e.pipes[2] != e.pipes[3]);

    /* C는 계속 정상 동작한다: 상태가 소멸하지 않았고 틱이 라우팅된다 */
    TR_CHECK(tr_engine_pipe_find(&e, 300) == pc);
    TR_CHECK(tr_ring_count(&pc->bb.bars) == 1);
    feed_pipe(&e, 300, 0, 30, 1002, 2);
    TR_CHECK(tr_ring_count(&pc->bb.bars) == 1); /* 같은 봉 갱신 */
    /* D도 독립적으로 동작한다 */
    feed_pipe(&e, 400, 1, 0, 2000, 3);
    TR_CHECK(tr_ring_count(&pd->bb.bars) == 1);
    TR_CHECK(tr_ring_count(&pc->bb.bars) == 1);
    TR_CHECK(tr_ring_count(&pa->bb.bars) == 0);

    /* 타이머는 각 파이프라인에 1회씩 전달된다: 세션 종료로 C·D의 봉이 각각 확정된다 */
    tr_engine_on_timer(&e, kst(15, 30, 1));
    TR_CHECK(!pc->bb.has_open);
    TR_CHECK(!pd->bb.has_open);
    TR_CHECK(tr_ring_count(&pc->bb.bars) == 1);
    TR_CHECK(tr_ring_count(&pd->bb.bars) == 1);
}

int main(void) {
    test_replay_pipeline();
    test_session_first_reset();
    test_orderbook_path();
    test_select_symbol_generation();
    test_status_ring_alignment();
    test_daily_chain();
    test_sma_payload();
    test_pipe_slot_reuse();
    TR_TEST_SUMMARY();
}
