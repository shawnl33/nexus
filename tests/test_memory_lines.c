/* 회귀기억선·지속선 상태 기계 테스트: 저장 조건, 호가 관성, 세션 리셋, 이탈 숨김 */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/indicators/memory_lines.h"

static tr_regmem_input_t make_rm_in(void) {
    tr_regmem_input_t in;
    memset(&in, 0, sizeof(in));
    in.reg_valid = true;
    in.line_flat = 100.0;
    in.line_sign = 1;
    in.slope = 0.5;
    in.pred_price[0] = 101.3;
    in.pred_price[1] = 102.6;
    in.pred_price[2] = 103.4;
    in.upper[0] = 104.0; in.upper[1] = 105.0; in.upper[2] = 106.0;
    in.lower[0] = 99.0;  in.lower[1] = 98.0;  in.lower[2] = 97.0;
    in.session_no = 1;
    in.compress_min = true;
    in.compress_min_le30 = true;
    in.ob_applicable = false;
    in.ob_state = 0;
    return in;
}

static void test_regmem_save_and_hold(void) {
    tr_regmem_t s;
    tr_regmem_config_t cfg = {1.0, 4.0, 1, true};
    tr_regmem_init(&s, &cfg);

    tr_regmem_input_t in = make_rm_in();
    tr_regmem_on_bar(&s, &in);
    /* 방향 확정 봉: 저장. 목표는 틱 양자화 */
    TR_CHECK(s.mem_valid && s.mem_dir == 1);
    TR_CHECK(fabs(s.mem_price - 100.0) < 1e-9);
    TR_CHECK(fabs(s.mem_target[0] - 101.0) < 1e-9);  /* round(101.3) */
    TR_CHECK(fabs(s.mem_target[1] - 103.0) < 1e-9);  /* round(102.6) */
    TR_CHECK(s.updated);
    TR_CHECK(s.show_targets);
    TR_CHECK(!s.show_upper && !s.show_lower); /* 저장된 그 봉은 밴드 숨김 */

    /* 같은 방향 다음 봉: 재저장 없이 유지, 밴드 표시 */
    tr_regmem_on_bar(&s, &in);
    TR_CHECK(!s.updated);
    TR_CHECK(fabs(s.mem_target[0] - 101.0) < 1e-9);
    TR_CHECK(s.show_upper && s.show_lower);

    /* 방향 전환(호가 미적용): 즉시 새 방향 저장 */
    in.line_sign = -1;
    in.line_flat = 99.0;
    tr_regmem_on_bar(&s, &in);
    TR_CHECK(s.mem_dir == -1 && s.updated);
    TR_CHECK(fabs(s.mem_price - 99.0) < 1e-9);
}

static void test_regmem_orderbook_inertia(void) {
    tr_regmem_t s;
    tr_regmem_config_t cfg = {1.0, 4.0, 1, true};
    tr_regmem_init(&s, &cfg);

    tr_regmem_input_t in = make_rm_in();
    tr_regmem_on_bar(&s, &in); /* 방향 1 저장 */
    TR_CHECK(s.mem_dir == 1);

    /* 방향 −1 전환 시도 + 호가 적용. 호가가 동의(−1)하면 확인 누적 */
    in.line_sign = -1;
    in.ob_applicable = true;
    in.ob_state = -1; /* |상태|<2 → 필요 확인 2봉 */
    tr_regmem_on_bar(&s, &in);
    /* 1봉째: 확인 부족 → 기존 방향 유지 */
    TR_CHECK(s.current_dir == 1);
    TR_CHECK(s.mem_dir == 1 && !s.updated);

    /* 2봉째: 확인 충족 → 전환 저장 */
    tr_regmem_on_bar(&s, &in);
    TR_CHECK(s.mem_dir == -1 && s.updated);
}

static void test_regmem_session_reset(void) {
    tr_regmem_t s;
    tr_regmem_config_t cfg = {1.0, 4.0, 1, true};
    tr_regmem_init(&s, &cfg);

    tr_regmem_input_t in = make_rm_in();
    tr_regmem_on_bar(&s, &in);
    TR_CHECK(s.mem_valid);

    /* 세션 변경: 기억 리셋 */
    in.session_no = 2;
    tr_regmem_on_bar(&s, &in);
    /* 리셋 후 같은 봉에서 재저장 조건이 성립하면 다시 저장된다 */
    TR_CHECK(s.mem_session == 2);
}

static void test_regmem_hide_bands(void) {
    tr_regmem_t s;
    tr_regmem_config_t cfg = {1.0, 4.0, 1, true};
    tr_regmem_init(&s, &cfg);

    static const double h_low[5] = {90.0, 90.0, 90.0, 90.0, 90.0};
    static const double l_low[5] = {89.0, 89.0, 89.0, 89.0, 89.0};
    tr_regmem_input_t in = make_rm_in();
    tr_regmem_on_bar(&s, &in); /* 저장 봉 */

    /* 5봉 연속 H < 목표3(103) → 상단 밴드 숨김, 하단은 표시 */
    in.h5 = h_low;
    in.l5 = l_low;
    in.hl_count = 5;
    tr_regmem_on_bar(&s, &in);
    TR_CHECK(!s.show_upper);
    TR_CHECK(s.show_lower);
}

static void test_persist_flow(void) {
    tr_persist_t s;
    tr_persist_config_t cfg = {1.0, 3, 0.40};
    tr_persist_init(&s, &cfg);

    tr_persist_input_t in;
    memset(&in, 0, sizeof(in));
    in.reg_valid = true;
    in.r2 = 0.50;
    in.pred_dir2 = 1;
    in.pred_price[0] = 101.0;
    in.pred_price[1] = 102.0;
    in.pred_price[2] = 103.0;

    tr_persist_on_bar(&s, &in); /* 봉1: 리셋, streak=0 */
    TR_CHECK(!s.saved_valid);
    tr_persist_on_bar(&s, &in); /* 봉2: streak=1 */
    tr_persist_on_bar(&s, &in); /* 봉3: streak=2 */
    TR_CHECK(!s.saved_valid);
    tr_persist_on_bar(&s, &in); /* 봉4: streak=3 == 지속봉수 → 저장 */
    TR_CHECK(s.saved_valid && s.saved_dir == 1);
    TR_CHECK(fabs(s.target[2] - 103.0) < 1e-9);

    /* streak이 더 이어져도 재저장하지 않는다 (== 조건) */
    in.pred_price[2] = 200.0;
    tr_persist_on_bar(&s, &in); /* 봉5: streak=4 */
    TR_CHECK(fabs(s.target[2] - 103.0) < 1e-9);

    /* 방향 변경 → streak 리셋 */
    in.pred_dir2 = -1;
    tr_persist_on_bar(&s, &in); /* streak=1 */
    TR_CHECK(s.streak == 1);
}

static void test_persist_min_r2(void) {
    tr_persist_t s;
    tr_persist_config_t cfg = {1.0, 2, 0.40};
    tr_persist_init(&s, &cfg);

    tr_persist_input_t in;
    memset(&in, 0, sizeof(in));
    in.reg_valid = true;
    in.r2 = 0.30; /* 최소신뢰도 미달 */
    in.pred_dir2 = 1;
    in.pred_price[2] = 103.0;

    tr_persist_on_bar(&s, &in); /* 봉1 */
    tr_persist_on_bar(&s, &in); /* 봉2: streak=1 */
    tr_persist_on_bar(&s, &in); /* 봉3: streak=2 == 지속봉수, 그러나 신뢰도 미달 */
    TR_CHECK(!s.saved_valid);
}

int main(void) {
    test_regmem_save_and_hold();
    test_regmem_orderbook_inertia();
    test_regmem_session_reset();
    test_regmem_hide_bands();
    test_persist_flow();
    test_persist_min_r2();
    TR_TEST_SUMMARY();
}
