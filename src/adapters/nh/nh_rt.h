#ifndef NH_RT_H
#define NH_RT_H

/* NH 해외 체결 실시간. wss://api.futures.co.kr/trade/ws-stream
 * 구독 action_code FA. 본문 last_pric, exec_qty, ko_trd_dt, ko_exec_tm (2026-10-08 실측).
 * 접근키는 연결 1회용이라 재연결마다 다시 받는다. 토큰과 키는 로그에 남기지 않는다. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adapters/nh/nh_auth.h"

typedef struct nh_rt nh_rt_t;

typedef struct {
    char sym[40];
    uint64_t instrument_id;
    int64_t price_raw; /* 지수 포인트 ×100 */
    int64_t qty;
    int64_t event_time_us;
} nh_rt_tick_t;

nh_rt_t *nh_rt_open(nh_auth_t *auth, char *err, size_t err_cap);
void nh_rt_close(nh_rt_t *rt);

bool nh_rt_subscribe(nh_rt_t *rt, const char *sym, uint64_t instrument_id);
bool nh_rt_unsubscribe(nh_rt_t *rt, const char *sym);

int nh_rt_service(nh_rt_t *rt, int timeout_ms);
bool nh_rt_next(nh_rt_t *rt, nh_rt_tick_t *out);
bool nh_rt_ready(const nh_rt_t *rt);

#endif
