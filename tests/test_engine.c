/* 엔진 런타임 통합 테스트: replay 경로 (틱→봉→지표→상태 발행) */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "runtime/engine.h"
#include "core/model/civil_time.h"
#include "yyjson.h"

#define BB_CAP 64
static tr_candle_t g_bb_storage[BB_CAP];
static double g_score_mid[32];

#define KST 540

/* init_engine의 세션과 동일한 정책 (pipe_add/select_symbol의 세션 인자) */
static const tr_session_policy_t TEST_SESS = {KST, 540, 930, TR_SESSION_WEEKDAYS};

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
        tr_engine_on_orderbook(&e, 1, kst(9, 0, (unsigned)(10 + i)), 800000.0, 200000.0);
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
    TR_CHECK(tr_engine_select_symbol(&e, 999, false, "099999", &TEST_SESS));
    TR_CHECK(e.generation == 2);
    TR_CHECK(e.cfg.instrument_id == 999);
    TR_CHECK(strcmp(e.shcode, "099999") == 0); /* 전환 종목 코드가 파이프라인에 실린다 */
    TR_CHECK(!e.lr3.reg_valid); /* 지표는 새 종목 기준으로 다시 워밍업 */
    TR_CHECK(e.status_cb == capture_cb); /* 출력 연결은 보존 */
    TR_CHECK(tr_engine_select_symbol(&e, 1000, true, "1000F0", &TEST_SESS));
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

/* 늦은 틱 정정 회귀: 과거 봉 정정 이벤트가 이평선 창을 오염시키지 않아야 한다.
 * 정정 봉의 open_time은 prev_bar_open보다 과거이므로 is_new_bar=true로 보이지만,
 * SMA 갱신 게이트(⑤ 체인과 동일)가 창 push를 막는다 */
static void test_sma_late_correction(void) {
    tr_engine_t e;
    capture_t dummy;
    last_capture_t cap;
    memset(&cap, 0, sizeof(cap));
    init_engine(&e, &dummy);
    tr_engine_attach_status_cb(&e, last_capture_cb, &cap);

    /* 봉 i(9시+i분) 종가 = 100+i, 6봉 → SMA5 창 완성 (최근 5봉 102..106, 평균 104) */
    uint64_t id = 1;
    for (int i = 1; i <= 6; i++) {
        feed_min1(&e, i, 100 + i, id++);
    }
    TR_CHECK(e.sma5.valid && e.sma5.count == 5);
    TR_CHECK(fabs(e.sma5.value - 104.0) < 1e-9);
    TR_CHECK(e.sma20.count == 6 && e.sma60.count == 6);

    /* 늦은 틱: 확정된 봉 3(9:03)을 999로 정정한다. exec_id는 단조 증가를 유지해야
     * 중복 필터를 지나 정정 경로(TR_BB_LATE_CORRECTED)에 도달한다 */
    {
        tr_event_envelope_t env;
        memset(&env, 0, sizeof(env));
        env.kind = TR_EVENT_TICK;
        tr_civil_t c = {2024, 1, 2, 9, 3, 30};
        tr_time_us_from_civil(&c, KST, &env.event_time_us);
        env.received_time_us = env.event_time_us;
        tr_tick_t tk;
        memset(&tk, 0, sizeof(tk));
        tk.instrument_id = e.cfg.instrument_id;
        tk.price = 999;
        tk.qty = 10;
        tk.source_exec_id = id++;
        tk.volume_meaning = TR_TICK_VOLUME_PER_TRADE;
        TR_CHECK(tr_engine_on_tick(&e, &env, &tk) == TR_BB_LATE_CORRECTED);
    }

    /* 창이 오염하지 않는다: 개수·값 모두 정정 전 그대로 (오염 시 999가 push되어
     * SMA5 = (103+104+105+106+999)/5 = 283.4) */
    TR_CHECK(e.sma5.count == 5);
    TR_CHECK(fabs(e.sma5.value - 104.0) < 1e-9);
    TR_CHECK(e.sma20.count == 6 && e.sma60.count == 6);
    TR_CHECK(strstr(cap.last, "\"sma\":[0,104,0,0]") != 0); /* 정정 이벤트 발행에도 현재 창 유지 */

    /* 다음 새 봉은 정상 push: 창은 103..107로 밀려 평균 105 (999가 남아 있으면 실패) */
    feed_min1(&e, 7, 107, id++);
    TR_CHECK(e.sma5.count == 5);
    TR_CHECK(fabs(e.sma5.value - 105.0) < 1e-9);
    TR_CHECK(strstr(cap.last, "\"sma\":[0,105,0,0]") != 0);
}

/* chart.snapshot ind[i] 포맷 회귀: 32개 값과 문서화된 인덱스 매핑
 * (docs/display_payload.md §2)을 tr_bar_status_format_ind 출력으로 고정한다 */
static void test_ind_format(void) {
    tr_bar_status_t st;
    memset(&st, 0, sizeof(st));
    st.closed = true;
    st.reg_valid = true;
    st.reg_line = 1234.5;
    st.reg_r2 = 0.75;
    st.pred[0] = 1.0;
    st.pred[1] = 2.0;
    st.pred[2] = 3.0;
    st.score = -42;
    st.ob_valid = true;
    st.ob_score = 0.5;
    st.residual = 7.5;
    st.pvol = 2.5;
    st.pred_dir[0] = 1;
    st.pred_dir[1] = 0;
    st.pred_dir[2] = -1;
    st.mkt_valid = true;
    st.mkt_center = 100;
    st.mkt_u1 = 101;
    st.mkt_l1 = 99;
    st.mkt_u2 = 102;
    st.mkt_l2 = 98;
    st.final_valid = 1;
    st.final_dir = -1;
    st.final_state = 2;
    st.final_strength = 88;
    st.trading_day = 20000;
    st.sma_valid = 1;
    st.sma[0] = 10.5;
    st.sma[1] = 20.5;
    st.sma[2] = 60.5;

    char buf[1024];
    int n = tr_bar_status_format_ind(&st, 5.0, true, buf, sizeof(buf));
    TR_CHECK(n > 0 && (size_t)n < sizeof(buf));

    yyjson_doc *doc = yyjson_read(buf, (size_t)n, 0);
    TR_CHECK(doc != 0);
    if (doc != 0) {
        yyjson_val *a = yyjson_doc_get_root(doc);
        TR_CHECK(yyjson_is_arr(a));
        TR_CHECK(yyjson_arr_size(a) == 32); /* [0..31] 고정 레이아웃 */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 0)) == 1);        /* closed */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 1)) == 1);        /* reg_valid */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 2)) == 1234.5);   /* reg_line */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 3)) == 0.75);     /* reg_r2 */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 4)) == 1.0);      /* pred[0] */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 5)) == 2.0);      /* pred[1] */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 6)) == 3.0);      /* pred[2] */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 7)) == -42);      /* score */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 8)) == 1);        /* ob_valid */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 9)) == 0.5);      /* ob_score */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 10)) == 7.5);     /* resid */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 11)) == 2.5);     /* pvol */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 12)) == 1);       /* pred_dir[0] */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 13)) == 0);       /* pred_dir[1] */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 14)) == -1);      /* pred_dir[2] */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 15)) == 1);       /* mkt 유효 */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 16)) == 100.0);   /* mkt 중심 */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 17)) == 101.0);   /* mkt 상단1 */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 18)) == 99.0);    /* mkt 하단1 */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 19)) == 102.0);   /* mkt 상단2 */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 20)) == 98.0);    /* mkt 하단2 */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 21)) == 1);       /* final_valid */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 22)) == -1);      /* final_dir */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 23)) == 2);       /* final_state */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 24)) == 88);      /* final_strength */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 25)) == 1235.0);  /* reg_flat 틱(5) 반올림 */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 26)) == 5);       /* tick 크기 */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 27)) == 20000);   /* 거래일 */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 28)) == 1);       /* SMA 유효 */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 29)) == 10.5);    /* SMA 5 */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 30)) == 20.5);    /* SMA 20 */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 31)) == 60.5);    /* SMA 60 */
        yyjson_doc_free(doc);
    }

    /* first=false는 앞에 쉼표를 붙인다 (배열 내 연결) */
    n = tr_bar_status_format_ind(&st, 5.0, false, buf, sizeof(buf));
    TR_CHECK(n > 0 && buf[0] == ',');

    /* 엔진 상태 링 → ind 매핑: 6봉(종가 101..106)에서 최신 봉의 SMA5=104가 [29]에 온다.
     * 나머지 필드는 실제 지표 계산값이라 시나리오에 따라 달려 고정할 수 없으므로,
     * 여기서는 개수(32)와 대표 위치([28]/[29])만 고정한다 — 32개 전체 값 고정은 위의
     * 고정 픽스처 블록이 담당한다 */
    {
        tr_engine_t e;
        capture_t dummy;
        init_engine(&e, &dummy);
        static tr_bar_status_t ring[BB_CAP];
        TR_CHECK(tr_engine_attach_status_ring(&e, ring, BB_CAP));
        uint64_t id = 1;
        for (int i = 1; i <= 6; i++) {
            feed_min1(&e, i, 100 + i, id++);
        }
        tr_bar_status_t cur;
        TR_CHECK(tr_engine_status_at(&e, 0, &cur));
        n = tr_bar_status_format_ind(&cur, 5.0, true, buf, sizeof(buf));
        TR_CHECK(n > 0 && (size_t)n < sizeof(buf));
        doc = yyjson_read(buf, (size_t)n, 0);
        TR_CHECK(doc != 0);
        if (doc != 0) {
            yyjson_val *a = yyjson_doc_get_root(doc);
            TR_CHECK(yyjson_arr_size(a) == 32);
            TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 28)) == 0);     /* 5일선만 완성 → 종합 0 */
            TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 29)) == 104.0); /* 최근 5봉 종가 평균 */
            yyjson_doc_free(doc);
        }
    }
}

/* ⑥ 세션 경계와 mem_reset: 경계 봉(새 세션 첫 봉)의 상태 링 슬롯에만 mem_reset=1이
 * 남아야 한다. 봉마다 2틱을 넣어 같은 봉 재평가 덮어쓰기에서도 리셋 표시가
 * 유지되는지(링 기록부 래치)까지 검증한다 */
static void test_mem_reset_ring(void) {
    tr_engine_t e;
    capture_t cap;
    init_engine(&e, &cap);
    static tr_bar_status_t ring[BB_CAP];
    TR_CHECK(tr_engine_attach_status_ring(&e, ring, BB_CAP));

    /* 세션 0(1/2 화): 봉 0..7, 세션 1(1/3 수): 봉 0..3 */
    uint64_t id = 1;
    feed_session_bars(&e, 0, 0, 7, &id);
    feed_session_bars(&e, 1, 0, 3, &id);
    TR_CHECK(tr_engine_status_count(&e) == 12);

    tr_bar_status_t st;
    /* 경계 봉(세션 1 첫 봉, back=3): 리셋 표시가 남는다. 회귀가 세션마다 워밍업을
     * 다시 하므로 이 봉에는 재저장이 없어 유효=0 — 스냅샷 이벤트는 valid=0/reset=1 */
    TR_CHECK(tr_engine_status_at(&e, 3, &st));
    TR_CHECK(st.mem_reset);
    TR_CHECK(!st.mem_valid);
    /* 이웃 봉(경계 전후)과 최신 봉은 리셋이 아니다 */
    TR_CHECK(tr_engine_status_at(&e, 4, &st)); /* 세션 0 마지막 봉 */
    TR_CHECK(!st.mem_reset);
    TR_CHECK(st.mem_valid); /* 이전 세션의 세트는 유효한 채로 끝난다 */
    TR_CHECK(tr_engine_status_at(&e, 2, &st));
    TR_CHECK(!st.mem_reset);
    TR_CHECK(tr_engine_status_at(&e, 0, &st));
    TR_CHECK(!st.mem_reset);
    /* 엔진 첫 봉(back=11)도 0→첫 세션 진입 리셋이다 (원본 회귀기억세션(-1) 초기값과 동일) */
    TR_CHECK(tr_engine_status_at(&e, 11, &st));
    TR_CHECK(st.mem_reset);
}

/* mem_reset 래치의 과거 슬롯 정정 분기 미러: 세션 경계 봉이 최신 슬롯에서 밀려난 뒤
 * 같은 세션의 늦은 틱으로 정정되어도(재평가 시점에는 session_reset이 내려가 있음)
 * 링 슬롯의 리셋 표시가 유지되어야 한다. 비경계 봉 정정에는 래치가 새 표시를 만들지
 * 않는다 */
static void test_mem_reset_correction_latch(void) {
    tr_engine_t e;
    capture_t cap;
    init_engine(&e, &cap);
    static tr_bar_status_t ring[BB_CAP];
    TR_CHECK(tr_engine_attach_status_ring(&e, ring, BB_CAP));

    /* 세션 0(1/2 화): 봉 0..7, 세션 1(1/3 수): 봉 0..3 — 경계 봉은 back=3 */
    uint64_t id = 1;
    feed_session_bars(&e, 0, 0, 7, &id);
    feed_session_bars(&e, 1, 0, 3, &id);
    TR_CHECK(tr_engine_status_count(&e) == 12);

    tr_bar_status_t st;
    TR_CHECK(tr_engine_status_at(&e, 3, &st));
    TR_CHECK(st.mem_reset);

    /* 늦은 틱: 세션 1 경계 봉(9:00)을 정정한다. 재평가의 session_reset은 이미 내려가
     * 있으므로(세션 번호가 갱신됨) 래치가 없으면 이 덮어쓰기가 리셋 표시를 지운다 */
    {
        tr_event_envelope_t env;
        memset(&env, 0, sizeof(env));
        env.kind = TR_EVENT_TICK;
        tr_civil_t c = {2024, 1, 3, 9, 0, 45};
        tr_time_us_from_civil(&c, KST, &env.event_time_us);
        env.received_time_us = env.event_time_us;
        tr_tick_t tk;
        memset(&tk, 0, sizeof(tk));
        tk.instrument_id = e.cfg.instrument_id;
        tk.price = 1050;
        tk.qty = 10;
        tk.source_exec_id = id++;
        tk.volume_meaning = TR_TICK_VOLUME_PER_TRADE;
        TR_CHECK(tr_engine_on_tick(&e, &env, &tk) == TR_BB_LATE_CORRECTED);
    }

    /* 링 구조는 그대로(12개), 경계 봉 슬롯의 리셋 표시는 유지된다 */
    TR_CHECK(tr_engine_status_count(&e) == 12);
    TR_CHECK(tr_engine_status_at(&e, 3, &st));
    TR_CHECK(st.mem_reset);

    /* 비경계 봉(세션 1 봉 1, back=2) 정정: 래치가 없던 표시를 새로 만들지 않는다 */
    {
        tr_event_envelope_t env;
        memset(&env, 0, sizeof(env));
        env.kind = TR_EVENT_TICK;
        tr_civil_t c = {2024, 1, 3, 9, 1, 45};
        tr_time_us_from_civil(&c, KST, &env.event_time_us);
        env.received_time_us = env.event_time_us;
        tr_tick_t tk;
        memset(&tk, 0, sizeof(tk));
        tk.instrument_id = e.cfg.instrument_id;
        tk.price = 1051;
        tk.qty = 10;
        tk.source_exec_id = id++;
        tk.volume_meaning = TR_TICK_VOLUME_PER_TRADE;
        TR_CHECK(tr_engine_on_tick(&e, &env, &tk) == TR_BB_LATE_CORRECTED);
    }
    TR_CHECK(tr_engine_status_count(&e) == 12);
    TR_CHECK(tr_engine_status_at(&e, 2, &st));
    TR_CHECK(!st.mem_reset);
}

/* chart.snapshot mem[i] 이벤트 포맷 회귀: 17개 값과 끝의 reset 플래그
 * (docs/display_payload.md §2)을 tr_bar_status_format_mem 출력으로 고정한다 */
static void test_mem_event_format(void) {
    tr_bar_status_t st;
    memset(&st, 0, sizeof(st));
    char buf[512];

    /* updated도 reset도 아닌 봉은 이벤트가 아니다 (아무것도 쓰지 않고 0) */
    TR_CHECK(tr_bar_status_format_mem(&st, true, buf, sizeof(buf)) == 0);

    st.open_time_us = 1700000000000000LL;
    st.mem_valid = true;
    st.mem_updated = true;
    st.mem_dir = -1;
    st.mem_price = 34550;
    st.mem_target[0] = 34600; st.mem_target[1] = 34620; st.mem_target[2] = 34640;
    st.mem_upper[0] = 34700; st.mem_upper[1] = 34710; st.mem_upper[2] = 34720;
    st.mem_lower[0] = 34400; st.mem_lower[1] = 34390; st.mem_lower[2] = 34380;
    st.mem_show_targets = true;
    st.mem_show_upper = true;
    st.mem_show_lower = false;

    int n = tr_bar_status_format_mem(&st, true, buf, sizeof(buf));
    TR_CHECK(n > 0 && (size_t)n < sizeof(buf));
    yyjson_doc *doc = yyjson_read(buf, (size_t)n, 0);
    TR_CHECK(doc != 0);
    if (doc != 0) {
        yyjson_val *a = yyjson_doc_get_root(doc);
        TR_CHECK(yyjson_is_arr(a));
        TR_CHECK(yyjson_arr_size(a) == 17); /* [0..16] 고정 레이아웃 */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 1)) == 1);        /* valid */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 2)) == -1);       /* dir */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 3)) == 34550.0);  /* price */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 6)) == 34640.0);  /* t3 */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 9)) == 34720.0);  /* u3 */
        TR_CHECK(yyjson_get_num(yyjson_arr_get(a, 12)) == 34380.0); /* l3 */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 13)) == 1);       /* showT */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 14)) == 1);       /* showU */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 15)) == 0);       /* showL */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 16)) == 0);       /* reset 없음 */
        yyjson_doc_free(doc);
    }

    /* 세션 리셋 봉: 끝 필드가 reset=1 (리셋만 있고 재저장이 없으면 valid=0으로 실린다) */
    st.mem_updated = false;
    st.mem_reset = true;
    st.mem_valid = false;
    st.mem_dir = 0;
    n = tr_bar_status_format_mem(&st, false, buf, sizeof(buf));
    TR_CHECK(n > 0 && buf[0] == ','); /* 배열 연결 */
    doc = yyjson_read(buf + 1, (size_t)(n - 1), 0);
    TR_CHECK(doc != 0);
    if (doc != 0) {
        yyjson_val *a = yyjson_doc_get_root(doc);
        TR_CHECK(yyjson_arr_size(a) == 17);
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 1)) == 0);  /* valid */
        TR_CHECK(yyjson_get_int(yyjson_arr_get(a, 16)) == 1); /* reset */
        yyjson_doc_free(doc);
    }
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
    tr_pipeline_t *pa = tr_engine_pipe_add(&e, 100, true, "0100A0", &TEST_SESS, bb_a, BB_CAP, mid_a, 32);
    tr_pipeline_t *pb = tr_engine_pipe_add(&e, 200, true, "0200B0", &TEST_SESS, bb_b, BB_CAP, mid_b, 32);
    tr_pipeline_t *pc = tr_engine_pipe_add(&e, 300, true, "0300C0", &TEST_SESS, bb_c, BB_CAP, mid_c, 32);
    TR_CHECK(pa != 0 && pb != 0 && pc != 0);
    TR_CHECK(e.pipe_count == 4);
    /* 저장소·세션 인자 검증: NULL·용량 부족은 거부 */
    TR_CHECK(tr_engine_pipe_add(&e, 500, true, "0500E0", &TEST_SESS, 0, BB_CAP, mid_b, 32) == 0);
    TR_CHECK(tr_engine_pipe_add(&e, 500, true, "0500E0", &TEST_SESS, bb_b, BB_CAP, mid_b, 4) == 0);
    TR_CHECK(tr_engine_pipe_add(&e, 500, true, "0500E0", 0, bb_b, BB_CAP, mid_b, 32) == 0);
    TR_CHECK(e.pipe_count == 4);

    /* C에 먼저 상태를 쌓아 둔다 (OPEN 봉 1개) */
    feed_pipe(&e, 300, 0, 0, 1000, 1);
    TR_CHECK(tr_ring_count(&pc->bb.bars) == 1);

    /* B를 제거하고 D를 추가해도 C의 저장소·포인터는 그대로여야 한다 */
    TR_CHECK(tr_engine_pipe_remove(&e, 200));
    TR_CHECK(e.pipe_count == 3);
    TR_CHECK(tr_engine_pipe_find(&e, 200) == 0);
    tr_pipeline_t *pd = tr_engine_pipe_add(&e, 400, true, "0400D0", &TEST_SESS, bb_d, BB_CAP, mid_d, 32);
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

/* 다중 파이프라인 독립성 (다중 종목 Task 2): 서로 다른 종목의 틱이 instrument_id로
 * 라우팅되어 각자 독립 회귀/점수를 쌓고, 발행 페이로드 맨 끝에 파이프라인의 shcode가 실린다 */
typedef struct {
    char payloads[128][2048];
    int n;
} multi_capture_t;

static void multi_capture_cb(void *ctx, const char *stream_id, uint64_t seq, const char *payload) {
    multi_capture_t *c = (multi_capture_t *)ctx;
    TR_CHECK(c->n < 128);
    snprintf(c->payloads[c->n], sizeof(c->payloads[0]), "%s", payload);
    c->n++;
    (void)stream_id;
    (void)seq;
}

static const char *multi_find(const multi_capture_t *c, const char *a, const char *b) {
    for (int i = 0; i < c->n; i++) {
        if (strstr(c->payloads[i], a) != 0 && (b == 0 || strstr(c->payloads[i], b) != 0)) {
            return c->payloads[i];
        }
    }
    return 0;
}

static void test_two_pipes_independent(void) {
    tr_engine_t e;
    multi_capture_t cap;
    memset(&cap, 0, sizeof(cap));

    tr_engine_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.engine_instance_id = 1;
    cfg.instrument_id = 1;
    snprintf(cfg.shcode, sizeof(cfg.shcode), "%s", "AAA001");
    cfg.session = (tr_session_policy_t){KST, 540, 930, TR_SESSION_WEEKDAYS};
    cfg.timeframe_sec = 60;
    cfg.no_trade = TR_NO_TRADE_SKIP;
    cfg.predict_bars[0] = 5;
    cfg.predict_bars[1] = 10;
    cfg.predict_bars[2] = 15;
    cfg.htf_ticks = 10;
    cfg.min_r2 = 0.40;
    cfg.market_period = 20;
    cfg.is_futures = true;
    TR_CHECK(tr_engine_init(&e, &cfg, g_bb_storage, BB_CAP, g_score_mid, 32));
    tr_engine_attach_status_cb(&e, multi_capture_cb, &cap);

    static tr_candle_t bb_b[BB_CAP];
    static double mid_b[32];
    static tr_bar_status_t ring_b[BB_CAP];
    /* B는 파이프0과 다른 세션(선물형 08:45~익일 05:00)을 갖는다 — 종목별 세션 보관 검증 겸용 */
    static const tr_session_policy_t fut_sess = {KST, 525, 300, TR_SESSION_WEEKDAYS};
    tr_pipeline_t *pb = tr_engine_pipe_add(&e, 100, false, "BBB002", &fut_sess, bb_b, BB_CAP, mid_b, 32);
    TR_CHECK(pb != 0);
    TR_CHECK(strcmp(pb->shcode, "BBB002") == 0);
    TR_CHECK(pb->session.open_min == 525 && pb->session.close_min == 300); /* 기동 세션 미상속 */
    TR_CHECK(tr_engine_pipe_attach_status_ring(&e, 100, ring_b, BB_CAP));
    TR_CHECK(!tr_engine_pipe_attach_status_ring(&e, 999, ring_b, BB_CAP)); /* 없는 종목 */
    TR_CHECK(tr_engine_pipe_attach_market(&e, 100, bb_b, 64));
    TR_CHECK(!tr_engine_pipe_attach_market(&e, 999, bb_b, 64));

    /* 파이프 A(id=1): 8봉 완전 직선 상승 → 회귀 유효 */
    uint64_t id = 1;
    for (int i = 1; i <= 8; i++) {
        int mid = 98 + 2 * i;
        feed_pipe(&e, 1, (unsigned)i, 0, mid - 2, id++);
        feed_pipe(&e, 1, (unsigned)i, 30, mid + 2, id++);
    }
    /* 파이프 B(id=100): 2봉만 → 회귀 무효 유지 (A의 워밍업과 무관) */
    feed_pipe(&e, 100, 1, 0, 500, id++);
    feed_pipe(&e, 100, 1, 30, 502, id++);
    feed_pipe(&e, 100, 2, 0, 498, id++);

    /* 지표 상태가 서로 섞이지 않는다 */
    TR_CHECK(tr_engine_pipe_find(&e, 1)->lr3.reg_valid);
    TR_CHECK(!pb->lr3.reg_valid);
    TR_CHECK(tr_ring_count(&e.pipe0.bb.bars) == 8);
    TR_CHECK(tr_ring_count(&pb->bb.bars) == 2);

    /* 페이로드에 발행 파이프라인의 shcode가 실린다 */
    TR_CHECK(multi_find(&cap, "\"shcode\":\"AAA001\"", "\"reg_valid\":1") != 0);
    TR_CHECK(multi_find(&cap, "\"shcode\":\"BBB002\"", "\"reg_valid\":0") != 0);
    TR_CHECK(multi_find(&cap, "\"shcode\":\"BBB002\"", "\"reg_valid\":1") == 0);

    /* 미관측 instrument의 틱은 드롭된다 */
    {
        tr_event_envelope_t env;
        memset(&env, 0, sizeof(env));
        env.kind = TR_EVENT_TICK;
        env.event_time_us = kst(9, 3, 0);
        env.received_time_us = env.event_time_us;
        tr_tick_t tk;
        memset(&tk, 0, sizeof(tk));
        tk.instrument_id = 999;
        tk.price = 100;
        tk.qty = 1;
        tk.volume_meaning = TR_TICK_VOLUME_PER_TRADE;
        TR_CHECK(tr_engine_on_tick(&e, &env, &tk) == TR_BB_ERROR);
    }

    /* 호가도 instrument별로 독립 누적된다 */
    for (int i = 0; i < 3; i++) {
        tr_engine_on_orderbook(&e, 100, kst(9, 2, (unsigned)(10 + i)), 300000.0, 100000.0);
    }
    tr_engine_on_orderbook(&e, 999, kst(9, 2, 20), 1.0, 1.0); /* 드롭 */
    TR_CHECK(pb->obd2.validity == TR_VALIDITY_VALID);
    TR_CHECK(e.pipe0.obd2.validity != TR_VALIDITY_VALID);

    /* 파이프라인별 상태 링: B만 부착했으므로 B만 기록된다 */
    TR_CHECK(tr_engine_pipe_status_count(&e, 100) == 2);
    TR_CHECK(tr_engine_status_count(&e) == 0); /* pipe0 미부착 */
    tr_bar_status_t stb;
    TR_CHECK(tr_engine_pipe_status_at(&e, 100, 0, &stb));
    TR_CHECK(!tr_engine_pipe_status_at(&e, 999, 0, &stb));

    /* pipes[0] 제거: 마지막 파이프라인(B)의 내용이 고정 주소 pipe0으로 이식된다 */
    TR_CHECK(tr_engine_pipe_remove(&e, 1));
    TR_CHECK(e.pipe_count == 1);
    TR_CHECK(e.pipes[0] == &e.pipe0);
    TR_CHECK(e.pipe0.instrument_id == 100);
    TR_CHECK(strcmp(e.pipe0.shcode, "BBB002") == 0);
    TR_CHECK(e.pipe0.session.open_min == 525); /* 세션 정책도 이식된다 */
    TR_CHECK(e.cfg.instrument_id == 100); /* 공유 cfg도 생존 종목 기준으로 맞춘다 */
    TR_CHECK(e.cfg.session.open_min == 525 && e.cfg.session.close_min == 300);
    TR_CHECK(tr_engine_pipe_find(&e, 100) == &e.pipe0);
    TR_CHECK(tr_ring_count(&e.bb.bars) == 2); /* 익명 뷰가 이식된 B 상태를 가리킨다 */
    TR_CHECK(tr_engine_pipe_status_count(&e, 100) == 2); /* 상태 링도 그대로 */
    feed_pipe(&e, 100, 3, 0, 504, id++); /* 이식 후에도 라우팅·봉 콜백(ctx)이 정상 */
    TR_CHECK(tr_ring_count(&e.pipe0.bb.bars) == 3);

    /* 마지막 1개는 제거할 수 없다 */
    TR_CHECK(!tr_engine_pipe_remove(&e, 100));
}

/* 혼합 시장: 파이프라인은 종목별 세션을 갖는다 — 기동 종목 세션을 상속하지 않는다.
 * 선물 세션(08:45~익일 05:00) 엔진에 주식 세션(08:00~20:00) 파이프라인을 add하면
 * 08:15 틱은 주식 파이프만 받고, 20:30 틱은 선물 파이프만 받는다 (그 역도 성립). */
static void feed_at(tr_engine_t *e, uint64_t instrument_id, unsigned h, unsigned mi,
                    tr_price_t price, uint64_t id, tr_bb_status_t expect) {
    tr_event_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.kind = TR_EVENT_TICK;
    tr_civil_t c = {2024, 1, 2, h, mi, 0};
    tr_time_us_from_civil(&c, KST, &env.event_time_us);
    env.received_time_us = env.event_time_us;
    tr_tick_t tk;
    memset(&tk, 0, sizeof(tk));
    tk.instrument_id = instrument_id;
    tk.price = price;
    tk.qty = 10;
    tk.source_exec_id = id;
    tk.volume_meaning = TR_TICK_VOLUME_PER_TRADE;
    TR_CHECK(tr_engine_on_tick(e, &env, &tk) == expect);
}

static void test_mixed_market_sessions(void) {
    tr_engine_t e;
    capture_t cap;
    memset(&cap, 0, sizeof(cap));
    tr_engine_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.engine_instance_id = 1;
    cfg.instrument_id = 1;
    cfg.session = (tr_session_policy_t){KST, 525, 300, TR_SESSION_WEEKDAYS}; /* 선물: 08:45~익일 05:00 */
    cfg.timeframe_sec = 60;
    cfg.no_trade = TR_NO_TRADE_SKIP;
    cfg.predict_bars[0] = 5;
    cfg.predict_bars[1] = 10;
    cfg.predict_bars[2] = 15;
    cfg.htf_ticks = 10;
    cfg.min_r2 = 0.40;
    cfg.market_period = 20;
    cfg.is_futures = true;
    TR_CHECK(tr_engine_init(&e, &cfg, g_bb_storage, BB_CAP, g_score_mid, 32));
    tr_engine_attach_status_cb(&e, capture_cb, &cap);

    static tr_candle_t bb_s[BB_CAP];
    static double mid_s[32];
    static const tr_session_policy_t stk_sess = {KST, 480, 1200, TR_SESSION_WEEKDAYS}; /* 주식: 08:00~20:00 */
    tr_pipeline_t *ps = tr_engine_pipe_add(&e, 100, false, "005930", &stk_sess, bb_s, BB_CAP, mid_s, 32);
    TR_CHECK(ps != 0);
    TR_CHECK(ps->session.open_min == 480 && ps->session.close_min == 1200);
    TR_CHECK(e.pipe0.session.open_min == 525); /* 기동(선물) 세션 유지 */

    /* 08:15: 주식 세션 안(08:00~), 선물 세션 밖(08:45 전) */
    feed_at(&e, 100, 8, 15, 70000, 1, TR_BB_ACCEPTED);
    feed_at(&e, 1, 8, 15, 10000, 2, TR_BB_REJECTED_OUT_OF_SESSION);
    TR_CHECK(tr_ring_count(&ps->bb.bars) == 1);
    TR_CHECK(tr_ring_count(&e.pipe0.bb.bars) == 0);
    TR_CHECK(!e.pipe0.bb.has_open);

    /* 20:30: 선물 세션 안(야간), 주식 세션 밖(20:00 이후) */
    feed_at(&e, 1, 20, 30, 10050, 3, TR_BB_ACCEPTED);
    feed_at(&e, 100, 20, 30, 70100, 4, TR_BB_REJECTED_OUT_OF_SESSION);
    TR_CHECK(tr_ring_count(&ps->bb.bars) == 1);
    TR_CHECK(tr_ring_count(&e.pipe0.bb.bars) == 1);

    /* 지표 컨텍스트도 파이프라인 세션에서 계산된다 (trading day가 양쪽에 기록됨) */
    TR_CHECK(e.pipe0.has_prev_day);
    TR_CHECK(ps->has_prev_day);
}

/* RT 캐치업 병합 (tr_engine_pipe_merge_bars): 공백 구간의 과거 확정 봉이 봉 링에
 * 지표 재평가 없이 삽입되고, 상태 링 인덱스 정합이 유지되며, generation이 오른다 */
static tr_candle_t fetched_bar(unsigned h, unsigned mi, tr_price_t close) {
    tr_candle_t b;
    memset(&b, 0, sizeof(b));
    b.instrument_id = 1;
    b.timeframe_sec = 60;
    b.open_time_us = kst(h, mi, 0);
    b.close_time_us = kst(h, mi, 0) + 60 * TR_US_PER_SEC;
    b.open = close;
    b.high = close;
    b.low = close;
    b.close = close;
    b.volume = 100;
    b.state = TR_CANDLE_CLOSED;
    return b;
}

static void test_merge_bars_catchup(void) {
    tr_engine_t e;
    capture_t cap;
    init_engine(&e, &cap);
    static tr_bar_status_t ring[BB_CAP];
    TR_CHECK(tr_engine_attach_status_ring(&e, ring, BB_CAP));

    /* 라이브: 9:01~9:05 봉 (9:05는 진행 중 OPEN). 여기서 9:06~9:08이 RT 공백으로 빠졌다 */
    uint64_t id = 1;
    for (int i = 1; i <= 5; i++) {
        feed_min1(&e, i, 100 + i, id++);
    }
    TR_CHECK(tr_ring_count(&e.bb.bars) == 5);
    TR_CHECK(tr_engine_status_count(&e) == 5);
    TR_CHECK(e.bb.has_open); /* 9:05 진행 중 */
    tr_bar_status_t open_st;
    TR_CHECK(tr_engine_status_at(&e, 0, &open_st));
    TR_CHECK(!open_st.closed);
    double reg_before = e.lr3.line;
    int payloads_before = cap.n;
    uint32_t gen_before = e.generation;

    /* 캐치업 조회 결과: 9:03(중복 — 기존 봉 유지 확인용으로 종가를 다르게), 9:06~9:08(구멍),
     * 9:09(아직 안 닫힌 봉 — close > now 이므로 제외), 20:00(세션 밖 — 제외) */
    tr_candle_t fetched[6];
    fetched[0] = fetched_bar(9, 3, 9999); /* 중복: 기존 9:03(종가 103)을 덮으면 안 된다 */
    fetched[1] = fetched_bar(9, 6, 200);
    fetched[2] = fetched_bar(9, 7, 201);
    fetched[3] = fetched_bar(9, 8, 202);
    fetched[4] = fetched_bar(9, 9, 203);  /* 미확정 (now=9:09:30) */
    fetched[5] = fetched_bar(20, 0, 300); /* 세션 밖 */
    static tr_candle_t bscr[BB_CAP];
    static tr_bar_status_t sscr[BB_CAP];
    size_t ins = tr_engine_pipe_merge_bars(&e, 1, fetched, 6, kst(9, 9, 30),
                                           bscr, BB_CAP, sscr, BB_CAP);
    TR_CHECK(ins == 3);
    TR_CHECK(e.generation == gen_before + 1); /* 대시보드 재시딩 트리거 */

    /* 봉 링: 9:01..9:08 오름차순 8개. 정체된 9:05 OPEN 봉은 제자리에서 닫혔다 */
    TR_CHECK(tr_ring_count(&e.bb.bars) == 8);
    TR_CHECK(!e.bb.has_open);
    for (size_t i = 0; i < 8; i++) {
        tr_candle_t c;
        TR_CHECK(tr_ring_at(&e.bb.bars, i, &c));
        TR_CHECK(c.open_time_us == kst(9, (unsigned)(8 - i), 0)); /* back 0 = 9:08 */
        TR_CHECK(c.state == TR_CANDLE_CLOSED);
    }
    {
        tr_candle_t c;
        TR_CHECK(tr_ring_at(&e.bb.bars, 5, &c)); /* 9:03 */
        TR_CHECK(c.close == 103); /* 중복 조회 봉에 덮이지 않았다 */
    }

    /* 지표·발행은 건드리지 않는다 */
    TR_CHECK(e.lr3.line == reg_before);
    TR_CHECK(cap.n == payloads_before);

    /* 상태 링: 같은 8개로 인덱스 정합. 삽입 봉은 지표 무효, 기존 봉은 값 보존 */
    TR_CHECK(tr_engine_status_count(&e) == 8);
    for (size_t i = 0; i < 8; i++) {
        tr_candle_t c;
        tr_bar_status_t st;
        TR_CHECK(tr_ring_at(&e.bb.bars, i, &c));
        TR_CHECK(tr_engine_status_at(&e, i, &st));
        TR_CHECK(st.open_time_us == c.open_time_us);
        TR_CHECK(st.closed);
    }
    tr_bar_status_t st;
    TR_CHECK(tr_engine_status_at(&e, 0, &st)); /* 9:08 (삽입) */
    TR_CHECK(!st.reg_valid && st.score == 0);
    TR_CHECK(st.trading_day == tr_days_from_civil(2024, 1, 2));
    TR_CHECK(tr_engine_status_at(&e, 3, &st)); /* 9:05 (강제 확정) — 지표 값 보존 */
    TR_CHECK(st.closed);
    TR_CHECK(st.reg_line == open_st.reg_line && st.score == open_st.score);

    /* 라이브 재개: 다음 틱이 새 봉을 정상으로 연다 */
    feed_min1(&e, 9, 210, id++);
    TR_CHECK(tr_ring_count(&e.bb.bars) == 9);
    TR_CHECK(e.bb.has_open);
    {
        tr_candle_t c;
        TR_CHECK(tr_ring_at(&e.bb.bars, 0, &c));
        TR_CHECK(c.open_time_us == kst(9, 9, 0) && c.state == TR_CANDLE_OPEN);
    }

    /* 같은 조회 결과로 다시 병합해도 모두 중복이라 삽입 0, generation 불변 */
    ins = tr_engine_pipe_merge_bars(&e, 1, fetched, 6, kst(9, 9, 40), bscr, BB_CAP, sscr, BB_CAP);
    TR_CHECK(ins == 0);
    TR_CHECK(e.generation == gen_before + 1);
    TR_CHECK(tr_ring_count(&e.bb.bars) == 9);

    /* 방어: 모르는 종목 / 스크래치 없음 / 용량 부족 */
    TR_CHECK(tr_engine_pipe_merge_bars(&e, 999, fetched, 6, kst(9, 9, 40),
                                       bscr, BB_CAP, sscr, BB_CAP) == 0);
    TR_CHECK(tr_engine_pipe_merge_bars(&e, 1, fetched, 6, kst(9, 9, 40),
                                       0, BB_CAP, sscr, BB_CAP) == 0);
    TR_CHECK(tr_engine_pipe_merge_bars(&e, 1, fetched, 6, kst(9, 9, 40),
                                       bscr, 4, sscr, BB_CAP) == 0);
}

/* 캐치업 병합 — OPEN 봉이 정체되지 않은 경우(구멍이 OPEN 봉보다 과거): OPEN 봉은
 * 그대로 유지되고 구멍만 채워진다 */
static void test_merge_bars_keeps_live_open(void) {
    tr_engine_t e;
    capture_t cap;
    init_engine(&e, &cap);
    static tr_bar_status_t ring[BB_CAP];
    TR_CHECK(tr_engine_attach_status_ring(&e, ring, BB_CAP));

    /* 9:01 확정 + 9:04 진행 중 (9:02, 9:03 구멍) */
    feed_min1(&e, 1, 101, 1);
    feed_min1(&e, 4, 104, 2);
    TR_CHECK(tr_ring_count(&e.bb.bars) == 2);
    TR_CHECK(e.bb.has_open);

    tr_candle_t fetched[2];
    fetched[0] = fetched_bar(9, 2, 102);
    fetched[1] = fetched_bar(9, 3, 103);
    static tr_candle_t bscr[BB_CAP];
    static tr_bar_status_t sscr[BB_CAP];
    size_t ins = tr_engine_pipe_merge_bars(&e, 1, fetched, 2, kst(9, 4, 30),
                                           bscr, BB_CAP, sscr, BB_CAP);
    TR_CHECK(ins == 2);
    TR_CHECK(tr_ring_count(&e.bb.bars) == 4);
    TR_CHECK(e.bb.has_open); /* 진행 중 봉은 열린 채로 남는다 */

    /* 최신은 여전히 9:04 OPEN, 그 아래로 구멍이 채워졌다 */
    for (size_t i = 0; i < 4; i++) {
        tr_candle_t c;
        TR_CHECK(tr_ring_at(&e.bb.bars, i, &c));
        TR_CHECK(c.open_time_us == kst(9, (unsigned)(4 - i), 0));
    }
    tr_candle_t c;
    TR_CHECK(tr_ring_at(&e.bb.bars, 0, &c));
    TR_CHECK(c.state == TR_CANDLE_OPEN);

    /* 상태 링 정합 유지 */
    TR_CHECK(tr_engine_status_count(&e) == 4);
    tr_bar_status_t st;
    TR_CHECK(tr_engine_status_at(&e, 0, &st));
    TR_CHECK(!st.closed); /* 9:04 진행 중 슬롯 보존 */
    TR_CHECK(tr_engine_status_at(&e, 1, &st));
    TR_CHECK(st.closed && !st.reg_valid); /* 9:03 삽입 슬롯 */

    /* 라이브 갱신이 정상 계속된다 */
    feed_min1(&e, 4, 106, 3);
    TR_CHECK(tr_ring_count(&e.bb.bars) == 4); /* 같은 봉 갱신 */
    TR_CHECK(tr_ring_at(&e.bb.bars, 0, &c));
    TR_CHECK(c.close == 106);
}

/* 캐치업 병합 — 래핑된 링(head != 0): 삽입 0인 병합(모두 중복)은 내용을 바꾸지 않고,
 * 삽입 병합은 가장 오래된 봉을 밀어내며 구멍을 채운다. 상태 링 정합은 끝까지 유지 */
static void test_merge_bars_wrapped_ring(void) {
    tr_engine_t e;
    capture_t dummy;
    last_capture_t cap;
    memset(&cap, 0, sizeof(cap));
    init_engine(&e, &dummy);
    tr_engine_attach_status_cb(&e, last_capture_cb, &cap); /* 64페이로드 상한 캡처 회피 */
    static tr_bar_status_t ring[BB_CAP];
    TR_CHECK(tr_engine_attach_status_ring(&e, ring, BB_CAP));

    /* BB_CAP(64)을 넘겨 채워 링을 래핑시킨다: 9:00+i분 봉, i=0..69 중 30~32(구멍) 제외.
     * 67봉 공급 → 가장 오래된 3봉(i=0..2)이 밀려 oldest는 9:03 */
    uint64_t id = 1;
    for (int i = 0; i <= 69; i++) {
        if (i >= 30 && i <= 32) {
            continue;
        }
        feed_min1(&e, i, 100 + i, id++);
    }
    TR_CHECK(tr_ring_count(&e.bb.bars) == BB_CAP);
    TR_CHECK(e.bb.bars.head != 0); /* 래핑 확인 */
    TR_CHECK(e.bb.has_open);       /* 10:09 진행 중 */

    static tr_candle_t bscr[BB_CAP];
    static tr_bar_status_t sscr[BB_CAP];

    /* 삽입 0 병합(기존 봉과 중복만): 래핑된 링의 내용이 한 봉도 바뀌면 안 된다 */
    tr_candle_t dups[2] = {fetched_bar(10, 7, 1), fetched_bar(10, 8, 1)};
    size_t ins = tr_engine_pipe_merge_bars(&e, 1, dups, 2, kst(10, 9, 30),
                                           bscr, BB_CAP, sscr, BB_CAP);
    TR_CHECK(ins == 0);
    TR_CHECK(e.generation == 1);
    TR_CHECK(tr_ring_count(&e.bb.bars) == BB_CAP);
    for (size_t b = 0; b < BB_CAP; b++) {
        tr_candle_t c;
        tr_bar_status_t st;
        TR_CHECK(tr_ring_at(&e.bb.bars, b, &c));
        TR_CHECK(tr_engine_status_at(&e, b, &st));
        TR_CHECK(st.open_time_us == c.open_time_us);
        int mi = 69 - (int)b;      /* newest 10:09(i=69)에서 뒤로 */
        if (mi <= 32) {
            mi -= 3;               /* 구멍 9:30~9:32 건너뜀 */
        }
        TR_CHECK(c.open_time_us == kst((unsigned)(9 + mi / 60), (unsigned)(mi % 60), 0));
    }

    /* 구멍 병합: 3봉 삽입, 가장 오래된 3봉(9:03~9:05)이 밀려 oldest는 9:06 */
    tr_candle_t fill[3] = {fetched_bar(9, 30, 201), fetched_bar(9, 31, 202),
                           fetched_bar(9, 32, 203)};
    ins = tr_engine_pipe_merge_bars(&e, 1, fill, 3, kst(10, 9, 30),
                                    bscr, BB_CAP, sscr, BB_CAP);
    TR_CHECK(ins == 3);
    TR_CHECK(e.generation == 2);
    TR_CHECK(tr_ring_count(&e.bb.bars) == BB_CAP);
    TR_CHECK(e.bb.has_open); /* OPEN 봉(10:09)은 그대로 */
    for (size_t b = 0; b < BB_CAP; b++) {
        tr_candle_t c;
        tr_bar_status_t st;
        int mi = 69 - (int)b; /* 6..69 연속 (구멍 메워짐) */
        TR_CHECK(tr_ring_at(&e.bb.bars, b, &c));
        TR_CHECK(c.open_time_us == kst((unsigned)(9 + mi / 60), (unsigned)(mi % 60), 0));
        TR_CHECK(tr_engine_status_at(&e, b, &st));
        TR_CHECK(st.open_time_us == c.open_time_us);
    }
    /* 삽입 슬롯(9:32, back=37)은 지표 무효·확정으로 기록된다 */
    tr_bar_status_t st;
    TR_CHECK(tr_engine_status_at(&e, 37, &st));
    TR_CHECK(st.closed && !st.reg_valid);
}

int main(void) {
    test_merge_bars_catchup();
    test_merge_bars_keeps_live_open();
    test_merge_bars_wrapped_ring();
    test_replay_pipeline();
    test_session_first_reset();
    test_orderbook_path();
    test_select_symbol_generation();
    test_status_ring_alignment();
    test_daily_chain();
    test_sma_payload();
    test_sma_late_correction();
    test_ind_format();
    test_mem_reset_ring();
    test_mem_reset_correction_latch();
    test_mem_event_format();
    test_pipe_slot_reuse();
    test_two_pipes_independent();
    test_mixed_market_sessions();
    TR_TEST_SUMMARY();
}
