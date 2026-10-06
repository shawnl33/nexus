/* WSF_KSValues1mV1: 해외 18값과 같은 수식. 세션은 BDate/DayIndex로만 갈린다. */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/ks_values_v1.h"

#define DATE 20241001

static tr_ksv_config_t test_cfg(tr_ks_tf_t tf) {
    tr_ksv_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.tf = tf;
    cfg.predict_ticks = 10;
    cfg.predict_bars[0] = 5;
    cfg.predict_bars[1] = 10;
    cfg.predict_bars[2] = 15;
    cfg.predict_bars[3] = 30;
    cfg.predict_bars[4] = 60;
    cfg.min_r2 = 0.4;
    cfg.persist_bars = 2;
    cfg.market_period = 3;
    cfg.min_hold_bars = 2;
    cfg.price_scale = 1.0;
    return cfg;
}

static tr_ksv_input_t make_bar(int i, int64_t bdate, int32_t day_index) {
    tr_ksv_input_t in;
    memset(&in, 0, sizeof(in));
    in.bdate = bdate;
    in.day_index = day_index;
    in.current_bar = i;
    in.bar_open = (tr_time_us_t)i * 60000000;
    in.high = 100.0 + i;
    in.low = 99.0 + i;
    in.close = 100.0 + i;
    in.volume = 100.0;
    return in;
}

static double round_tick(double v) {
    return floor(v / 1.0 + 0.5) * 1.0;
}

static void feed_session(tr_ksv_t *s, int n) {
    for (int i = 1; i <= n; i++) {
        tr_ksv_input_t in = make_bar(i, DATE, i - 1);
        tr_ksv_eval(s, &in);
    }
}

static void test_values_match_references(void) {
    tr_ksv_t s;
    tr_ksv_config_t cfg = test_cfg(TR_KS_TF_1M);
    TR_CHECK(tr_ksv_init(&s, &cfg));

    tr_ksv_input_t in = make_bar(1, DATE, 0);
    tr_ksv_eval(&s, &in);
    TR_CHECK(s.session_reset && s.session_bars == 1 && s.is_new_bar);
    TR_CHECK(s.session.session_no == 1);
    TR_CHECK(s.out.score == 0.0);
    TR_CHECK(s.out.reg_flat == 0.0);
    TR_CHECK(s.out.market_center == 0.0);

    for (int i = 2; i <= 5; i++) {
        in = make_bar(i, DATE, i - 1);
        tr_ksv_eval(&s, &in);
    }
    TR_CHECK(s.session_bars == 5 && !s.session_reset);
    TR_CHECK(s.out.reg_flat == round_tick(104.5));
    TR_CHECK(s.out.score == 3.0);
    double tp3 = (103.0 + 102.0 + 103.0) / 3.0;
    double tp4 = (104.0 + 103.0 + 104.0) / 3.0;
    double tp5 = (105.0 + 104.0 + 105.0) / 3.0;
    double pv = tp5 * 100.0 + tp4 * 100.0 + tp3 * 100.0;
    TR_CHECK(s.out.market_center == pv / 300.0);
    TR_CHECK(s.out.lup_high == 0.0 && s.out.ldn_high == 0.0);
    TR_CHECK(s.out.persist_target[0] == 0.0);

    in = make_bar(6, DATE, 5);
    tr_ksv_eval(&s, &in);
    TR_CHECK(s.out.score == 3.0);
    static const int nh[5] = {5, 10, 15, 30, 60};
    for (int k = 0; k < 5; k++) {
        double mv = 0.5 * (double)nh[k];
        double mx = 1.0 * 1.5 * sqrt(fmax(1.0, (double)nh[k]));
        double move = mv > mx ? mx : mv;
        TR_CHECK(s.out.persist_target[k] == round_tick(105.5 + move));
    }
}

/* 같은 날짜에서 DayIndex만 이어가면 15:30이어도 장이 안 갈린다. 날짜가 바뀌면 리셋. */
static void test_session_follows_bdate_not_clock(void) {
    tr_ksv_t s;
    tr_ksv_config_t cfg = test_cfg(TR_KS_TF_1M);
    tr_ksv_init(&s, &cfg);
    feed_session(&s, 6);
    TR_CHECK(s.out.score == 3.0);
    TR_CHECK(s.session.session_no == 1);

    tr_ksv_input_t in = make_bar(7, DATE, 200);
    tr_ksv_eval(&s, &in);
    TR_CHECK(!s.session_reset && s.session_bars == 7);
    TR_CHECK(s.session.session_no == 1);

    in = make_bar(8, DATE + 1, 0);
    tr_ksv_eval(&s, &in);
    TR_CHECK(s.session_reset && s.session_bars == 1);
    TR_CHECK(s.session.session_no == 2);
    TR_CHECK(s.out.score == 0.0);
    TR_CHECK(s.out.reg_flat == 0.0);
    TR_CHECK(s.out.persist_target[0] == 0.0);
}

static void test_same_bar_restore(void) {
    tr_ksv_t s;
    tr_ksv_config_t cfg = test_cfg(TR_KS_TF_1M);
    tr_ksv_init(&s, &cfg);
    feed_session(&s, 6);
    double targets[5];
    memcpy(targets, s.out.persist_target, sizeof(targets));

    tr_ksv_input_t in = make_bar(6, DATE, 5);
    in.close = 107.0;
    tr_ksv_eval(&s, &in);
    TR_CHECK(s.out.score == 4.0);

    in = make_bar(6, DATE, 5);
    tr_ksv_eval(&s, &in);
    TR_CHECK(s.out.score == 3.0);
    for (int k = 0; k < 5; k++) {
        TR_CHECK(s.out.persist_target[k] == targets[k]);
    }
    TR_CHECK(s.session_bars == 6);
    TR_CHECK(s.session.session_no == 1);
}

static void test_15s_matches_1m_regression_window(void) {
    tr_ksv_t a, b;
    tr_ksv_config_t c1 = test_cfg(TR_KS_TF_1M);
    tr_ksv_config_t c15 = test_cfg(TR_KS_TF_15S);
    TR_CHECK(tr_ksv_init(&a, &c1));
    TR_CHECK(tr_ksv_init(&b, &c15));
    TR_CHECK(a.reg_pred.n == 30 && b.reg_pred.n == 30);
    feed_session(&a, 6);
    feed_session(&b, 6);
    TR_CHECK(a.out.score == b.out.score);
    TR_CHECK(a.out.reg_flat == b.out.reg_flat);
    TR_CHECK(a.out.persist_target[2] == b.out.persist_target[2]);
}

static void test_relink_independence(void) {
    tr_ksv_t a;
    tr_ksv_config_t cfg = test_cfg(TR_KS_TF_1M);
    tr_ksv_init(&a, &cfg);
    feed_session(&a, 6);
    tr_ksv_t b = a;
    TR_CHECK(tr_ksv_relink(&b));
    for (int i = 7; i <= 10; i++) {
        tr_ksv_input_t in = make_bar(i, DATE, i - 1);
        in.high = 500.0;
        tr_ksv_eval(&b, &in);
    }
    tr_ksv_input_t in = make_bar(7, DATE, 6);
    tr_ksv_eval(&a, &in);
    TR_CHECK(a.out.score == 3.0);
    TR_CHECK(a.out.persist_target[0] == round_tick(105.5 + 0.5 * 5.0));
}

int main(void) {
    test_values_match_references();
    test_session_follows_bdate_not_clock();
    test_same_bar_restore();
    test_15s_matches_1m_regression_window();
    test_relink_independence();
    TR_TEST_SUMMARY();
}
