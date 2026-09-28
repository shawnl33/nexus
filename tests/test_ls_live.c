/* LS 실제 API 통합 테스트 (라이브).
 *
 * 환경변수 LS_APP_KEY/LS_SECRET_KEY가 있을 때만 실제 호출한다.
 * 없으면 77을 반환해 CTest에서 'skipped'으로 처리된다.
 * 네트워크·실제 계정 키를 쓰므로 기본 개발 루프에는 포함하지 않는다.
 */

#include <stdio.h>
#include <string.h>

#include "adapters/ls/ls_auth.h"
#include "adapters/ls/ls_chart.h"
#include "adapters/ls/ls_http.h"

#define CAP 8
static tr_candle_t g_bars[CAP];

int main(void) {
    if (!ls_auth_keys_present()) {
        fprintf(stderr, "LS_APP_KEY/LS_SECRET_KEY not set: skipping live test\n");
        return 77;
    }
    ls_auth_t auth;
    ls_auth_init(&auth, 0);
    const char *token = 0;
    if (!ls_auth_ensure(&auth, &token)) {
        fprintf(stderr, "auth failed: %s\n", auth.last_error);
        return 1;
    }
    printf("auth OK (token_len=%zu)\n", strlen(token));

    ls_chart_page_t page;
    char err[128] = {0};

    int rc = ls_chart_fetch_minute(&auth, LS_CHART_STOCK_MIN, "005930", 1, 3, "99999999", " ",
                                   " ", " ", 1, 7, g_bars, CAP, &page, err, sizeof(err));
    if (rc != LS_HTTP_OK) {
        fprintf(stderr, "stock chart failed rc=%d: %s\n", rc, err);
        return 1;
    }
    printf("stock 005930: %zu bars, cts=%s %s, session %lld~%lld\n",
           page.count, page.cts_date, page.cts_time,
           (long long)page.session_open_min, (long long)page.session_close_min);
    if (page.count == 0) {
        return 1;
    }

    rc = ls_chart_fetch_minute(&auth, LS_CHART_FUT_MIN, "A016C000", 1, 3, "99999999", " ",
                               " ", " ", 2, 7, g_bars, CAP, &page, err, sizeof(err));
    if (rc != LS_HTTP_OK) {
        fprintf(stderr, "futures chart failed rc=%d: %s\n", rc, err);
        return 1;
    }
    printf("futures A016C000: %zu bars, session %lld~%lld\n",
           page.count, (long long)page.session_open_min, (long long)page.session_close_min);
    if (page.count == 0) {
        return 1;
    }

    printf("live test OK\n");
    return 0;
}
