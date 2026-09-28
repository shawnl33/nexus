/* LS 실시간 어댑터 라이브 테스트: 실제 LS WebSocket 서버로 구독·수신 검증.
 *
 * 환경변수 LS_APP_KEY/LS_SECRET_KEY가 있을 때만 실제 접속한다 (없으면 77로 skip).
 * 장중(한국 시각 09:00~15:30 KST 평일)이 아니면 틱이 오지 않을 수 있어,
 * 그 경우도 어댑터 연결·구독 ACK 상태까지만 확인하고 skip 한다.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "adapters/ls/ls_auth.h"
#include "adapters/ls/ls_realtime.h"

int main(void) {
    if (!ls_auth_keys_present()) {
        fprintf(stderr, "LS_APP_KEY/LS_SECRET_KEY not set: skipping live rt test\n");
        return 77;
    }
    ls_auth_t auth;
    ls_auth_init(&auth, 0);
    const char *token = 0;
    if (!ls_auth_ensure(&auth, &token)) {
        fprintf(stderr, "auth failed: %s\n", auth.last_error);
        return 1;
    }

    ls_rt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.auth = &auth;
    cfg.queue_capacity = 64;
    cfg.reconnect_min_ms = 1000;
    cfg.reconnect_max_ms = 5000;

    char err[128] = {0};
    tr_ls_rt_t *rt = tr_ls_rt_open(&cfg, err, sizeof(err));
    if (rt == 0) {
        fprintf(stderr, "rt open failed: %s\n", err);
        return 1;
    }
    if (!tr_ls_rt_subscribe(rt, "S3_", "005930", 1)) {
        fprintf(stderr, "subscribe failed\n");
        return 1;
    }

    int64_t deadline = (int64_t)time(0) * 1000000LL + 20000000LL; /* 최대 20초 */
    int ticks = 0;
    ls_rt_event_t ev;
    while ((int64_t)time(0) * 1000000LL < deadline && ticks < 3) {
        tr_ls_rt_service(rt, 100);
        while (tr_ls_rt_next(rt, &ev)) {
            if (ev.kind == LS_RT_TICK) {
                ticks++;
                printf("tick %d: tr_cd=%s inst=%llu price=%lld qty=%lld\n",
                       ticks, ev.tr_cd, (unsigned long long)ev.instrument_id,
                       (long long)ev.price, (long long)ev.qty);
            }
        }
    }

    ls_rt_state_t st = tr_ls_rt_state(rt);
    printf("state=%d ticks=%d reconnects=%llu dropped=%llu\n",
           st, ticks, (unsigned long long)tr_ls_rt_reconnect_count(rt),
           (unsigned long long)tr_ls_rt_queue_dropped(rt));
    tr_ls_rt_close(rt);

    if (st != LS_RT_READY) {
        fprintf(stderr, "did not reach READY\n");
        return 1;
    }
    if (ticks == 0) {
        fprintf(stderr, "no ticks (market closed?): treated as skip\n");
        return 77;
    }
    printf("live rt test OK\n");
    return 0;
}
