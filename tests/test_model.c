/* 모델 계약 테스트: Instrument 유효성, Candle 불변식, 품질 플래그 구분 */

#include "test_util.h"

#include <string.h>

#include "core/market/candle.h"
#include "core/model/envelope.h"
#include "core/model/instrument.h"

static tr_instrument_t make_valid_stock(void) {
    tr_instrument_t ins;
    memset(&ins, 0, sizeof(ins));
    ins.instrument_id = 1;
    strcpy(ins.broker_code, "005930");
    ins.market = TR_MARKET_KRX;
    ins.product_type = TR_PRODUCT_STOCK;
    ins.tradable = true;
    ins.price_scale = 1;   /* KRX 주식은 원화 단위 */
    ins.qty_scale = 1;
    ins.multiplier = 1;
    strcpy(ins.currency, "KRW");
    ins.session_policy_id = 1;
    ins.metadata_version = 1;
    return ins;
}

static void test_instrument_validate(void) {
    tr_instrument_t ins = make_valid_stock();
    TR_CHECK(tr_instrument_validate(&ins));
    TR_CHECK(!tr_instrument_validate(0));

    tr_instrument_t bad = ins;
    bad.instrument_id = 0;
    TR_CHECK(!tr_instrument_validate(&bad));

    bad = ins;
    bad.broker_code[0] = '\0';
    TR_CHECK(!tr_instrument_validate(&bad));

    bad = ins;
    bad.price_scale = 3;
    TR_CHECK(!tr_instrument_validate(&bad));

    bad = ins;
    bad.qty_scale = 0;
    TR_CHECK(!tr_instrument_validate(&bad));

    bad = ins;
    bad.multiplier = 0;
    TR_CHECK(!tr_instrument_validate(&bad));

    bad = ins;
    bad.market = TR_MARKET_UNKNOWN;
    TR_CHECK(!tr_instrument_validate(&bad));

    /* 지수는 거래 불가지만 유효한 종목이다 */
    tr_instrument_t idx = make_valid_stock();
    idx.instrument_id = 2;
    strcpy(idx.broker_code, "KOSPI200");
    idx.market = TR_MARKET_INDEX;
    idx.product_type = TR_PRODUCT_INDEX;
    idx.tradable = false;
    TR_CHECK(tr_instrument_validate(&idx));
}

static void test_candle_consistency(void) {
    tr_candle_t c;
    memset(&c, 0, sizeof(c));
    c.instrument_id = 1;
    c.timeframe_sec = 60;
    c.open_time_us = 1000000;
    c.close_time_us = 1060000;
    c.open = 100;
    c.high = 110;
    c.low = 90;
    c.close = 105;
    c.volume = 1000;
    c.state = TR_CANDLE_CLOSED;
    TR_CHECK(tr_candle_is_consistent(&c));

    tr_candle_t bad = c;
    bad.high = 80; /* high < low, high < close */
    TR_CHECK(!tr_candle_is_consistent(&bad));

    bad = c;
    bad.open_time_us = bad.close_time_us; /* 시작 == 종료 */
    TR_CHECK(!tr_candle_is_consistent(&bad));

    bad = c;
    bad.volume = -1;
    TR_CHECK(!tr_candle_is_consistent(&bad));

    bad = c;
    bad.timeframe_sec = 0;
    TR_CHECK(!tr_candle_is_consistent(&bad));

    TR_CHECK(!tr_candle_is_consistent(0));
}

static void test_quality_flags_distinct(void) {
    TR_CHECK(TR_QUALITY_NONE == 0);
    TR_CHECK((TR_QUALITY_LATE & TR_QUALITY_DUPLICATE) == 0);
    TR_CHECK((TR_QUALITY_LATE | TR_QUALITY_GAP) != TR_QUALITY_LATE);

    tr_event_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.kind = TR_EVENT_TICK;
    env.quality = TR_QUALITY_LATE | TR_QUALITY_GAP;
    TR_CHECK((env.quality & TR_QUALITY_LATE) != 0);
    TR_CHECK((env.quality & TR_QUALITY_GAP) != 0);
    TR_CHECK((env.quality & TR_QUALITY_DUPLICATE) == 0);
}

int main(void) {
    test_instrument_validate();
    test_candle_consistency();
    test_quality_flags_distinct();
    TR_TEST_SUMMARY();
}
