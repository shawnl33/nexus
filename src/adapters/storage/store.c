#include "adapters/storage/store.h"

#include <sqlite3.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct tr_store {
    sqlite3 *db;
};

/* ---------- 유틸 ---------- */

static tr_store_rc_t exec_sql(tr_store_t *s, const char *sql) {
    char *err = 0;
    int rc = sqlite3_exec(s->db, sql, 0, 0, &err);
    if (rc != SQLITE_OK) {
        sqlite3_free(err);
        return TR_STORE_ERR;
    }
    return TR_STORE_OK;
}

static tr_store_rc_t execf(tr_store_t *s, const char *fmt, ...) {
    char sql[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(sql, sizeof(sql), fmt, ap);
    va_end(ap);
    return exec_sql(s, sql);
}

static tr_store_rc_t bind_step(tr_store_t *s, sqlite3_stmt *st) {
    (void)s;
    int rc = sqlite3_step(st);
    if (rc == SQLITE_DONE) {
        return TR_STORE_OK;
    }
    if (rc == SQLITE_CONSTRAINT) {
        return TR_STORE_DUPLICATE;
    }
    return TR_STORE_ERR;
}

/* ---------- 스키마 ---------- */

static const char *SCHEMA_V1 =
    "CREATE TABLE schema_migrations("
    "  version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL, checksum INTEGER NOT NULL);"
    "CREATE TABLE instruments("
    "  instrument_id INTEGER PRIMARY KEY, broker_code TEXT NOT NULL,"
    "  market INTEGER NOT NULL, product_type INTEGER NOT NULL, tradable INTEGER NOT NULL,"
    "  price_scale INTEGER NOT NULL, qty_scale INTEGER NOT NULL, multiplier INTEGER NOT NULL,"
    "  currency TEXT NOT NULL, session_policy_id INTEGER NOT NULL, metadata_version INTEGER NOT NULL);"
    "CREATE TABLE candles("
    "  instrument_id INTEGER NOT NULL, timeframe INTEGER NOT NULL,"
    "  open_time INTEGER NOT NULL, close_time INTEGER NOT NULL,"
    "  open INTEGER NOT NULL, high INTEGER NOT NULL, low INTEGER NOT NULL, close INTEGER NOT NULL,"
    "  volume INTEGER NOT NULL, source_id INTEGER NOT NULL,"
    "  is_closed INTEGER NOT NULL, revision INTEGER NOT NULL, quality INTEGER NOT NULL,"
    "  PRIMARY KEY (instrument_id, timeframe, open_time, source_id));"
    "CREATE INDEX idx_candles_lookup ON candles(instrument_id, timeframe, open_time);"
    "CREATE TABLE strategy_runs("
    "  run_id TEXT PRIMARY KEY, strategy_id TEXT NOT NULL, strategy_version TEXT NOT NULL,"
    "  mode INTEGER NOT NULL, config_version INTEGER NOT NULL, input_manifest TEXT,"
    "  started_at INTEGER NOT NULL, ended_at INTEGER, status INTEGER NOT NULL);"
    "CREATE TABLE orders("
    "  order_id TEXT PRIMARY KEY, run_id TEXT, account_id TEXT NOT NULL,"
    "  instrument_id INTEGER NOT NULL, side INTEGER NOT NULL, type INTEGER NOT NULL,"
    "  quantity INTEGER NOT NULL, price INTEGER NOT NULL, broker_id TEXT,"
    "  state INTEGER NOT NULL, cumulative_fill INTEGER NOT NULL,"
    "  created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL);"
    "CREATE UNIQUE INDEX idx_orders_broker ON orders(broker_id) WHERE broker_id IS NOT NULL;"
    "CREATE TABLE order_events("
    "  event_id INTEGER PRIMARY KEY AUTOINCREMENT, order_id TEXT NOT NULL,"
    "  engine_sequence INTEGER NOT NULL, event_type INTEGER NOT NULL, source_id INTEGER NOT NULL,"
    "  event_time INTEGER NOT NULL, received_time INTEGER NOT NULL, payload TEXT);"
    "CREATE INDEX idx_order_events_order ON order_events(order_id, engine_sequence);"
    "CREATE TABLE fills("
    "  fill_id INTEGER PRIMARY KEY AUTOINCREMENT, order_id TEXT NOT NULL,"
    "  account_id TEXT NOT NULL, instrument_id INTEGER NOT NULL,"
    "  quantity INTEGER NOT NULL, price INTEGER NOT NULL, fees INTEGER NOT NULL,"
    "  source_execution_id TEXT NOT NULL, executed_at INTEGER NOT NULL);"
    "CREATE UNIQUE INDEX idx_fills_source_exec ON fills(account_id, source_execution_id);"
    "CREATE TABLE position_snapshots("
    "  account_id TEXT NOT NULL, instrument_id INTEGER NOT NULL, as_of_sequence INTEGER NOT NULL,"
    "  quantity INTEGER NOT NULL, avg_price INTEGER NOT NULL, realized_pnl INTEGER NOT NULL,"
    "  provenance TEXT, PRIMARY KEY (account_id, instrument_id, as_of_sequence));"
    "CREATE TABLE commands("
    "  command_id TEXT PRIMARY KEY, payload_hash TEXT NOT NULL, command_type INTEGER NOT NULL,"
    "  state INTEGER NOT NULL, result TEXT, created_at INTEGER NOT NULL, applied_at INTEGER);"
    "CREATE TABLE config_versions("
    "  config_version INTEGER PRIMARY KEY, scope TEXT NOT NULL, contents TEXT NOT NULL,"
    "  effective_sequence INTEGER NOT NULL, created_at INTEGER NOT NULL);"
    "CREATE TABLE backtest_runs("
    "  run_id TEXT PRIMARY KEY, input_hash TEXT NOT NULL, code_version TEXT NOT NULL,"
    "  config_version INTEGER NOT NULL, clock_policy TEXT, fill_policy TEXT, cost_policy TEXT,"
    "  seed INTEGER, metrics TEXT, artifact_paths TEXT);"
    "CREATE TABLE checkpoints("
    "  checkpoint_id TEXT PRIMARY KEY, schema_version INTEGER NOT NULL, code_version TEXT NOT NULL,"
    "  as_of_sequence INTEGER NOT NULL, state_path TEXT NOT NULL, state_hash TEXT NOT NULL);"
    "CREATE TABLE pnl_snapshots("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT, scope TEXT NOT NULL, scope_id TEXT NOT NULL,"
    "  time INTEGER NOT NULL, realized INTEGER NOT NULL, unrealized INTEGER NOT NULL,"
    "  fees INTEGER NOT NULL, valuation_basis TEXT NOT NULL);";

static uint64_t fnv1a64(const char *p) {
    uint64_t h = 1469598103934665603ULL;
    while (*p) {
        h ^= (unsigned char)*p++;
        h *= 1099511628211ULL;
    }
    return h;
}

/* ---------- 수명 ---------- */

tr_store_rc_t tr_store_open(const char *path, tr_store_t **out, char *errbuf, size_t errlen) {
    if (path == 0 || out == 0) {
        return TR_STORE_ERR;
    }
    tr_store_t *s = (tr_store_t *)calloc(1, sizeof(tr_store_t));
    if (s == 0) {
        return TR_STORE_ERR;
    }
    int rc = sqlite3_open(path, &s->db);
    if (rc != SQLITE_OK) {
        if (errbuf != 0 && errlen > 0) {
            snprintf(errbuf, errlen, "sqlite3_open: %s", sqlite3_errmsg(s->db));
        }
        sqlite3_close(s->db);
        free(s);
        return TR_STORE_ERR;
    }
    /* WAL + 거래 기록 내구성 우선 (계획서 §20) */
    if (exec_sql(s, "PRAGMA journal_mode=WAL;") != TR_STORE_OK ||
        exec_sql(s, "PRAGMA synchronous=FULL;") != TR_STORE_OK ||
        exec_sql(s, "PRAGMA foreign_keys=ON;") != TR_STORE_OK ||
        exec_sql(s, "PRAGMA busy_timeout=5000;") != TR_STORE_OK) {
        tr_store_close(s);
        return TR_STORE_ERR;
    }
    *out = s;
    return TR_STORE_OK;
}

void tr_store_close(tr_store_t *s) {
    if (s == 0) {
        return;
    }
    sqlite3_close(s->db);
    free(s);
}

tr_store_rc_t tr_store_migrate(tr_store_t *s) {
    int version = 0;
    sqlite3_stmt *st = 0;
    bool has_table = false;
    if (sqlite3_prepare_v2(s->db, "SELECT name FROM sqlite_master WHERE type='table' AND name='schema_migrations';", -1, &st, 0) == SQLITE_OK) {
        has_table = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_finalize(st);
    }
    if (has_table) {
        st = 0;
        if (sqlite3_prepare_v2(s->db, "SELECT COALESCE(MAX(version),0) FROM schema_migrations;", -1, &st, 0) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) {
                version = sqlite3_column_int(st, 0);
            }
            sqlite3_finalize(st);
        }
    }

    static const struct {
        int version;
        const char *sql;
    } MIGRATIONS[] = {
        {1, 0}, /* v1: SCHEMA_V1 사용 */
    };
    (void)MIGRATIONS;

    if (version < 1) {
        if (exec_sql(s, "BEGIN IMMEDIATE;") != TR_STORE_OK) {
            return TR_STORE_ERR;
        }
        tr_store_rc_t rc = exec_sql(s, SCHEMA_V1);
        if (rc == TR_STORE_OK) {
            rc = execf(s, "INSERT INTO schema_migrations(version, applied_at, checksum) "
                          "VALUES(1, datetime('now'), %lld);",
                       (long long)fnv1a64(SCHEMA_V1));
        }
        if (rc != TR_STORE_OK) {
            exec_sql(s, "ROLLBACK;");
            return rc;
        }
        return exec_sql(s, "COMMIT;");
    }
    return TR_STORE_OK;
}

/* ---------- 종목 ---------- */

tr_store_rc_t tr_store_put_instrument(tr_store_t *s, const tr_instrument_t *ins) {
    static const char *SQL =
        "INSERT INTO instruments(instrument_id, broker_code, market, product_type, tradable,"
        " price_scale, qty_scale, multiplier, currency, session_policy_id, metadata_version)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(instrument_id) DO UPDATE SET"
        "  broker_code=excluded.broker_code, market=excluded.market, product_type=excluded.product_type,"
        "  tradable=excluded.tradable, price_scale=excluded.price_scale, qty_scale=excluded.qty_scale,"
        "  multiplier=excluded.multiplier, currency=excluded.currency,"
        "  session_policy_id=excluded.session_policy_id, metadata_version=excluded.metadata_version"
        " WHERE excluded.metadata_version >= instruments.metadata_version;";
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(s->db, SQL, -1, &st, 0) != SQLITE_OK) {
        return TR_STORE_ERR;
    }
    sqlite3_bind_int64(st, 1, (sqlite3_int64)ins->instrument_id);
    sqlite3_bind_text(st, 2, ins->broker_code, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, ins->market);
    sqlite3_bind_int(st, 4, ins->product_type);
    sqlite3_bind_int(st, 5, ins->tradable ? 1 : 0);
    sqlite3_bind_int64(st, 6, ins->price_scale);
    sqlite3_bind_int64(st, 7, ins->qty_scale);
    sqlite3_bind_int64(st, 8, ins->multiplier);
    sqlite3_bind_text(st, 9, ins->currency, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 10, (int)ins->session_policy_id);
    sqlite3_bind_int64(st, 11, (sqlite3_int64)ins->metadata_version);
    tr_store_rc_t rc = bind_step(s, st);
    sqlite3_finalize(st);
    return rc;
}

tr_store_rc_t tr_store_get_instrument(tr_store_t *s, uint64_t instrument_id, tr_instrument_t *out) {
    static const char *SQL = "SELECT broker_code, market, product_type, tradable, price_scale,"
                             " qty_scale, multiplier, currency, session_policy_id, metadata_version"
                             " FROM instruments WHERE instrument_id = ?;";
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(s->db, SQL, -1, &st, 0) != SQLITE_OK) {
        return TR_STORE_ERR;
    }
    sqlite3_bind_int64(st, 1, (sqlite3_int64)instrument_id);
    tr_store_rc_t rc = TR_STORE_NOT_FOUND;
    if (sqlite3_step(st) == SQLITE_ROW) {
        memset(out, 0, sizeof(*out));
        out->instrument_id = instrument_id;
        snprintf(out->broker_code, sizeof(out->broker_code), "%s", (const char *)sqlite3_column_text(st, 0));
        out->market = sqlite3_column_int(st, 1);
        out->product_type = sqlite3_column_int(st, 2);
        out->tradable = sqlite3_column_int(st, 3) != 0;
        out->price_scale = sqlite3_column_int64(st, 4);
        out->qty_scale = sqlite3_column_int64(st, 5);
        out->multiplier = sqlite3_column_int64(st, 6);
        snprintf(out->currency, sizeof(out->currency), "%s", (const char *)sqlite3_column_text(st, 7));
        out->session_policy_id = (uint32_t)sqlite3_column_int(st, 8);
        out->metadata_version = (uint64_t)sqlite3_column_int64(st, 9);
        rc = TR_STORE_OK;
    }
    sqlite3_finalize(st);
    return rc;
}

/* ---------- 봉 ---------- */

tr_store_rc_t tr_store_put_candle(tr_store_t *s, const tr_candle_t *c) {
    static const char *SQL =
        "INSERT INTO candles(instrument_id, timeframe, open_time, close_time,"
        " open, high, low, close, volume, source_id, is_closed, revision, quality)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(instrument_id, timeframe, open_time, source_id) DO UPDATE SET"
        "  close_time=excluded.close_time, open=excluded.open, high=excluded.high,"
        "  low=excluded.low, close=excluded.close, volume=excluded.volume,"
        "  is_closed=excluded.is_closed, revision=excluded.revision, quality=excluded.quality"
        " WHERE excluded.revision > candles.revision"
        "    OR (excluded.revision = candles.revision AND excluded.is_closed > candles.is_closed);";
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(s->db, SQL, -1, &st, 0) != SQLITE_OK) {
        return TR_STORE_ERR;
    }
    sqlite3_bind_int64(st, 1, (sqlite3_int64)c->instrument_id);
    sqlite3_bind_int(st, 2, (int)c->timeframe_sec);
    sqlite3_bind_int64(st, 3, c->open_time_us);
    sqlite3_bind_int64(st, 4, c->close_time_us);
    sqlite3_bind_int64(st, 5, c->open);
    sqlite3_bind_int64(st, 6, c->high);
    sqlite3_bind_int64(st, 7, c->low);
    sqlite3_bind_int64(st, 8, c->close);
    sqlite3_bind_int64(st, 9, c->volume);
    sqlite3_bind_int64(st, 10, (sqlite3_int64)c->source_id);
    sqlite3_bind_int(st, 11, c->state == TR_CANDLE_CLOSED ? 1 : 0);
    sqlite3_bind_int(st, 12, (int)c->revision);
    sqlite3_bind_int(st, 13, (int)c->quality);
    tr_store_rc_t rc = bind_step(s, st);
    sqlite3_finalize(st);
    return rc;
}

int tr_store_query_candles(tr_store_t *s, uint64_t instrument_id, uint32_t timeframe,
                           int64_t from_us, int64_t to_us, size_t limit,
                           tr_store_candle_fn cb, void *ctx) {
    static const char *SQL =
        "SELECT open_time, close_time, open, high, low, close, volume, source_id, is_closed, revision, quality"
        " FROM candles WHERE instrument_id = ? AND timeframe = ? AND open_time >= ? AND open_time < ?"
        " ORDER BY open_time ASC LIMIT ?;";
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(s->db, SQL, -1, &st, 0) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int64(st, 1, (sqlite3_int64)instrument_id);
    sqlite3_bind_int(st, 2, (int)timeframe);
    sqlite3_bind_int64(st, 3, from_us);
    sqlite3_bind_int64(st, 4, to_us);
    sqlite3_bind_int64(st, 5, (sqlite3_int64)limit);
    int count = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (cb != 0) {
            tr_candle_t c;
            memset(&c, 0, sizeof(c));
            c.instrument_id = instrument_id;
            c.timeframe_sec = timeframe;
            c.open_time_us = sqlite3_column_int64(st, 0);
            c.close_time_us = sqlite3_column_int64(st, 1);
            c.open = sqlite3_column_int64(st, 2);
            c.high = sqlite3_column_int64(st, 3);
            c.low = sqlite3_column_int64(st, 4);
            c.close = sqlite3_column_int64(st, 5);
            c.volume = sqlite3_column_int64(st, 6);
            c.source_id = (uint64_t)sqlite3_column_int64(st, 7);
            c.state = sqlite3_column_int(st, 8) ? TR_CANDLE_CLOSED : TR_CANDLE_OPEN;
            c.revision = (uint32_t)sqlite3_column_int(st, 9);
            c.quality = (tr_quality_flags_t)sqlite3_column_int(st, 10);
            cb(ctx, &c);
        }
        count++;
    }
    sqlite3_finalize(st);
    return count;
}

int tr_store_query_recent_candles(tr_store_t *s, uint64_t instrument_id, uint32_t timeframe,
                                  int64_t before_us, size_t limit,
                                  tr_candle_t *out, size_t cap) {
    if (s == 0 || out == 0 || cap == 0 || limit == 0) {
        return -1;
    }
    if (limit > cap) {
        limit = cap;
    }
    static const char *SQL =
        "SELECT open_time, close_time, open, high, low, close, volume, source_id, is_closed, revision, quality"
        " FROM candles WHERE instrument_id = ? AND timeframe = ? AND open_time < ?"
        " ORDER BY open_time DESC LIMIT ?;";
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(s->db, SQL, -1, &st, 0) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int64(st, 1, (sqlite3_int64)instrument_id);
    sqlite3_bind_int(st, 2, (int)timeframe);
    sqlite3_bind_int64(st, 3, before_us);
    sqlite3_bind_int64(st, 4, (sqlite3_int64)limit);
    /* 최근 N개를 역순으로 받은 뒤 뒤집는다. */
    tr_candle_t *tmp = out;
    int n = 0;
    while (sqlite3_step(st) == SQLITE_ROW && (size_t)n < limit) {
        tr_candle_t c;
        memset(&c, 0, sizeof(c));
        c.instrument_id = instrument_id;
        c.timeframe_sec = timeframe;
        c.open_time_us = sqlite3_column_int64(st, 0);
        c.close_time_us = sqlite3_column_int64(st, 1);
        c.open = sqlite3_column_int64(st, 2);
        c.high = sqlite3_column_int64(st, 3);
        c.low = sqlite3_column_int64(st, 4);
        c.close = sqlite3_column_int64(st, 5);
        c.volume = sqlite3_column_int64(st, 6);
        c.source_id = (uint64_t)sqlite3_column_int64(st, 7);
        c.state = sqlite3_column_int(st, 8) ? TR_CANDLE_CLOSED : TR_CANDLE_OPEN;
        c.revision = (uint32_t)sqlite3_column_int(st, 9);
        c.quality = (tr_quality_flags_t)sqlite3_column_int(st, 10);
        if ((c.quality & TR_QUALITY_FILLED_EMPTY) != 0) {
            continue;
        }
        tmp[n++] = c;
    }
    sqlite3_finalize(st);
    for (int i = 0; i < n / 2; i++) {
        tr_candle_t swap = tmp[i];
        tmp[i] = tmp[n - 1 - i];
        tmp[n - 1 - i] = swap;
    }
    return n;
}

/* ---------- 명령 ---------- */

tr_store_cmd_check_t tr_store_command_check(tr_store_t *s, const char *command_id,
                                            const char *payload_hash, int command_type,
                                            int64_t created_at_us) {
    sqlite3_stmt *st = 0;
    tr_store_cmd_check_t result = TR_STORE_CMD_NEW;
    if (sqlite3_prepare_v2(s->db, "SELECT payload_hash FROM commands WHERE command_id = ?;", -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, command_id, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const char *existing = (const char *)sqlite3_column_text(st, 0);
            result = (existing != 0 && strcmp(existing, payload_hash) == 0)
                         ? TR_STORE_CMD_EXISTS
                         : TR_STORE_CMD_CONFLICT;
        }
        sqlite3_finalize(st);
    }
    if (result == TR_STORE_CMD_NEW) {
        st = 0;
        if (sqlite3_prepare_v2(s->db, "INSERT INTO commands(command_id, payload_hash, command_type, state, created_at)"
                                      " VALUES(?,?,?,0,?);", -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, command_id, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, payload_hash, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 3, command_type);
            sqlite3_bind_int64(st, 4, created_at_us);
            if (bind_step(s, st) != TR_STORE_OK) {
                result = TR_STORE_CMD_CONFLICT; /* 동시 삽입 경합 등 */
            }
            sqlite3_finalize(st);
        }
    }
    return result;
}

tr_store_rc_t tr_store_command_mark(tr_store_t *s, const char *command_id,
                                    int state, const char *result, int64_t applied_at_us) {
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(s->db, "UPDATE commands SET state = ?, result = ?, applied_at = ?"
                                  " WHERE command_id = ?;", -1, &st, 0) != SQLITE_OK) {
        return TR_STORE_ERR;
    }
    sqlite3_bind_int(st, 1, state);
    sqlite3_bind_text(st, 2, result, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, applied_at_us);
    sqlite3_bind_text(st, 4, command_id, -1, SQLITE_TRANSIENT);
    tr_store_rc_t rc = bind_step(s, st);
    sqlite3_finalize(st);
    return rc;
}

/* ---------- 주문·체결 ---------- */

tr_store_rc_t tr_store_order_insert(tr_store_t *s, const tr_store_order_t *o) {
    static const char *SQL =
        "INSERT INTO orders(order_id, run_id, account_id, instrument_id, side, type,"
        " quantity, price, state, cumulative_fill, created_at, updated_at)"
        " VALUES(?,?,?,?,?,?,?,?,?,0,?,?);";
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(s->db, SQL, -1, &st, 0) != SQLITE_OK) {
        return TR_STORE_ERR;
    }
    sqlite3_bind_text(st, 1, o->order_id, -1, SQLITE_TRANSIENT);
    if (o->run_id != 0) {
        sqlite3_bind_text(st, 2, o->run_id, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(st, 2);
    }
    sqlite3_bind_text(st, 3, o->account_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, (sqlite3_int64)o->instrument_id);
    sqlite3_bind_int(st, 5, o->side);
    sqlite3_bind_int(st, 6, o->type);
    sqlite3_bind_int64(st, 7, o->quantity);
    sqlite3_bind_int64(st, 8, o->price);
    sqlite3_bind_int(st, 9, o->state);
    sqlite3_bind_int64(st, 10, o->created_at_us);
    sqlite3_bind_int64(st, 11, o->created_at_us);
    tr_store_rc_t rc = bind_step(s, st);
    sqlite3_finalize(st);
    return rc;
}

tr_store_rc_t tr_store_order_add_event(tr_store_t *s, const tr_store_order_event_t *ev) {
    static const char *SQL =
        "INSERT INTO order_events(order_id, engine_sequence, event_type, source_id,"
        " event_time, received_time, payload) VALUES(?,?,?,?,?,?,?);";
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(s->db, SQL, -1, &st, 0) != SQLITE_OK) {
        return TR_STORE_ERR;
    }
    sqlite3_bind_text(st, 1, ev->order_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, (sqlite3_int64)ev->engine_sequence);
    sqlite3_bind_int(st, 3, ev->event_type);
    sqlite3_bind_int64(st, 4, (sqlite3_int64)ev->source_id);
    sqlite3_bind_int64(st, 5, ev->event_time_us);
    sqlite3_bind_int64(st, 6, ev->received_time_us);
    sqlite3_bind_text(st, 7, ev->payload != 0 ? ev->payload : "", -1, SQLITE_TRANSIENT);
    tr_store_rc_t rc = bind_step(s, st);
    sqlite3_finalize(st);
    return rc;
}

tr_store_rc_t tr_store_record_fill(tr_store_t *s, const tr_store_fill_t *fill,
                                   int64_t new_cumulative_fill, int new_order_state,
                                   const tr_store_order_event_t *ev) {
    if (exec_sql(s, "BEGIN IMMEDIATE;") != TR_STORE_OK) {
        return TR_STORE_ERR;
    }
    tr_store_rc_t rc;
    /* fills: source_execution_id 유니크 (계좌 범위) */
    sqlite3_stmt *st = 0;
    rc = TR_STORE_ERR;
    if (sqlite3_prepare_v2(s->db, "INSERT INTO fills(order_id, account_id, instrument_id,"
                                  " quantity, price, fees, source_execution_id, executed_at)"
                                  " VALUES(?,?,?,?,?,?,?,?);", -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, fill->order_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, fill->account_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 3, (sqlite3_int64)fill->instrument_id);
        sqlite3_bind_int64(st, 4, fill->quantity);
        sqlite3_bind_int64(st, 5, fill->price);
        sqlite3_bind_int64(st, 6, fill->fees);
        sqlite3_bind_text(st, 7, fill->source_execution_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 8, fill->executed_at_us);
        rc = bind_step(s, st);
        sqlite3_finalize(st);
    }
    if (rc == TR_STORE_OK && ev != 0) {
        rc = tr_store_order_add_event(s, ev);
    }
    if (rc == TR_STORE_OK) {
        st = 0;
        rc = TR_STORE_ERR;
        if (sqlite3_prepare_v2(s->db, "UPDATE orders SET cumulative_fill = ?, state = ?, updated_at = ?"
                                      " WHERE order_id = ?;", -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_int64(st, 1, new_cumulative_fill);
            sqlite3_bind_int(st, 2, new_order_state);
            sqlite3_bind_int64(st, 3, fill->executed_at_us);
            sqlite3_bind_text(st, 4, fill->order_id, -1, SQLITE_TRANSIENT);
            rc = bind_step(s, st);
            sqlite3_finalize(st);
        }
    }
    if (rc != TR_STORE_OK) {
        exec_sql(s, "ROLLBACK;");
        return rc;
    }
    return exec_sql(s, "COMMIT;");
}

/* ---------- 주문 조회 ---------- */

int tr_store_query_orders(tr_store_t *s, const char *account_id, size_t limit,
                          tr_store_order_fn cb, void *ctx) {
    static const char *SQL =
        "SELECT order_id, run_id, instrument_id, side, type, quantity, price,"
        " state, cumulative_fill, broker_id, created_at, updated_at"
        " FROM orders WHERE account_id = ? ORDER BY created_at DESC LIMIT ?;";
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(s->db, SQL, -1, &st, 0) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(st, 1, account_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, (sqlite3_int64)limit);
    int count = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (cb != 0) {
            tr_store_order_t o;
            memset(&o, 0, sizeof(o));
            o.order_id = (const char *)sqlite3_column_text(st, 0);
            o.run_id = (const char *)sqlite3_column_text(st, 1);
            o.account_id = account_id;
            o.instrument_id = (uint64_t)sqlite3_column_int64(st, 2);
            o.side = sqlite3_column_int(st, 3);
            o.type = sqlite3_column_int(st, 4);
            o.quantity = sqlite3_column_int64(st, 5);
            o.price = sqlite3_column_int64(st, 6);
            o.state = sqlite3_column_int(st, 7);
            int64_t cum = sqlite3_column_int64(st, 8);
            const char *broker_id = (const char *)sqlite3_column_text(st, 9);
            o.created_at_us = sqlite3_column_int64(st, 10);
            int64_t updated = sqlite3_column_int64(st, 11);
            cb(ctx, &o, broker_id, cum, updated);
        }
        count++;
    }
    sqlite3_finalize(st);
    return count;
}
