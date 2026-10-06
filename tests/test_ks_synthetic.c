/* WSF_KSSynthetic1mV1 / 15초: 장 시작(DayIndex) 기준 버킷. */

#include "test_util.h"

#include <string.h>

#include "core/functions/ks_synthetic_v1.h"

#define DATE 20241001

static int64_t t_at_min(int m) {
    int h = 9 + m / 60, mm = m % 60;
    return (int64_t)h * 10000 + (int64_t)mm * 100;
}

static int64_t t_at_15s(int slot) {
    int sec = 9 * 3600 + slot * 15;
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    int s = sec % 60;
    return (int64_t)h * 10000 + (int64_t)m * 100 + s;
}

static tr_kssyn_input_t bar_min(int cur_m, int current_bar, int64_t bdate, bool has_prev) {
    tr_kssyn_input_t in;
    memset(&in, 0, sizeof(in));
    in.bdate = bdate;
    in.day_index = cur_m;
    in.current_bar = current_bar;
    in.cur_time = t_at_min(cur_m);
    in.is_new_bar = true;
    in.has_prev = has_prev;
    if (has_prev) {
        in.prev_time = t_at_min(cur_m - 1);
        int prev = cur_m - 1;
        in.prev_h = 101.0 + prev;
        in.prev_l = 99.0 + prev;
        in.prev_c = 100.0 + prev;
        in.prev_v = 100.0;
    }
    return in;
}

static void test_completion_and_lines(void) {
    tr_kssyn_t s;
    tr_kssyn_config_t cfg = {5, 5, 2, 0, 1.0, 60};
    TR_CHECK(tr_kssyn_init(&s, &cfg));

    tr_kssyn_input_t open = bar_min(0, 1, DATE, false);
    tr_kssyn_eval(&s, &open);
    TR_CHECK(ylv_count(&s.mids) == 0);

    for (int m = 1; m <= 25; m++) {
        tr_kssyn_input_t in = bar_min(m, m + 1, DATE, true);
        tr_kssyn_eval(&s, &in);
    }
    TR_CHECK(ylv_count(&s.mids) == 5);
    TR_CHECK(s.out_reg_valid == 1);
    TR_CHECK(s.out_reg == 122.0);
    TR_CHECK(s.out_mkt_valid == 1);
    TR_CHECK(s.out_mkt == ((353.0 / 3.0) * 500.0 + (368.0 / 3.0) * 500.0) / 1000.0);
}

static void test_gap_and_session_reset(void) {
    tr_kssyn_t s;
    tr_kssyn_config_t cfg = {5, 5, 2, 0, 1.0, 60};
    tr_kssyn_init(&s, &cfg);
    tr_kssyn_input_t open = bar_min(0, 1, DATE, false);
    tr_kssyn_eval(&s, &open);
    for (int m = 1; m <= 10; m++) {
        tr_kssyn_input_t in = bar_min(m, m + 1, DATE, true);
        tr_kssyn_eval(&s, &in);
    }
    TR_CHECK(ylv_count(&s.mids) == 2);

    /* 09:11, 09:12를 건너뛰고 09:13의 직전을 09:12가 아닌 09:10 다음으로 넣지 않는다. */
    tr_kssyn_input_t gap = bar_min(13, 14, DATE, true);
    gap.prev_time = t_at_min(12);
    gap.prev_h = 113.0;
    gap.prev_l = 111.0;
    gap.prev_c = 112.0;
    tr_kssyn_eval(&s, &gap);
    TR_CHECK(ylv_count(&s.mids) == 2);

    tr_kssyn_input_t next = bar_min(0, 15, DATE + 1, true);
    tr_kssyn_eval(&s, &next);
    TR_CHECK(s.session.session_no == 2);
    TR_CHECK(ylv_count(&s.mids) == 0);
    TR_CHECK(s.out_reg_valid == 0 && s.out_mkt_valid == 0);
}

static void test_same_bar_does_not_double_count(void) {
    tr_kssyn_t s;
    tr_kssyn_config_t cfg = {5, 5, 2, 0, 1.0, 60};
    tr_kssyn_init(&s, &cfg);
    tr_kssyn_input_t open = bar_min(0, 1, DATE, false);
    tr_kssyn_eval(&s, &open);
    tr_kssyn_input_t again = open;
    again.is_new_bar = false;
    tr_kssyn_eval(&s, &again);
    TR_CHECK(s.session.session_no == 1);
    TR_CHECK(s.agg_count == 0);
}

/* 15초 5분 합성은 20슬롯. 슬롯 0을 장 시작으로 두고 1..20을 넣으면 1개 완성. */
static void test_15s_bucket_is_four_times_minutes(void) {
    tr_kssyn_t s;
    tr_kssyn_config_t cfg = {5, 5, 2, 0, 1.0, 15};
    TR_CHECK(tr_kssyn_init(&s, &cfg));
    TR_CHECK(s.synth_bars == 20);

    tr_kssyn_input_t open;
    memset(&open, 0, sizeof(open));
    open.bdate = DATE;
    open.day_index = 0;
    open.current_bar = 1;
    open.cur_time = t_at_15s(0);
    open.is_new_bar = true;
    tr_kssyn_eval(&s, &open);

    for (int slot = 1; slot <= 20; slot++) {
        tr_kssyn_input_t in;
        memset(&in, 0, sizeof(in));
        in.bdate = DATE;
        in.day_index = slot;
        in.current_bar = slot + 1;
        in.cur_time = t_at_15s(slot);
        in.is_new_bar = true;
        in.has_prev = true;
        in.prev_time = t_at_15s(slot - 1);
        in.prev_h = 101.0;
        in.prev_l = 99.0;
        in.prev_c = 100.0;
        in.prev_v = 10.0;
        tr_kssyn_eval(&s, &in);
    }
    TR_CHECK(ylv_count(&s.mids) == 1);
    double mid = 0.0;
    TR_CHECK(ylv_at(&s.mids, 0, &mid) && mid == 100.0);
    TR_CHECK(s.out_reg_valid == 0);
}

int main(void) {
    test_completion_and_lines();
    test_gap_and_session_reset();
    test_same_bar_does_not_double_count();
    test_15s_bucket_is_four_times_minutes();
    TR_TEST_SUMMARY();
}
