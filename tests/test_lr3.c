/* WSF_Mtf_LinRegV3 포팅 테스트: 새 봉 시프트/같은 봉 갱신, 워밍업, 세션 리셋, V4 연계 */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/linreg_v3.h"

static void make_eval(tr_ind_eval_t *ev, tr_candle_t *bar, int64_t open_us,
                      double high, double low, double close, bool session_first) {
    memset(bar, 0, sizeof(*bar));
    bar->instrument_id = 1;
    bar->timeframe_sec = 60;
    bar->open_time_us = open_us;
    bar->close_time_us = open_us + 60000000;
    bar->open = close;
    bar->high = high;
    bar->low = low;
    bar->close = close;
    bar->state = TR_CANDLE_OPEN;

    memset(ev, 0, sizeof(*ev));
    ev->bar = bar;
    ev->event_time_us = open_us;
    ev->trading_day = 100;
    ev->is_new_bar = false; /* V3는 open_time으로 자체 판정 */
    ev->is_session_first = session_first;
    ev->compress = TR_COMPRESS_MIN;
    ev->bar_interval = 1;
}

/* i번째 봉(1부터) 중간값 = 98 + 2i 인 완전 직선 */
static void feed_bar(tr_lr3_t *s, tr_ind_eval_t *ev, tr_candle_t *bar, int i, bool session_first) {
    double mid = 98.0 + 2.0 * i;
    make_eval(ev, bar, (int64_t)i * 60000000, mid + 1.0, mid - 1.0, mid, session_first);
    tr_lr3_eval(s, ev);
}

static void test_warmup_and_valid(void) {
    tr_lr3_t s;
    TR_CHECK(tr_lr3_init(&s, TR_COMPRESS_MIN, 1, 5, 10, 15));
    TR_CHECK(s.n == 30); /* 1분 이하 분봉 → 회귀기간 30 */

    tr_ind_eval_t ev;
    tr_candle_t bar;
    for (int i = 1; i <= 4; i++) {
        feed_bar(&s, &ev, &bar, i, false);
        TR_CHECK(!s.reg_valid); /* 최소 표본 5 미만 */
        TR_CHECK(s.validity == TR_VALIDITY_MISSING);
    }
    feed_bar(&s, &ev, &bar, 5, false);
    TR_CHECK(s.reg_valid);
    TR_CHECK(s.validity == TR_VALIDITY_VALID);
    TR_CHECK(fabs(s.slope - 2.0) < 1e-9);
    TR_CHECK(fabs(s.r2 - 1.0) < 1e-9);
    TR_CHECK(fabs(s.line - 108.0) < 1e-9); /* 5번째 봉 중간값 = 108 */
    TR_CHECK(s.line_sign == 1);
    TR_CHECK(s.v4.valid_out); /* V4 낶부 연계 */
}

static void test_same_bar_update_no_shift(void) {
    tr_lr3_t s;
    tr_lr3_init(&s, TR_COMPRESS_MIN, 1, 5, 10, 15);
    tr_ind_eval_t ev;
    tr_candle_t bar;
    for (int i = 1; i <= 5; i++) {
        feed_bar(&s, &ev, &bar, i, false);
    }
    size_t before = ylv_count(&s.prices);

    /* 같은 봉(open_time 동일) 재평가: 시프트 없이 [0]만 갱신 */
    make_eval(&ev, &bar, 5 * 60000000, 200.0, 190.0, 195.0, false);
    tr_lr3_eval(&s, &ev);
    TR_CHECK(ylv_count(&s.prices) == before);
    double p0 = 0.0;
    TR_CHECK(ylv_at(&s.prices, 0, &p0) && fabs(p0 - 195.0) < 1e-9);
}

static void test_session_reset(void) {
    tr_lr3_t s;
    tr_lr3_init(&s, TR_COMPRESS_MIN, 1, 5, 10, 15);
    tr_ind_eval_t ev;
    tr_candle_t bar;
    for (int i = 1; i <= 6; i++) {
        feed_bar(&s, &ev, &bar, i, false);
    }
    TR_CHECK(s.reg_valid);

    /* 당일 첫 봉: 배열·유효개수 리셋 → 다시 워밍업 */
    feed_bar(&s, &ev, &bar, 7, true);
    TR_CHECK(!s.reg_valid);
    TR_CHECK(ylv_count(&s.prices) == 1);
    for (int i = 8; i <= 10; i++) {
        feed_bar(&s, &ev, &bar, i, false);
        TR_CHECK(!s.reg_valid); /* 유효개수 2~4 */
    }
    feed_bar(&s, &ev, &bar, 11, false); /* 유효개수 5 → 다시 유효 */
    TR_CHECK(s.reg_valid);
    TR_CHECK(fabs(s.slope - 2.0) < 1e-9);
}

static void test_n_selection(void) {
    tr_lr3_t s;
    tr_lr3_init(&s, TR_COMPRESS_MIN, 5, 5, 10, 15);
    TR_CHECK(s.n == 18);
    tr_lr3_init(&s, TR_COMPRESS_MIN, 15, 5, 10, 15);
    TR_CHECK(s.n == 10);
    tr_lr3_init(&s, TR_COMPRESS_MIN, 30, 5, 10, 15);
    TR_CHECK(s.n == 6);
    tr_lr3_init(&s, TR_COMPRESS_MIN, 60, 5, 10, 15);
    TR_CHECK(s.n == 12);
    tr_lr3_init(&s, TR_COMPRESS_DAY, 1, 5, 10, 15);
    TR_CHECK(s.n == 20);
}

int main(void) {
    test_warmup_and_valid();
    test_same_bar_update_no_shift();
    test_session_reset();
    test_n_selection();
    TR_TEST_SUMMARY();
}
