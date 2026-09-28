/* LS 실시간 어댑터 파서 단위 테스트 (네트워크 불필요, 결정적).
 *
 * 연결·구독·재연결의 통합 검증은 test_ls_rt_live.c가 실제 LS 서버로 수행한다.
 * 루프백 스텁 서버 방식은 lws 4.3.5가 lws_service의 timeout 인자를 무시하는
 * upstream 동작 때문에 비결정적이었다 (2026-09-28 규명, ls_realtime.c의
 * service_bounded 참조). 라이브 검증으로 대체하고 파서 검증만 유지한다.
 */

#include "test_util.h"

#include <string.h>

#include "adapters/ls/ls_realtime.h"

static void test_parse_s3_tick(void) {
    const char *msg =
        "{\"header\":{\"tr_cd\":\"S3_\",\"tr_key\":\"005930\"},"
        "\"body\":{\"price\":\"272500\",\"cvolume\":\"10\",\"chetime\":\"124500\",\"mdvolume\":\"1000000\"}}";
    ls_rt_event_t ev;
    int64_t recv = 1790567100000000LL; /* 2026-09-28 03:45 UTC */
    TR_CHECK(tr_ls_rt_parse_message(msg, strlen(msg), 42, recv, &ev));
    TR_CHECK(ev.kind == LS_RT_TICK);
    TR_CHECK(ev.instrument_id == 42);
    TR_CHECK(ev.price == 27250000); /* ×100 */
    TR_CHECK(ev.qty == 10);
    TR_CHECK(ev.volume_meaning == TR_TICK_VOLUME_PER_TRADE);
    TR_CHECK(ev.event_time_us == recv);
    TR_CHECK(strcmp(ev.tr_cd, "S3_") == 0);
}

static void test_parse_fut_tick(void) {
    const char *msg =
        "{\"header\":{\"tr_cd\":\"FC9\",\"tr_key\":\"A016C000\"},"
        "\"body\":{\"price\":\"1094.05\",\"cvolume\":\"3\",\"chetime\":\"090010\"}}";
    ls_rt_event_t ev;
    TR_CHECK(tr_ls_rt_parse_message(msg, strlen(msg), 7, 1790567100000000LL, &ev));
    TR_CHECK(ev.kind == LS_RT_TICK);
    TR_CHECK(ev.price == 109405);
    TR_CHECK(ev.qty == 3);
}

static void test_parse_orderbook(void) {
    /* 2026-09-28 H1_ 실측 필드 구조 */
    const char *msg =
        "{\"header\":{\"tr_cd\":\"H1_\",\"tr_key\":\"005930\"},"
        "\"body\":{\"totbidrem\":\"1257710\",\"totofferrem\":\"306757\",\"hotime\":\"142351\","
        "\"bidho1\":\"272500\",\"bidrem1\":\"1000\",\"offerho1\":\"273000\",\"offerrem1\":\"900\"}}";
    ls_rt_event_t ev;
    TR_CHECK(tr_ls_rt_parse_message(msg, strlen(msg), 42, 1790567100000000LL, &ev));
    TR_CHECK(ev.kind == LS_RT_ORDERBOOK);
    TR_CHECK(ev.bid_total == 1257710);
    TR_CHECK(ev.ask_total == 306757);
    TR_CHECK(ev.level_count == 1);
    TR_CHECK(ev.levels[0].price == 27250000);
    TR_CHECK(ev.levels[5].price == 27300000);
}

static void test_parse_us3_tick(void) {
    /* 통합(KRX+NXT) 체결: 필드명은 S3_와 같고 수치 필드는 Number 타입 (공식 명세) */
    const char *msg =
        "{\"header\":{\"tr_cd\":\"US3\",\"tr_key\":\"005930    \"},"
        "\"body\":{\"price\":272500,\"cvolume\":10,\"chetime\":\"162005\",\"mdvolume\":1000000}}";
    ls_rt_event_t ev;
    TR_CHECK(tr_ls_rt_parse_message(msg, strlen(msg), 42, 1790567100000000LL, &ev));
    TR_CHECK(ev.kind == LS_RT_TICK);
    TR_CHECK(ev.price == 27250000);
    TR_CHECK(ev.qty == 10);
    TR_CHECK(ev.volume_meaning == TR_TICK_VOLUME_PER_TRADE);
    TR_CHECK(strcmp(ev.tr_cd, "US3") == 0);
}

static void test_parse_uh1_orderbook(void) {
    /* 통합 호가: 총잔량·단계 잔량은 unt_ 접두사, 가격 단계는 무접두사 (공식 명세) */
    const char *msg =
        "{\"header\":{\"tr_cd\":\"UH1\",\"tr_key\":\"005930    \"},"
        "\"body\":{\"hotime\":\"162005\",\"unt_totbidrem\":\"1257710\",\"unt_totofferrem\":\"306757\","
        "\"bidho1\":\"272500\",\"unt_bidrem1\":\"1000\",\"offerho1\":\"273000\",\"unt_offerrem1\":\"900\"}}";
    ls_rt_event_t ev;
    TR_CHECK(tr_ls_rt_parse_message(msg, strlen(msg), 42, 1790567100000000LL, &ev));
    TR_CHECK(ev.kind == LS_RT_ORDERBOOK);
    TR_CHECK(ev.bid_total == 1257710);
    TR_CHECK(ev.ask_total == 306757);
    TR_CHECK(ev.level_count == 1);
    TR_CHECK(ev.levels[0].price == 27250000);
    TR_CHECK(ev.levels[5].price == 27300000);
}

static void test_parse_dc0_night_fut_tick(void) {
    /* KRX야간파생 체결: price/cvolume/chetime은 FC9와 같은 이름 (공식 명세, 2026-09-28 실측) */
    const char *msg =
        "{\"header\":{\"tr_cd\":\"DC0\",\"tr_key\":\"A016C000\"},"
        "\"body\":{\"date\":\"20260928\",\"price\":\"1087.15\",\"cvolume\":\"1\",\"chetime\":\"201505\"}}";
    ls_rt_event_t ev;
    TR_CHECK(tr_ls_rt_parse_message(msg, strlen(msg), 7, 1790567100000000LL, &ev));
    TR_CHECK(ev.kind == LS_RT_TICK);
    TR_CHECK(ev.price == 108715);
    TR_CHECK(ev.qty == 1);
    TR_CHECK(strcmp(ev.tr_cd, "DC0") == 0);
}

static void test_parse_dh0_night_fut_orderbook(void) {
    /* KRX야간파생 호가: 총잔량 totbidrem/totofferrem (H1_과 같은 이름, 공식 명세) */
    const char *msg =
        "{\"header\":{\"tr_cd\":\"DH0\",\"tr_key\":\"A016C000\"},"
        "\"body\":{\"hotime\":\"201505\",\"totbidrem\":\"1234\",\"totofferrem\":\"987\","
        "\"bidho1\":\"1087.00\",\"bidrem1\":\"10\",\"offerho1\":\"1087.15\",\"offerrem1\":\"5\"}}";
    ls_rt_event_t ev;
    TR_CHECK(tr_ls_rt_parse_message(msg, strlen(msg), 7, 1790567100000000LL, &ev));
    TR_CHECK(ev.kind == LS_RT_ORDERBOOK);
    TR_CHECK(ev.bid_total == 1234);
    TR_CHECK(ev.ask_total == 987);
    TR_CHECK(ev.level_count == 1);
    TR_CHECK(ev.levels[0].price == 108700);
    TR_CHECK(ev.levels[5].price == 108715);
}

static void test_parse_rejects(void) {
    ls_rt_event_t ev;
    TR_CHECK(!tr_ls_rt_parse_message("{broken", 7, 1, 0, &ev));
    TR_CHECK(!tr_ls_rt_parse_message("{\"header\":{}}", 12, 1, 0, &ev));
    const char *unknown = "{\"header\":{\"tr_cd\":\"NWS\",\"tr_key\":\"x\"},\"body\":{}}";
    TR_CHECK(!tr_ls_rt_parse_message(unknown, strlen(unknown), 1, 0, &ev));
}

int main(void) {
    test_parse_s3_tick();
    test_parse_fut_tick();
    test_parse_us3_tick();
    test_parse_dc0_night_fut_tick();
    test_parse_orderbook();
    test_parse_uh1_orderbook();
    test_parse_dh0_night_fut_orderbook();
    test_parse_rejects();
    TR_TEST_SUMMARY();
}
