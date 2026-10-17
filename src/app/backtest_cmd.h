#ifndef TR_BACKTEST_CMD_H
#define TR_BACKTEST_CMD_H

#include <stddef.h>

/* payload 는 IPC 명령 JSON 전체. 성공 시 0 이고 out 은 결과 JSON.
 * 실패 시 -1 이고 out 은 {"error":"..."} 이다. 실시간 워치는 바꾸지 않는다. */
int tr_backtest_run(const char *payload, char *out, size_t cap);

/* 백테스트 봉은 포인트다. 신호 PriceScale 은 그 종목의 1틱(포인트). */
double tr_bt_price_scale(const char *symbol);

/* IPC 응답은 64KB다. 결과가 그 한도를 넘으면 web/.runtime/backtest-result.json 에 쓰고
 * out 을 {"result_file":true} 로 바꾼다. 작으면 out 을 그대로 둔다. */
int tr_backtest_fit_ipc(char *out, size_t cap);

#endif
