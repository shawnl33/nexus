/* 저장소 어댑터 테스트 (계획서 §26: 저장·복구, 명령 중복, 체결 트랜잭션·중복 제거, 봉 revision) */

#include "test_util.h"

#include <string.h>

#include "adapters/storage/store.h"

static tr_store_t *open_db(void) {
    tr_store_t *s = 0;
    char err[256] = {0};
    TR_CHECK(tr_store_open(":memory:", &s, err, sizeof(err)) == TR_STORE_OK);
    if (err[0]) {
        fprintf(stderr, "open err: %s\n", err);
    }
    TR_CHECK(tr_store_migrate(s) == TR_STORE_OK);
    /* 마이그레이션은 멱등이어야 한다 */
    TR_CHECK(tr_store_migrate(s) == TR_STORE_OK);
    return s;
}

static tr_instrument_t make_ins(void) {
    tr_instrument_t ins;
    memset(&ins, 0, sizeof(ins));
    ins.instrument_id = 1;
    strcpy(ins.broker_code, "005930");
    ins.market = TR_MARKET_KRX;
    ins.product_type = TR_PRODUCT_STOCK;
    ins.tradable = true;
    ins.price_scale = 1;
    ins.qty_scale = 1;
    ins.multiplier = 1;
    strcpy(ins.currency, "KRW");
    ins.session_policy_id = 1;
    ins.metadata_version = 1;
    return ins;
}

static void test_instrument_roundtrip(void) {
    tr_store_t *s = open_db();
    tr_instrument_t ins = make_ins();
    TR_CHECK(tr_store_put_instrument(s, &ins) == TR_STORE_OK);

    tr_instrument_t out;
    memset(&out, 0, sizeof(out));
    TR_CHECK(tr_store_get_instrument(s, 1, &out) == TR_STORE_OK);
    TR_CHECK(out.instrument_id == 1);
    TR_CHECK(strcmp(out.broker_code, "005930") == 0);
    TR_CHECK(out.market == TR_MARKET_KRX && out.tradable);
    TR_CHECK(out.price_scale == 1 && out.multiplier == 1);
    TR_CHECK(strcmp(out.currency, "KRW") == 0);

    tr_instrument_t missing;
    TR_CHECK(tr_store_get_instrument(s, 999, &missing) == TR_STORE_NOT_FOUND);
    tr_store_close(s);
}

static tr_candle_t make_candle(int64_t open_us, int64_t close_px, uint32_t rev, tr_candle_state_t st) {
    tr_candle_t c;
    memset(&c, 0, sizeof(c));
    c.instrument_id = 1;
    c.timeframe_sec = 60;
    c.open_time_us = open_us;
    c.close_time_us = open_us + 60000000;
    c.open = close_px - 1;
    c.high = close_px + 1;
    c.low = close_px - 2;
    c.close = close_px;
    c.volume = 100;
    c.state = st;
    c.revision = rev;
    c.source_id = 7;
    return c;
}

typedef struct {
    tr_candle_t bars[16];
    int n;
} collect_t;

static void on_candle(void *ctx, const tr_candle_t *c) {
    collect_t *col = (collect_t *)ctx;
    TR_CHECK(col->n < 16);
    col->bars[col->n++] = *c;
}

static void test_candle_revision_upsert(void) {
    tr_store_t *s = open_db();
    tr_candle_t c = make_candle(1000000, 100, 0, TR_CANDLE_OPEN);
    TR_CHECK(tr_store_put_candle(s, &c) == TR_STORE_OK);

    /* 같은 revision의 CLOSED는 OPEN을 덮어쓴다 */
    c = make_candle(1000000, 101, 0, TR_CANDLE_CLOSED);
    TR_CHECK(tr_store_put_candle(s, &c) == TR_STORE_OK);

    /* 더 큰 revision(정정)만 반영 */
    c = make_candle(1000000, 99, 1, TR_CANDLE_CLOSED);
    TR_CHECK(tr_store_put_candle(s, &c) == TR_STORE_OK);

    /* 낮은 revision은 무시 */
    c = make_candle(1000000, 55, 0, TR_CANDLE_CLOSED);
    TR_CHECK(tr_store_put_candle(s, &c) == TR_STORE_OK);

    collect_t col = {0};
    TR_CHECK(tr_store_query_candles(s, 1, 60, 0, 2000000, 10, on_candle, &col) == 1);
    TR_CHECK(col.bars[0].close == 99 && col.bars[0].revision == 1);
    TR_CHECK(col.bars[0].state == TR_CANDLE_CLOSED);
    tr_store_close(s);
}

static void test_candle_query_range_order(void) {
    tr_store_t *s = open_db();
    for (int i = 0; i < 5; i++) {
        tr_candle_t c = make_candle((int64_t)i * 60000000, 100 + i, 0, TR_CANDLE_CLOSED);
        TR_CHECK(tr_store_put_candle(s, &c) == TR_STORE_OK);
    }
    collect_t col = {0};
    /* [1분, 4분) 범위 → 3개, 오름차순 */
    TR_CHECK(tr_store_query_candles(s, 1, 60, 60000000, 240000000, 10, on_candle, &col) == 3);
    TR_CHECK(col.bars[0].open_time_us == 60000000);
    TR_CHECK(col.bars[2].open_time_us == 180000000);
    tr_store_close(s);
}

static void test_candle_recent_is_newest_ascending(void) {
    tr_store_t *s = open_db();
    for (int i = 0; i < 5; i++) {
        tr_candle_t c = make_candle((int64_t)i * 60000000, 100 + i, 0, TR_CANDLE_CLOSED);
        TR_CHECK(tr_store_put_candle(s, &c) == TR_STORE_OK);
    }
    tr_candle_t out[3];
    int n = tr_store_query_recent_candles(s, 1, 60, 60000000LL * 5, 3, out, 3);
    TR_CHECK(n == 3);
    TR_CHECK(out[0].open_time_us == 120000000 && out[2].open_time_us == 240000000);
    tr_store_close(s);
}

static void test_command_dedup(void) {
    tr_store_t *s = open_db();
    TR_CHECK(tr_store_command_check(s, "cmd-1", "hash-a", 1, 1000) == TR_STORE_CMD_NEW);
    TR_CHECK(tr_store_command_check(s, "cmd-1", "hash-a", 1, 1000) == TR_STORE_CMD_EXISTS);
    /* 같은 ID·다른 내용 → 충돌 거절 */
    TR_CHECK(tr_store_command_check(s, "cmd-1", "hash-b", 1, 1000) == TR_STORE_CMD_CONFLICT);

    TR_CHECK(tr_store_command_mark(s, "cmd-1", 2, "{\"status\":\"ok\"}", 2000) == TR_STORE_OK);
    tr_store_close(s);
}

typedef struct {
    int n;
    int64_t cum;
    int state;
    char broker[32];
} order_collect_t;

static void on_order(void *ctx, const tr_store_order_t *o, const char *broker_id,
                     int64_t cum, int64_t updated) {
    order_collect_t *c = (order_collect_t *)ctx;
    c->n++;
    c->cum = cum;
    c->state = o->state;
    snprintf(c->broker, sizeof(c->broker), "%s", broker_id != 0 ? broker_id : "(null)");
    (void)updated;
}

static void test_fill_transaction_and_dedup(void) {
    tr_store_t *s = open_db();
    tr_store_order_t o;
    memset(&o, 0, sizeof(o));
    o.order_id = "ord-1";
    o.account_id = "acc-1";
    o.instrument_id = 1;
    o.side = 1;
    o.type = 2;
    o.quantity = 10;
    o.price = 100;
    o.state = 3; /* WORKING */
    o.created_at_us = 1000;
    TR_CHECK(tr_store_order_insert(s, &o) == TR_STORE_OK);

    tr_store_fill_t f;
    memset(&f, 0, sizeof(f));
    f.order_id = "ord-1";
    f.account_id = "acc-1";
    f.instrument_id = 1;
    f.quantity = 4;
    f.price = 100;
    f.fees = 1;
    f.source_execution_id = "exec-100";
    f.executed_at_us = 2000;

    tr_store_order_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.order_id = "ord-1";
    ev.engine_sequence = 10;
    ev.event_type = 6; /* FILL */
    ev.source_id = 7;
    ev.event_time_us = 2000;
    ev.received_time_us = 2000;
    ev.payload = "{\"note\":\"partial\"}";

    /* 부분 체결 기록: fills + event + orders 갱신이 하나의 트랜잭션 */
    TR_CHECK(tr_store_record_fill(s, &f, 4, 4, &ev) == TR_STORE_OK);

    order_collect_t col = {0, 0, 0, {0}};
    TR_CHECK(tr_store_query_orders(s, "acc-1", 10, on_order, &col) == 1);
    TR_CHECK(col.cum == 4 && col.state == 4);

    /* 같은 체결 ID 재수신: 전부 기록되지 않고 DUPLICATE */
    TR_CHECK(tr_store_record_fill(s, &f, 8, 4, &ev) == TR_STORE_DUPLICATE);
    col = (order_collect_t){0, 0, 0, {0}};
    TR_CHECK(tr_store_query_orders(s, "acc-1", 10, on_order, &col) == 1);
    TR_CHECK(col.cum == 4); /* 롤백되어 갱신 없음 */
    tr_store_close(s);
}

static void test_persistence_reopen(void) {
    const char *path = "/tmp/tr_store_test.db";
    remove(path);
    remove("/tmp/tr_store_test.db-wal");
    remove("/tmp/tr_store_test.db-shm");
    tr_store_t *s = 0;
    TR_CHECK(tr_store_open(path, &s, 0, 0) == TR_STORE_OK);
    TR_CHECK(tr_store_migrate(s) == TR_STORE_OK);
    tr_instrument_t ins = make_ins();
    TR_CHECK(tr_store_put_instrument(s, &ins) == TR_STORE_OK);
    tr_store_close(s);

    /* 재오픈핮 데이터가 남아 있어야 한다 (WAL 체크포인트 포함) */
    TR_CHECK(tr_store_open(path, &s, 0, 0) == TR_STORE_OK);
    tr_instrument_t out;
    TR_CHECK(tr_store_get_instrument(s, 1, &out) == TR_STORE_OK);
    TR_CHECK(strcmp(out.broker_code, "005930") == 0);
    tr_store_close(s);
    remove(path);
    remove("/tmp/tr_store_test.db-wal");
    remove("/tmp/tr_store_test.db-shm");
}

int main(void) {
    test_instrument_roundtrip();
    test_candle_revision_upsert();
    test_candle_query_range_order();
    test_candle_recent_is_newest_ascending();
    test_command_dedup();
    test_fill_transaction_and_dedup();
    test_persistence_reopen();
    TR_TEST_SUMMARY();
}
