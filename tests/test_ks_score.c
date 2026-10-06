/* 국내 1분 점수 블록: 삼선·오선 비율, 합성 회귀·마켓 차, 두 구간 가격비율.
 * 같은 봉은 직전 봉 말에서 다시 계산한다. */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/ks_score_v1.h"

#define DATE 20241001

static int64_t t_at_min(int m) {
    int h = 9 + m / 60;
    int mm = m % 60;
    return (int64_t)h * 10000 + (int64_t)mm * 100;
}

static tr_ksscore_config_t fast_cfg(int include_mkt_30) {
    tr_ksscore_config_t cfg;
    tr_ksscore_default_config(&cfg, 1.0);
    cfg.values.persist_bars = 2;
    cfg.values.market_period = 2;
    cfg.values.min_hold_bars = 2;
    cfg.reg_period_5 = 5;
    cfg.reg_period_15 = 5;
    cfg.reg_period_30 = 5;
    cfg.include_mkt_30 = include_mkt_30;
    return cfg;
}

static tr_ksscore_input_t make_bar(int32_t current_bar, int64_t bdate, int32_t day_index, int minute) {
    tr_ksscore_input_t in;
    memset(&in, 0, sizeof(in));
    in.bdate = bdate;
    in.day_index = day_index;
    in.current_bar = current_bar;
    in.bar_open = (tr_time_us_t)current_bar * 60000000;
    in.cur_time = t_at_min(minute);
    in.high = 101.0 + minute;
    in.low = 99.0 + minute;
    in.close = 100.0 + minute;
    in.volume = 100.0;
    return in;
}

static void feed_range(tr_ksscore_t *s, int from, int to) {
    for (int i = from; i <= to; i++) {
        tr_ksscore_input_t in = make_bar(i, DATE, i - 1, i - 1);
        tr_ksscore_eval(s, &in);
    }
}

static int near(double a, double b) {
    return fabs(a - b) < 1e-9;
}

static void test_init_and_defaults(void) {
    tr_ksscore_config_t cfg;
    tr_ksscore_default_config(&cfg, 0.05);
    TR_CHECK(cfg.values.tf == TR_KS_TF_1M);
    TR_CHECK(cfg.values.persist_bars == 5);
    TR_CHECK(cfg.values.market_period == 20);
    TR_CHECK(cfg.reg_period_5 == 18 && cfg.reg_period_15 == 10 && cfg.reg_period_30 == 6);
    TR_CHECK(cfg.include_mkt_30 == 1 && cfg.time_basis == 0);
    TR_CHECK(cfg.values.price_scale == 0.05);

    tr_ksscore_t s;
    TR_CHECK(tr_ksscore_init(&s, &cfg));
    TR_CHECK(s.syn[0].cfg.synth_min == 5 && s.syn[1].cfg.synth_min == 15 && s.syn[2].cfg.synth_min == 30);
    TR_CHECK(s.syn[2].cfg.reg_period == 6 && s.syn[2].cfg.bar_sec == 60);
    TR_CHECK(tr_ksscore_relink(&s));

    cfg.values.tf = TR_KS_TF_15S;
    TR_CHECK(!tr_ksscore_init(&s, &cfg));
    cfg = fast_cfg(1);
    cfg.time_basis = 2;
    TR_CHECK(!tr_ksscore_init(&s, &cfg));
    cfg = fast_cfg(1);
    cfg.include_mkt_30 = 2;
    TR_CHECK(!tr_ksscore_init(&s, &cfg));
    TR_CHECK(!tr_ksscore_init(0, &cfg));
}

static void test_first_bar_is_reset(void) {
    tr_ksscore_t s;
    tr_ksscore_config_t cfg = fast_cfg(1);
    TR_CHECK(tr_ksscore_init(&s, &cfg));
    tr_ksscore_input_t in = make_bar(1, DATE, 0, 0);
    tr_ksscore_eval(&s, &in);
    TR_CHECK(s.out.calc_ready == 1);
    TR_CHECK(s.out.session_reset == 1 && s.out.session_key == 1);
    TR_CHECK(!s.out.three_ready && !s.out.five_ready);
    TR_CHECK(s.out.three_ratio == 0.0 && s.out.three_peak == 0.0 && s.out.three_prev_peak == 0.0);
    TR_CHECK(!s.out.break_ready && s.out.price_ratio == 0.0 && s.out.two_max == 0.0);
    TR_CHECK(s.out.ratio_score == 0 && s.out.score == 0);
    TR_CHECK(!s.out.reg_valid[0] && !s.out.mkt_valid[0]);

    in.close = 1000.0;
    tr_ksscore_eval(&s, &in);
    TR_CHECK(s.out.session_key == 1 && s.out.session_reset == 1);
    TR_CHECK(s.out.three_cnt == 0.0 && s.out.seg_cnt == 0.0);
}

static void test_ratios_colors_and_session_roll(void) {
    tr_ksscore_t s;
    tr_ksscore_config_t cfg = fast_cfg(1);
    TR_CHECK(tr_ksscore_init(&s, &cfg));

    int ready_at = 0;
    for (int i = 1; i <= 80 && ready_at == 0; i++) {
        tr_ksscore_input_t in = make_bar(i, DATE, i - 1, i - 1);
        tr_ksscore_eval(&s, &in);
        if (s.out.three_ready) {
            ready_at = i;
        }
    }
    TR_CHECK(ready_at > 1);
    TR_CHECK(s.out.five_ready);
    TR_CHECK(near(s.out.three_ratio, 100.0) && near(s.out.five_ratio, 100.0));
    TR_CHECK(near(s.out.three_cnt, 1.0) && near(s.out.five_cnt, 1.0));
    TR_CHECK(near(s.out.price_ratio, 100.0));
    TR_CHECK(near(s.out.two_range, s.out.two_max));
    TR_CHECK(s.out.three_prev_peak == 0.0);

    double thi = s.out.target[0], tlo = s.out.target[0];
    double fhi = thi, flo = tlo;
    for (int k = 1; k < 5; k++) {
        fhi = fmax(fhi, s.out.target[k]);
        flo = fmin(flo, s.out.target[k]);
        if (k < 3) {
            thi = fmax(thi, s.out.target[k]);
            tlo = fmin(tlo, s.out.target[k]);
        }
    }
    TR_CHECK(near(s.out.three_hi, thi) && near(s.out.three_lo, tlo));
    TR_CHECK(near(s.out.three_gap, thi - tlo));
    TR_CHECK(near(s.out.five_hi, fhi) && near(s.out.five_lo, flo));
    TR_CHECK(near(s.out.five_gap, fhi - flo));
    TR_CHECK(s.out.five_gap + 1e-9 >= s.out.three_gap);
    TR_CHECK(s.out.target[3] != 0.0 && s.out.target[4] != 0.0);

    tr_ksscore_out_t held = s.out;
    tr_ksscore_input_t again = make_bar(ready_at, DATE, ready_at - 1, ready_at - 1);
    tr_ksscore_eval(&s, &again);
    TR_CHECK(near(s.out.three_ratio, held.three_ratio));
    TR_CHECK(near(s.out.three_cnt, held.three_cnt));
    TR_CHECK(near(s.out.three_peak, held.three_peak));
    TR_CHECK(near(s.out.price_ratio, held.price_ratio));
    TR_CHECK(near(s.out.seg_cnt, held.seg_cnt));
    TR_CHECK(s.out.three_below == held.three_below && s.out.three_above == held.three_above);

    /* 종가만 바꾸면 목표 폭은 그대로다. 색 기록은 직전 봉(미준비)에서 다시 센다. */
    if (held.three_hi > held.three_lo) {
        again.close = held.three_hi;
        tr_ksscore_eval(&s, &again);
        TR_CHECK(s.out.three_below == 0);
        TR_CHECK(s.out.three_above == 1);
        TR_CHECK(s.out.three_rgb == 0xFF0000u);
        TR_CHECK(near(s.out.three_ratio, held.three_ratio));
        TR_CHECK(near(s.out.three_cnt, held.three_cnt));
        TR_CHECK(near(s.out.three_peak, held.three_peak));
        again.close = 100.0 + (ready_at - 1);
        tr_ksscore_eval(&s, &again);
        TR_CHECK(s.out.three_below == held.three_below);
        TR_CHECK(s.out.three_above == held.three_above);
    }

    /* 스코프 가산은 합계에만 들어가고, 제외 점수는 직전 봉 이탈이 없으면 0이다. */
    int scope = 0;
    if (s.out.three_ready && s.out.three_ratio >= 30.0) {
        if (s.out.three_below == 0) {
            scope = -1;
        } else if (s.out.three_above == 0) {
            scope = 1;
        }
    }
    TR_CHECK(s.out.score == scope);
    TR_CHECK(s.out.score_ex == 0);
    TR_CHECK(s.out.ratio_score == 0);

    double prev_w = s.out.tgt_width;
    double prev_hi = s.out.cur_hi;
    double prev_cnt = s.out.seg_cnt;
    int prev_ready = s.out.break_ready;
    int next = ready_at + 1;
    tr_ksscore_input_t nxt = make_bar(next, DATE, next - 1, next - 1);
    tr_ksscore_eval(&s, &nxt);
    TR_CHECK(!s.out.session_reset && s.out.break_ready && prev_ready);
    if (s.out.tgt_width != prev_w) {
        TR_CHECK(s.out.seg_changed == 1 && s.out.prev_valid == 1);
        TR_CHECK(near(s.out.prev_hi, prev_hi));
        TR_CHECK(near(s.out.cur_hi, nxt.high));
        TR_CHECK(near(s.out.seg_cnt, 1.0));
    } else {
        TR_CHECK(s.out.seg_changed == 0 && s.out.prev_valid == 0);
        TR_CHECK(near(s.out.seg_cnt, prev_cnt + 1.0));
        TR_CHECK(near(s.out.cur_hi, fmax(prev_hi, nxt.high)));
    }
    if (s.out.two_max > 0.0) {
        TR_CHECK(near(s.out.price_ratio, s.out.two_range / s.out.two_max * 100.0));
    }

    /* 같은 봉에서 고가를 낮추면 앞서 올린 구간 최고를 유지하지 않는다. */
    nxt.high = nxt.close + 30.0;
    tr_ksscore_eval(&s, &nxt);
    double inflated_hi = s.out.cur_hi;
    nxt.high = nxt.close + 1.0;
    tr_ksscore_eval(&s, &nxt);
    TR_CHECK(s.out.cur_hi < inflated_hi);
    if (s.out.break_ready && !s.out.session_reset) {
        if (s.out.tgt_width == prev_w) {
            TR_CHECK(near(s.out.cur_hi, fmax(prev_hi, nxt.high)));
            TR_CHECK(near(s.out.seg_cnt, prev_cnt + 1.0));
        } else {
            TR_CHECK(near(s.out.cur_hi, nxt.high));
            TR_CHECK(near(s.out.seg_cnt, 1.0));
        }
    }

    double peak = s.out.three_peak;
    int64_t key = s.out.session_key;
    TR_CHECK(peak > 0.0 && s.out.two_max > 0.0);

    int jump = next + 1;
    tr_ksscore_input_t jump_in = make_bar(jump, DATE, 400, 400);
    tr_ksscore_eval(&s, &jump_in);
    TR_CHECK(!s.out.session_reset && s.out.session_key == key);
    TR_CHECK(s.out.three_prev_peak == 0.0);
    TR_CHECK(s.out.three_peak + 1e-9 >= peak);
    double peak_at_jump = s.out.three_peak;
    double five_at_jump = s.out.five_peak;

    tr_ksscore_input_t rolled = make_bar(jump + 1, DATE + 1, 0, 0);
    tr_ksscore_eval(&s, &rolled);
    TR_CHECK(s.out.session_reset == 1 && s.out.session_key == key + 1);
    TR_CHECK(near(s.out.three_prev_peak, peak_at_jump));
    TR_CHECK(near(s.out.five_prev_peak, five_at_jump));
    TR_CHECK(!s.out.three_ready && !s.out.five_ready && !s.out.break_ready);
    TR_CHECK(s.out.three_peak == 0.0 && s.out.five_peak == 0.0);
    TR_CHECK(s.out.two_max == 0.0 && s.out.price_ratio == 0.0);
}

static void test_synthetic_lines_feed_gaps(void) {
    tr_ksscore_t s;
    tr_ksscore_config_t cfg = fast_cfg(0);
    TR_CHECK(tr_ksscore_init(&s, &cfg));
    feed_range(&s, 1, 26);
    TR_CHECK(s.out.reg_valid[0] == 1);
    TR_CHECK(near(s.out.reg_px[0], 122.0));
    TR_CHECK(s.out.mkt_valid[0] == 1);
    double vwap = ((353.0 / 3.0) * 500.0 + (368.0 / 3.0) * 500.0) / 1000.0;
    TR_CHECK(near(s.out.mkt_px[0], vwap));
    TR_CHECK(!s.out.reg_ready);

    tr_ksscore_input_t again = make_bar(26, DATE, 25, 25);
    again.volume = 1.0;
    tr_ksscore_eval(&s, &again);
    TR_CHECK(near(s.out.reg_px[0], 122.0));
    TR_CHECK(s.syn[0].out_reg_valid == 1);

    feed_range(&s, 27, 31);
    TR_CHECK(s.out.mkt_valid[0] == 1 && s.out.mkt_valid[1] == 1);
    TR_CHECK(s.out.mkt_valid[2] == 0);
    TR_CHECK(s.out.market_center != 0.0);
    TR_CHECK(s.out.mkt_ready == 1);
    TR_CHECK(near(s.out.mkt_ratio, 100.0));
    double hi = s.out.market_center;
    double lo = hi;
    hi = fmax(hi, s.out.mkt_px[0]);
    lo = fmin(lo, s.out.mkt_px[0]);
    hi = fmax(hi, s.out.mkt_px[1]);
    lo = fmin(lo, s.out.mkt_px[1]);
    TR_CHECK(near(s.out.mkt_hi, hi) && near(s.out.mkt_lo, lo));
    TR_CHECK(near(s.out.mkt_gap, hi - lo));
    TR_CHECK(s.out.mkt_lo != 0.0);

    tr_ksscore_t full;
    cfg = fast_cfg(1);
    TR_CHECK(tr_ksscore_init(&full, &cfg));
    int first_reg = 0;
    int first_mkt = 0;
    for (int i = 1; i <= 151; i++) {
        tr_ksscore_input_t in = make_bar(i, DATE, i - 1, i - 1);
        tr_ksscore_eval(&full, &in);
        if (i == 76) {
            TR_CHECK(full.out.reg_valid[0] == 1 && full.out.reg_valid[1] == 1);
            TR_CHECK(full.out.reg_valid[2] == 0 && full.out.reg_ready == 0);
        }
        if (full.out.reg_ready && first_reg == 0) {
            first_reg = i;
            TR_CHECK(near(full.out.reg_ratio, 100.0));
        }
        if (full.out.mkt_ready && first_mkt == 0) {
            first_mkt = i;
            TR_CHECK(near(full.out.mkt_ratio, 100.0));
        }
    }
    TR_CHECK(first_reg == 151);
    TR_CHECK(first_mkt > 0 && first_mkt < 151);
    TR_CHECK(full.out.reg_valid[2] == 1 && full.out.reg_flat != 0.0 && full.out.reg_ready == 1);
    hi = full.out.reg_flat;
    lo = hi;
    for (int i = 0; i < 3; i++) {
        hi = fmax(hi, full.out.reg_px[i]);
        lo = fmin(lo, full.out.reg_px[i]);
    }
    TR_CHECK(near(full.out.reg_hi, hi) && near(full.out.reg_lo, lo));
    TR_CHECK(near(full.out.reg_gap, hi - lo));
    TR_CHECK(near(full.out.reg_ratio, full.out.reg_gap / full.out.reg_peak * 100.0));
    TR_CHECK(full.out.mkt_ready == 1 && full.out.mkt_peak > 0.0);
    hi = full.out.market_center;
    lo = hi;
    for (int i = 0; i < 3; i++) {
        hi = fmax(hi, full.out.mkt_px[i]);
        lo = fmin(lo, full.out.mkt_px[i]);
    }
    TR_CHECK(near(full.out.mkt_gap, hi - lo));
    TR_CHECK(near(full.out.mkt_ratio, full.out.mkt_gap / full.out.mkt_peak * 100.0));
    TR_CHECK(full.out.mkt_ratio < 100.0);
}

int main(void) {
    test_init_and_defaults();
    test_first_bar_is_reset();
    test_ratios_colors_and_session_roll();
    test_synthetic_lines_feed_gaps();
    TR_TEST_SUMMARY();
}
