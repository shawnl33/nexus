/* V3 표시부: 단계화, 과거 채점, 방향 기억, 지속선 */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/indicators/fx_mirae_v3.h"

static tr_fxv3_input_t bar(int n, bool reset) {
    tr_fxv3_input_t in;
    memset(&in, 0, sizeof(in));
    in.session_reset = reset;
    in.bar_index = n;
    in.bar_open = (tr_time_us_t)n * 60 * 1000000;
    in.high = 110;
    in.low = 90;
    in.close = 100;
    in.reg_valid = 1;
    in.reg_line = 100;
    in.reg_r2 = 0.8;
    in.reg_resid = 1;
    in.reg_sign = 1;
    in.pred_price[0] = 101;
    in.pred_price[1] = 102;
    in.pred_price[2] = 103;
    in.pred_price[3] = 104;
    in.pred_price[4] = 105;
    in.pred_dir[0] = 1;
    in.pred_dir[1] = 1;
    in.pred_dir[2] = 1;
    in.pred_vol = 1;
    return in;
}

static void test_stage_reset_then_strong(void) {
    tr_fxv3_t s;
    tr_fxv3_config_t cfg;
    tr_fxv3_default_config(&cfg, 1);
    TR_CHECK(tr_fxv3_init(&s, &cfg));
    tr_fxv3_input_t b1 = bar(1, true);
    b1.future_dir = 5;
    b1.market_dir = 1;
    b1.reg_dir = 1;
    tr_fxv3_eval(&s, &b1);
    TR_CHECK(s.plots[1].on);
    TR_CHECK(s.stage == 0.0); /* 세션 첫 봉은 0 */
    TR_CHECK(s.plots[1].rgb == 0x969696);
    TR_CHECK(s.plots[1].width == 2);

    tr_fxv3_input_t b2 = bar(2, false);
    b2.future_dir = 5;
    b2.market_dir = 1;
    b2.reg_dir = 1;
    tr_fxv3_eval(&s, &b2);
    /* 2 + (5>5 ? 0) + 1 + 1 = 4 */
    TR_CHECK(s.stage == 4.0);
    TR_CHECK(s.plots[1].rgb == 0xDC0000);
    TR_CHECK(s.plots[1].width == 6);
    TR_CHECK(s.plots[7].on);
    TR_CHECK(s.plots[7].width == 6); /* r2 0.8 >= 0.70 */

    tr_fxv3_eval(&s, &b2); /* 같은 봉 */
    TR_CHECK(s.stage == 4.0);
    TR_CHECK(s.cur.session_bars == 2);
}

static void test_past_mode_and_session_break(void) {
    tr_fxv3_t s;
    tr_fxv3_config_t cfg;
    tr_fxv3_default_config(&cfg, 1);
    cfg.past_mode = 2;
    cfg.predict_bars[0] = 1;
    cfg.predict_bars[1] = 1;
    cfg.predict_bars[2] = 1;
    cfg.predict_bars[3] = 1;
    cfg.predict_bars[4] = 1;
    tr_fxv3_init(&s, &cfg);
    tr_fxv3_input_t b1 = bar(1, true);
    b1.pred_price[2] = 777;
    b1.pred_dir[2] = -1;
    tr_fxv3_eval(&s, &b1);
    TR_CHECK(!s.plots[22].on); /* 아직 한 봉 전 없음 */

    tr_fxv3_input_t b2 = bar(2, false);
    tr_fxv3_eval(&s, &b2);
    TR_CHECK(s.plots[22].on);
    TR_CHECK(fabs(s.plots[22].value - 777.0) < 1e-9);
    TR_CHECK(s.plots[22].rgb == 0x3755CD); /* 하락·신뢰 충족 */

    tr_fxv3_input_t b3 = bar(3, true); /* 세션이 갈리면 지난 예측을 잇지 않음 */
    tr_fxv3_eval(&s, &b3);
    TR_CHECK(!s.plots[22].on);
}

static void test_memory_latches_until_session(void) {
    tr_fxv3_t s;
    tr_fxv3_config_t cfg;
    tr_fxv3_default_config(&cfg, 1);
    cfg.show_range = 1;
    tr_fxv3_init(&s, &cfg);
    tr_fxv3_input_t b1 = bar(1, true);
    b1.final_valid = 0; /* 회귀선 부호 +1로 방향 */
    tr_fxv3_eval(&s, &b1);
    TR_CHECK(s.plots[34].on);
    TR_CHECK(fabs(s.plots[34].value - 102.0) < 1e-9);
    TR_CHECK(!s.plots[38].on); /* 갱신한 봉에는 범위선을 그리지 않음 */

    tr_fxv3_input_t b2 = bar(2, false);
    tr_fxv3_eval(&s, &b2);
    TR_CHECK(s.plots[34].on);
    TR_CHECK(s.plots[38].on); /* 다음 봉부터 상단 */

    tr_fxv3_input_t b3 = bar(3, true);
    tr_fxv3_eval(&s, &b3);
    /* 세션이 바뀌면 지웠다가 새 방향으로 다시 기억 */
    TR_CHECK(s.plots[34].on);
    TR_CHECK(!s.plots[38].on);
}

static void test_persist_latches_on_streak(void) {
    tr_fxv3_t s;
    tr_fxv3_config_t cfg;
    tr_fxv3_default_config(&cfg, 1);
    cfg.persist_bars = 3;
    tr_fxv3_init(&s, &cfg);
    for (int i = 1; i <= 2; i++) {
        tr_fxv3_input_t b = bar(i, i == 1);
        tr_fxv3_eval(&s, &b);
        TR_CHECK(!s.plots[43].on);
    }
    tr_fxv3_input_t b3 = bar(3, false);
    tr_fxv3_eval(&s, &b3);
    TR_CHECK(!s.plots[43].on); /* 리셋 봉은 연속을 0으로 지운다. 그 뒤 2봉 */
    tr_fxv3_input_t b4 = bar(4, false);
    tr_fxv3_eval(&s, &b4);
    tr_fxv3_input_t b5 = bar(5, false);
    tr_fxv3_eval(&s, &b5);
    TR_CHECK(s.plots[43].on);
    TR_CHECK(fabs(s.plots[43].value - 102.0) < 1e-9);
}

static void test_market_band_and_swing_lines(void) {
    tr_fxv3_t s;
    tr_fxv3_config_t cfg;
    tr_fxv3_default_config(&cfg, 1);
    TR_CHECK(tr_fxv3_init(&s, &cfg));
    tr_fxv3_input_t b = bar(1, true);
    b.mkt_valid = 1;
    b.mkt_center = 100;
    b.mkt_up1 = 110;
    b.mkt_dn1 = 90;
    b.mkt_up2 = 120;
    b.mkt_dn2 = 80;
    b.mkt_stage = 0;
    b.sw_leg_dir = 1;
    b.sw_time_ratio = 0.5;
    b.sw_dn.high = 130;
    b.sw_dn.low = 90;
    b.sw_dn.lvl_382 = 115;
    b.sw_up_grade = 2;
    b.sw_up.ext_1618 = 140;
    b.sw_up.ext_2382 = 150;
    tr_fxv3_eval(&s, &b);
    TR_CHECK(s.plots[51].on);
    TR_CHECK(fabs(s.plots[51].value - 100.0) < 1e-9);
    TR_CHECK(s.plots[51].rgb == 0x969696); /* 단계 0 → 단계화 색(세션 첫 봉 회색) */
    TR_CHECK(s.plots[52].on && fabs(s.plots[52].value - 110.0) < 1e-9);
    TR_CHECK(s.plots[55].on && fabs(s.plots[55].value - 80.0) < 1e-9);
    TR_CHECK(s.plots[60].on && fabs(s.plots[60].value - 130.0) < 1e-9);
    TR_CHECK(s.plots[60].width == 2);
    TR_CHECK(s.plots[71].on && fabs(s.plots[71].value - 140.0) < 1e-9);
    TR_CHECK(!s.plots[65].on); /* 상승 되돌림은 하락 구간에서만 */
    TR_CHECK(s.plots[70].on && fabs(s.plots[70].value - 50.0) < 1e-9);
}

int main(void) {
    test_stage_reset_then_strong();
    test_past_mode_and_session_break();
    test_memory_latches_until_session();
    test_persist_latches_on_streak();
    test_market_band_and_swing_lines();
    TR_TEST_SUMMARY();
}
