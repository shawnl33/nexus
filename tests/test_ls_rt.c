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
#include "core/model/civil_time.h"

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

static void test_parse_oc0_opt_tick(void) {
    /* KOSPI200 옵션 체결: price/cvolume/chetime은 FC9와 같은 이름 (명세 OC0) */
    const char *msg =
        "{\"header\":{\"tr_cd\":\"OC0\",\"tr_key\":\"B016A745\"},"
        "\"body\":{\"price\":\"2.55\",\"cvolume\":\"3\",\"chetime\":\"090010\"}}";
    ls_rt_event_t ev;
    TR_CHECK(tr_ls_rt_parse_message(msg, strlen(msg), 9, 1790567100000000LL, &ev));
    TR_CHECK(ev.kind == LS_RT_TICK);
    TR_CHECK(ev.price == 255);
    TR_CHECK(ev.qty == 3);
    TR_CHECK(strcmp(ev.tr_cd, "OC0") == 0);
    TR_CHECK(ls_rt_key_pad_width("OC0") == 0);
}

static void test_parse_oh0_opt_orderbook(void) {
    /* KOSPI200 옵션 호가: totbidrem/bidho1은 H1_·FH9와 같은 이름 (명세 OH0) */
    const char *msg =
        "{\"header\":{\"tr_cd\":\"OH0\",\"tr_key\":\"B016A745\"},"
        "\"body\":{\"hotime\":\"090010\",\"totbidrem\":\"40\",\"totofferrem\":\"12\","
        "\"bidho1\":\"2.50\",\"bidrem1\":\"10\",\"offerho1\":\"2.55\",\"offerrem1\":\"4\"}}";
    ls_rt_event_t ev;
    TR_CHECK(tr_ls_rt_parse_message(msg, strlen(msg), 9, 1790567100000000LL, &ev));
    TR_CHECK(ev.kind == LS_RT_ORDERBOOK);
    TR_CHECK(ev.bid_total == 40);
    TR_CHECK(ev.ask_total == 12);
    TR_CHECK(ev.level_count == 1);
    TR_CHECK(ev.levels[0].price == 250);
    TR_CHECK(ev.levels[5].price == 255);
    TR_CHECK(ls_rt_key_pad_width("OH0") == 0);
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

static void test_parse_ovc_tick(void) {
    /* 해외선물 체결 (2026-10-01 ESZ26 실측 그대로): curpr=현재가, trdq=개별 체결량,
     * 시각은 현지(trdtm)가 아니라 한국(kordate+kortm KST) */
    const char *msg =
        "{\"header\":{\"tr_cd\":\"OVC\",\"tr_key\":\"ESZ26   \"},"
        "\"body\":{\"symbol\":\"ESZ26\",\"trdtm\":\"080004\",\"kortm\":\"220004\","
        "\"ovsdate\":\"20261001\",\"kordate\":\"20261001\",\"curpr\":\"7734.75\","
        "\"open\":\"7719.50\",\"high\":\"7767.75\",\"low\":\"7705.00\",\"ydiffpr\":\"19.25\","
        "\"ydiffSign\":\"2\",\"chgrate\":\"0.25\",\"trdq\":\"2\",\"totq\":\"360993\","
        "\"cgubun\":\"-\",\"ovsmkend\":\"20261001\",\"lSeq\":\"60\",\"mdvolume\":\"\",\"msvolume\":\"\"}}";
    ls_rt_event_t ev;
    TR_CHECK(tr_ls_rt_parse_message(msg, strlen(msg), 99, 1790567100000000LL, &ev));
    TR_CHECK(ev.kind == LS_RT_TICK);
    TR_CHECK(ev.instrument_id == 99);
    TR_CHECK(ev.price == 773475); /* ×100 */
    TR_CHECK(ev.qty == 2);        /* trdq = 개별 체결량 (누적 totq가 아니다) */
    TR_CHECK(ev.volume_meaning == TR_TICK_VOLUME_PER_TRADE);
    /* 2026-10-01 22:00:04 KST = 13:00:04 UTC */
    tr_civil_t c = {2026, 10, 1, 13, 0, 4};
    tr_time_us_t expect;
    TR_CHECK(tr_time_us_from_civil(&c, 0, &expect));
    TR_CHECK(ev.event_time_us == expect);
    TR_CHECK(strcmp(ev.tr_cd, "OVC") == 0);
}

static void test_parse_ovh_orderbook(void) {
    /* 해외선물 호가 (2026-10-01 ESZ26 실측 필드): 총잔량 totbidrem/totofferrem, 5단계.
     * hotime은 거래소 현지 시각이라 수신 시각을 이벤트 시각으로 쓴다 */
    const char *msg =
        "{\"header\":{\"tr_cd\":\"OVH\",\"tr_key\":\"ESZ26   \"},"
        "\"body\":{\"symbol\":\"ESZ26\",\"hotime\":\"083301\","
        "\"offerho1\":\"7729.50\",\"offerrem1\":\"36\",\"offerno1\":\"22\","
        "\"offerho2\":\"7729.75\",\"offerrem2\":\"41\",\"offerno2\":\"25\","
        "\"offerho3\":\"7730.00\",\"offerrem3\":\"55\",\"offerno3\":\"30\","
        "\"offerho4\":\"7730.25\",\"offerrem4\":\"60\",\"offerno4\":\"31\","
        "\"offerho5\":\"7730.50\",\"offerrem5\":\"30\",\"offerno5\":\"34\","
        "\"bidho1\":\"7729.25\",\"bidrem1\":\"21\",\"bidno1\":\"15\","
        "\"bidho2\":\"7729.00\",\"bidrem2\":\"18\",\"bidno2\":\"12\","
        "\"bidho3\":\"7728.75\",\"bidrem3\":\"25\",\"bidno3\":\"14\","
        "\"bidho4\":\"7728.50\",\"bidrem4\":\"30\",\"bidno4\":\"16\","
        "\"bidho5\":\"7728.25\",\"bidrem5\":\"40\",\"bidno5\":\"18\","
        "\"totofferrem\":\"222\",\"totbidrem\":\"181\",\"totoffercnt\":\"141\",\"totbidcnt\":\"127\"}}";
    int64_t recv = 1790567100000000LL;
    ls_rt_event_t ev;
    TR_CHECK(tr_ls_rt_parse_message(msg, strlen(msg), 99, recv, &ev));
    TR_CHECK(ev.kind == LS_RT_ORDERBOOK);
    TR_CHECK(ev.bid_total == 181);
    TR_CHECK(ev.ask_total == 222);
    TR_CHECK(ev.level_count == 5);
    TR_CHECK(ev.levels[0].price == 772925 && ev.levels[0].qty == 21);  /* 매수 1단계 */
    TR_CHECK(ev.levels[4].price == 772825 && ev.levels[4].qty == 40);  /* 매수 5단계 */
    TR_CHECK(ev.levels[5].price == 772950 && ev.levels[5].qty == 36);  /* 매도 1단계 */
    TR_CHECK(ev.levels[9].price == 773050 && ev.levels[9].qty == 30);  /* 매도 5단계 */
    TR_CHECK(ev.event_time_us == recv); /* hotime은 현지 시각 — KST 변환 불가, 수신 시각 */
}

static void test_key_pad_width(void) {
    /* 해외선물 채널은 8자리 고정 (미패딩 시 rsp_cd 10009, 2026-10-01 실측),
     * 통합 채널은 10자리 고정, 국내 채널은 패딩 없음 */
    TR_CHECK(ls_rt_key_pad_width("OVC") == 8);
    TR_CHECK(ls_rt_key_pad_width("OVH") == 8);
    TR_CHECK(ls_rt_key_pad_width("WOC") == 8);
    TR_CHECK(ls_rt_key_pad_width("WOH") == 8);
    TR_CHECK(ls_rt_key_pad_width("US3") == 10);
    TR_CHECK(ls_rt_key_pad_width("UH1") == 10);
    TR_CHECK(ls_rt_key_pad_width("S3_") == 0);
    TR_CHECK(ls_rt_key_pad_width("H1_") == 0);
    TR_CHECK(ls_rt_key_pad_width("FC9") == 0);
    TR_CHECK(ls_rt_key_pad_width(0) == 0);
}

static void test_parse_rejects(void) {
    ls_rt_event_t ev;
    TR_CHECK(!tr_ls_rt_parse_message("{broken", 7, 1, 0, &ev));
    TR_CHECK(!tr_ls_rt_parse_message("{\"header\":{}}", 12, 1, 0, &ev));
    const char *unknown = "{\"header\":{\"tr_cd\":\"NWS\",\"tr_key\":\"x\"},\"body\":{}}";
    TR_CHECK(!tr_ls_rt_parse_message(unknown, strlen(unknown), 1, 0, &ev));
}

static void test_retry_backoff(void) {
    const int min_ms = 1000, max_ms = 15000;
    /* 콜백(ls_realtime.c CLOSED/CONNECTION_ERROR)의 저장값 갱신과 동일한 규칙 */
    const int expect[] = {1000, 2000, 4000, 8000, 15000, 15000};

    /* 첫 실패(수립 전): min에서 시작 */
    TR_CHECK(ls_rt_next_retry_ms(-1, min_ms, min_ms, max_ms) == min_ms);

    /* 수립 없이 실패 반복(-1): cap까지 doubling */
    int retry = min_ms;
    for (int i = 0; i < 6; i++) {
        int delay = ls_rt_next_retry_ms(-1, retry, min_ms, max_ms);
        TR_CHECK(delay == expect[i]);
        retry = delay;
        if (retry < max_ms) {
            retry *= 2;
        }
    }

    /* 즉시 단절(수립 30ms 후 CLOSED) 반복: 리셋 없이 cap까지 doubling */
    retry = min_ms;
    for (int i = 0; i < 6; i++) {
        int delay = ls_rt_next_retry_ms(30, retry, min_ms, max_ms);
        TR_CHECK(delay == expect[i]);
        retry = delay;
        if (retry < max_ms) {
            retry *= 2;
        }
    }

    /* 건강 기준 미만(9999ms) 세션: 리셋 안 함 */
    TR_CHECK(ls_rt_next_retry_ms(9999, 8000, min_ms, max_ms) == 8000);

    /* 건강한 세션(>=10s) 이후 단절: min으로 리셋 (빠른 복구) */
    TR_CHECK(ls_rt_next_retry_ms(10000, 15000, min_ms, max_ms) == min_ms);
    TR_CHECK(ls_rt_next_retry_ms(60000, 8000, min_ms, max_ms) == min_ms);

    /* cap 초과 저장값은 cap으로 클램프 */
    TR_CHECK(ls_rt_next_retry_ms(-1, 30000, min_ms, max_ms) == max_ms);
}

static void test_consec_short_counter(void) {
    int consec = 0;
    /* 건강한 세션 이후 단절: 카운터 리셋 */
    ls_rt_note_session_end(true, &consec);
    TR_CHECK(consec == 0);
    /* 단기 세션(수립 실패 포함): +1씩 누적 */
    ls_rt_note_session_end(false, &consec);
    TR_CHECK(consec == 1);
    ls_rt_note_session_end(false, &consec);
    TR_CHECK(consec == 2);
    /* 3회째에 재발급 트리거 조건 도달 (아직 시도한 적 없음: last=0) */
    ls_rt_note_session_end(false, &consec);
    TR_CHECK(consec == LS_RT_REAUTH_THRESHOLD);
    TR_CHECK(ls_rt_should_reauth(consec, 0, 1000000000LL));
    /* 임계값 초과 후에도 계속 누적 (재발급 실패 시 카운터 유지 경로) */
    ls_rt_note_session_end(false, &consec);
    TR_CHECK(consec == LS_RT_REAUTH_THRESHOLD + 1);
    /* 건강한 세션 하나가 끼면 리셋 → 트리거 해제 */
    ls_rt_note_session_end(true, &consec);
    TR_CHECK(consec == 0);
    TR_CHECK(!ls_rt_should_reauth(consec, 0, 1000000000LL));
}

static void test_reauth_cooldown(void) {
    const int64_t t0 = 1790800000000000LL; /* 임의 기준 시각 */
    /* 임계값 미만이면 시각과 무관하게 트리거 안 됨 */
    TR_CHECK(!ls_rt_should_reauth(0, 0, t0));
    TR_CHECK(!ls_rt_should_reauth(LS_RT_REAUTH_THRESHOLD - 1, 0, t0));
    /* 첫 시도(last=0)는 즉시 허용 */
    TR_CHECK(ls_rt_should_reauth(LS_RT_REAUTH_THRESHOLD, 0, t0));
    /* 직전 시도 후 쿨다운 내에는 재시도 안 함 (재발급 실패·카운터 유지 케이스) */
    TR_CHECK(!ls_rt_should_reauth(LS_RT_REAUTH_THRESHOLD, t0, t0));
    TR_CHECK(!ls_rt_should_reauth(LS_RT_REAUTH_THRESHOLD, t0, t0 + LS_RT_REAUTH_COOLDOWN_US - 1));
    /* 쿨다운 경과 후에는 다시 시도 */
    TR_CHECK(ls_rt_should_reauth(LS_RT_REAUTH_THRESHOLD, t0, t0 + LS_RT_REAUTH_COOLDOWN_US));
    TR_CHECK(ls_rt_should_reauth(LS_RT_REAUTH_THRESHOLD + 2, t0, t0 + LS_RT_REAUTH_COOLDOWN_US));
}

static void test_sub_ack_rejected(void) {
    /* 정상 ACK: rsp_cd "00000" → 조용히 통과 */
    const char *ok =
        "{\"header\":{\"tr_cd\":\"S3_\",\"tr_key\":\"005930\",\"rsp_cd\":\"00000\",\"rsp_msg\":\"정상처리\"}}";
    TR_CHECK(!ls_rt_sub_ack_rejected(ok, strlen(ok)));
    /* 거절 ACK(토큰 무효 등): rsp_cd가 "00000"이 아니면 감지 */
    const char *rej =
        "{\"header\":{\"tr_cd\":\"S3_\",\"tr_key\":\"005930\",\"rsp_cd\":\"IGW00121\",\"rsp_msg\":\"유효하지 않은 token 입니다\"}}";
    TR_CHECK(ls_rt_sub_ack_rejected(rej, strlen(rej)));
    /* rsp_cd가 없거나 JSON이 아니면 판정 보류 → 거절 아님 */
    const char *nocd = "{\"header\":{\"tr_cd\":\"S3_\",\"tr_key\":\"005930\"}}";
    TR_CHECK(!ls_rt_sub_ack_rejected(nocd, strlen(nocd)));
    TR_CHECK(!ls_rt_sub_ack_rejected("{broken", 7));
}

int main(void) {
    test_parse_s3_tick();
    test_parse_fut_tick();
    test_parse_oc0_opt_tick();
    test_parse_oh0_opt_orderbook();
    test_parse_us3_tick();
    test_parse_dc0_night_fut_tick();
    test_parse_orderbook();
    test_parse_uh1_orderbook();
    test_parse_dh0_night_fut_orderbook();
    test_parse_ovc_tick();
    test_parse_ovh_orderbook();
    test_key_pad_width();
    test_parse_rejects();
    test_retry_backoff();
    test_consec_short_counter();
    test_reauth_cooldown();
    test_sub_ack_rejected();
    TR_TEST_SUMMARY();
}
