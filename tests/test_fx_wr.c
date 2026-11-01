/* 승률 브리지가 원본 사용자 함수 호출과 같은지.
 * 미래곡선 반환·V1/V3 분리, 조정분석 최소추세봉수 15, 합성선 봉시작 시각. */

#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#include "core/functions/fx_synthetic_lines_v1.h"
#include "core/signals/fx_wr_yl.h"

static tr_fxwr_t *fresh(void) {
    tr_fxwr_t *s = (tr_fxwr_t *)calloc(1, sizeof(*s));
    if (s != 0) {
        tr_fxwr_load_defaults(s);
    }
    return s;
}

static tr_fxwr_bar_t bar_at(int64_t open, double px) {
    tr_fxwr_bar_t b;
    memset(&b, 0, sizeof(b));
    b.date = 20241001;
    b.time = 90000;
    b.bar_open = open;
    b.open = px;
    b.high = px;
    b.low = px;
    b.close = px;
    b.volume = 1;
    b.price_scale = 0.25;
    b.is_new_bar = 1;
    return b;
}

/* 1분봉이고 틱이 있으면 회귀선이 없어도 반환은 1이다. 출력만 0이다. */
static void test_future_returns_one_before_prices(void) {
    tr_fxwr_t *s = fresh();
    tr_fxwr_bar_t b = bar_at(1, 5000);
    TR_CHECK(s != 0);
    TR_CHECK(yl_WSF_FXFutureValuesV3_CO(s, &b) == 1.0);
    TR_CHECK(s->cur[YL_F_핵심지난상승최고가] == 0);
    TR_CHECK(yl_WSF_FXFutureValuesV1(s, &b) == 1.0);
    free(s->eng);
    free(s);
}

/* V3는 지속저장목표만, V1은 S목표만 쓴다. */
static void test_future_v3_and_v1_keep_separate_targets(void) {
    tr_fxwr_t *v3 = fresh();
    tr_fxwr_t *v1 = fresh();
    tr_fxwr_bar_t b = bar_at(1, 5000);
    TR_CHECK(v3 != 0 && v1 != 0);
    v3->cur[YL_F_S목표1] = 12345;
    TR_CHECK(yl_WSF_FXFutureValuesV3_CO(v3, &b) == 1.0);
    TR_CHECK(v3->cur[YL_F_S목표1] == 12345);
    v1->cur[YL_F_지속저장목표1] = 12345;
    TR_CHECK(yl_WSF_FXFutureValuesV1(v1, &b) == 1.0);
    TR_CHECK(v1->cur[YL_F_지속저장목표1] == 12345);
    free(v3->eng);
    free(v3);
    free(v1->eng);
    free(v1);
}

/* 평탄 회귀선을 한 봉씩 올린다. 첫 봉은 세션 초기화. */
static void feed_rising_flat(tr_fxwr_t *s, int bars) {
    int i;
    s->cur[YL_F_미래계산가능] = 1;
    for (i = 0; i < bars; i++) {
        double flat = 100.0 + (double)i;
        tr_fxwr_bar_t b = bar_at((int64_t)i + 1, flat + 1.0);
        b.high = flat + 2.0;
        b.low = flat - 2.0;
        s->cur[YL_F_FX초기화] = i == 0 ? 1 : 0;
        s->cur[YL_F_곡선회귀선_평탄] = flat;
        yl_WSF_FXWaveAdjV6_CO(s, &b);
        yl_WSF_FXFlatCountV2_CO(s, &b);
    }
}

/* 최소추세봉수 15. 기울기 3봉으로는 추세가 시작되지 않는다. */
static void test_wave_short_slope_does_not_trend(void) {
    tr_fxwr_t *s = fresh();
    TR_CHECK(s != 0);
    feed_rising_flat(s, 4);
    TR_CHECK(s->cur[YL_F_조정추세방향] == 0);
    free(s->eng);
    free(s);
}

/* 기울기가 15봉이 되면 상승 추세가 시작된다. */
static void test_wave_trend_starts_at_min_trend(void) {
    tr_fxwr_t *s = fresh();
    TR_CHECK(s != 0);
    feed_rising_flat(s, 16);
    TR_CHECK(s->cur[YL_F_조정추세방향] == 1);
    free(s->eng);
    free(s);
}

static int64_t minute_time(int m) {
    int h = 9 + m / 60;
    int mm = m % 60;
    return (int64_t)h * 10000 + (int64_t)mm * 100;
}

/* 엔진 시각은 봉 시작이다. 합성선은 원본 종료시각 기준 1과 같은 버킷(time_basis 0)이다. */
static void test_synthetic_uses_bar_start_clock(void) {
    tr_fxwr_t *s = fresh();
    tr_fxsyn_t oracle;
    tr_fxsyn_config_t cfg;
    int m;
    int saw = 0;
    TR_CHECK(s != 0);
    memset(&cfg, 0, sizeof(cfg));
    cfg.synth_min = 5;
    cfg.reg_period = 18;
    cfg.mkt_period = 20;
    cfg.time_basis = 0;
    cfg.price_scale = 0.25;
    TR_CHECK(tr_fxsyn_init(&oracle, &cfg));
    for (m = 1; m <= 200 && !saw; m++) {
        tr_fxwr_bar_t b;
        tr_fxsyn_input_t in;
        memset(&b, 0, sizeof(b));
        b.date = 20241001;
        b.time = minute_time(m);
        b.bar_open = (int64_t)m * 60000000;
        b.high = 101.0 + (double)m;
        b.low = 99.0 + (double)m;
        b.close = 100.0 + (double)m;
        b.volume = 100;
        b.price_scale = 0.25;
        b.is_new_bar = 1;
        s->bar_n = m + 1;
        s->last_date = 20241001;
        s->last_time = minute_time(m - 1);
        s->last_h = 101.0 + (double)(m - 1);
        s->last_l = 99.0 + (double)(m - 1);
        s->last_c = 100.0 + (double)(m - 1);
        s->last_v = 100;
        yl_WSF_FXSyntheticLinesV1(s, &b, 5);
        memset(&in, 0, sizeof(in));
        in.cur_date = b.date;
        in.cur_time = b.time;
        in.is_new_bar = 1;
        in.has_prev = 1;
        in.prev_date = s->last_date;
        in.prev_time = s->last_time;
        in.prev_h = s->last_h;
        in.prev_l = s->last_l;
        in.prev_c = s->last_c;
        in.prev_v = s->last_v;
        tr_fxsyn_eval(&oracle, &in);
        if (oracle.out_reg_valid) {
            saw = 1;
            TR_CHECK(s->cur[YL_F_S회귀유효5분] == 1);
            TR_CHECK(s->cur[YL_F_S평탄회귀5분] == oracle.out_reg);
        }
    }
    TR_CHECK(saw == 1);
    free(s->eng);
    free(s);
}

int main(void) {
    test_future_returns_one_before_prices();
    test_future_v3_and_v1_keep_separate_targets();
    test_wave_short_slope_does_not_trend();
    test_wave_trend_starts_at_min_trend();
    test_synthetic_uses_bar_start_clock();
    TR_TEST_SUMMARY();
}
