/* #WSF_해외선물미래곡선V1 포팅 테스트: Input 기본값, 단계색상, Plot 24개 매핑
 * (값·표시여부·스타일), 합성 기준선 연동 */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/indicators/fx_mirae_v1.h"

#define DATE 20241001

static int64_t t_at(int m) {
    int h = 9 + m / 60, mm = m % 60;
    return (int64_t)h * 10000 + (int64_t)mm * 100;
}

/* 결정적 상승 봉 (fx_future 테스트와 같은 수열): m = 09:00 기준 분 (0기반) */
static tr_fxmirae_input_t make_bar(int m) {
    tr_fxmirae_input_t in;
    memset(&in, 0, sizeof(in));
    in.bar.date = DATE;
    in.bar.time = t_at(m);
    in.bar.bar_open = (tr_time_us_t)(m + 1) * 60000000;
    in.bar.high = 101.0 + m;
    in.bar.low = 99.0 + m;
    in.bar.close = 100.0 + m;
    in.bar.volume = 100.0;
    in.has_prev = (m > 0);
    if (m > 0) {
        in.prev_date = DATE;
        in.prev_time = t_at(m - 1);
        in.prev_h = 101.0 + (m - 1);
        in.prev_l = 99.0 + (m - 1);
        in.prev_c = 100.0 + (m - 1);
        in.prev_v = 100.0;
    }
    return in;
}

/* 기본값·초기 표시 상태 */
static void test_defaults_and_stage_color(void) {
    tr_fxmirae_config_t cfg;
    tr_fxmirae_default_config(&cfg, 1.0);
    TR_CHECK(cfg.fv.predict_ticks == 10);
    TR_CHECK(cfg.fv.predict_bars[0] == 5 && cfg.fv.predict_bars[4] == 60);
    TR_CHECK(cfg.fv.min_r2 == 0.4 && cfg.fv.persist_bars == 5);
    TR_CHECK(cfg.fv.market_period == 20 && cfg.fv.min_hold_bars == 3);
    TR_CHECK(cfg.synth_time_basis == 0);
    TR_CHECK(cfg.reg_period_5m == 18 && cfg.reg_period_15m == 10 && cfg.reg_period_30m == 6);

    tr_fxmirae_t s;
    TR_CHECK(tr_fxmirae_init(&s, &cfg));
    /* 가격 수열로 6봉: 봉6(m=5)에서 점수 3 → 단계색상 RGB(255,100,70) */
    for (int m = 0; m <= 5; m++) {
        tr_fxmirae_input_t in = make_bar(m);
        tr_fxmirae_eval(&s, &in);
    }
    TR_CHECK(s.plots[0].on && s.plots[0].value == 3.0);
    TR_CHECK(s.plots[0].rgb == 0xFF6446u); /* (255,100,70) */
    TR_CHECK(s.plots[0].width == 2);
    TR_CHECK(s.stage_rgb == 0xFF6446u);
    /* 곡선회귀선_평탄: 회귀선 105 → Round = 105 */
    TR_CHECK(s.plots[1].on && s.plots[1].value == 105.0 && s.plots[1].width == 3);
    TR_CHECK(s.plots[2].on); /* 마켓중심가격 != 0 */
    /* 아직 지속저장·스윙 메모리 없음 → Plot4~18 전부 NoPlot */
    for (int k = 3; k < 18; k++) {
        TR_CHECK(!s.plots[k].on);
    }
}

/* 지속저장 목표·스윙 메모리 게이트 */
static void test_persist_and_swing_plots(void) {
    tr_fxmirae_config_t cfg;
    tr_fxmirae_default_config(&cfg, 1.0);
    cfg.fv.persist_bars = 2; /* 시나리오 단축 */
    cfg.fv.min_hold_bars = 2;
    tr_fxmirae_t s;
    tr_fxmirae_init(&s, &cfg);
    for (int m = 0; m <= 5; m++) {
        tr_fxmirae_input_t in = make_bar(m);
        tr_fxmirae_eval(&s, &in);
    }
    /* 지속저장 래치 → Plot4~8 표시 (값은 오케스트레이터 출력과 같다) */
    for (int k = 0; k < 5; k++) {
        TR_CHECK(s.plots[3 + k].on);
        TR_CHECK(s.plots[3 + k].value == s.fv.out.persist_target[k]);
        TR_CHECK(s.plots[3 + k].width == 2);
    }
    TR_CHECK(s.plots[3].rgb == 0x6E6E6Eu);   /* RGB(110,110,110) */
    TR_CHECK(s.plots[7].rgb == 0x5A46B4u);   /* RGB(90,70,180) */

    /* 하락 전환 후 지난상승 메모리 확정 → Plot9~13 (Orange 게이트: 최고가 > 0).
     * 급락시켜 미래방향을 확실히 음수로 만든다 (최소전환유지봉수 2로 하락 확정 유도) */
    static const double crash[5][3] = {
        {106.0, 90.0, 95.0}, {95.0, 80.0, 82.0}, {85.0, 70.0, 72.0},
        {75.0, 60.0, 62.0}, {65.0, 50.0, 52.0}
    };
    for (int m = 0; m < 5; m++) {
        tr_fxmirae_input_t in = make_bar(6 + m);
        in.bar.high = crash[m][0];
        in.bar.low = crash[m][1];
        in.bar.close = crash[m][2];
        tr_fxmirae_eval(&s, &in);
    }
    if (s.fv.out.lup_high > 0.0) {
        for (int k = 8; k < 13; k++) {
            TR_CHECK(s.plots[k].on);
            TR_CHECK(s.plots[k].rgb == 0xFF7F00u); /* Orange */
        }
        TR_CHECK(s.plots[8].value == s.fv.out.lup_high);
        TR_CHECK(s.plots[12].value == s.fv.out.lup_618);
    } else {
        TR_CHECK(0); /* 이 시나리오에서는 지난상승 메모리가 확정되어야 한다 */
    }
}

/* 합성 기준선: 5분(Plot19/20), 15분(21/22), 30분(23/24) — 181분 공급 */
static void test_synthetic_plots(void) {
    tr_fxmirae_config_t cfg;
    tr_fxmirae_default_config(&cfg, 1.0);
    tr_fxmirae_t s;
    tr_fxmirae_init(&s, &cfg);
    for (int m = 0; m <= 180; m++) {
        tr_fxmirae_input_t in = make_bar(m);
        tr_fxmirae_eval(&s, &in);
    }

    /* 5분: 완성 36개, 회귀기간 18 — 완전 직선(버킷 중간값 기울기 5)이라 현재값 277 */
    TR_CHECK(s.plots[18].on);
    TR_CHECK(s.plots[18].value == 277.0);
    TR_CHECK(s.plots[18].rgb == 0xF0821Eu && s.plots[18].width == 3); /* RGB(240,130,30) */
    TR_CHECK(s.plots[19].on); /* 마켓중심 5분 유효 */
    TR_CHECK(s.plots[19].value == s.syn5.out_mkt);

    /* 15분: 완성 12개, 회귀기간 10 — 버킷 중간값 기울기 15 → 현재값 272 */
    TR_CHECK(s.plots[20].on);
    TR_CHECK(s.plots[20].value == 272.0);
    TR_CHECK(s.plots[20].rgb == 0x8C46BEu && s.plots[20].width == 3); /* RGB(140,70,190) */

    /* 30분: 완성 6개, 회귀기간 6 — 버킷 중간값 기울기 30 → 현재값 265 */
    TR_CHECK(s.plots[22].on);
    TR_CHECK(s.plots[22].value == 265.0);
    TR_CHECK(s.plots[22].rgb == 0x1482B4u && s.plots[22].width == 3); /* RGB(20,130,180) */
    TR_CHECK(s.plots[23].on);
}

int main(void) {
    test_defaults_and_stage_color();
    test_persist_and_swing_plots();
    test_synthetic_plots();
    TR_TEST_SUMMARY();
}
