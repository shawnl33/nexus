/* WSF_1m_DailyAlignV2 포팅 테스트: 합의, 일반장/갭장 가감점, 큰 갭 비중 복원, 최종 상태 승격 */

#include "test_util.h"

#include <math.h>

#include "core/functions/daily_align_v2.h"

static tr_dalign2_input_t make_in(void) {
    tr_dalign2_input_t in;
    in.pred_dir[0] = 1;
    in.pred_dir[1] = 1;
    in.pred_dir[2] = 1;
    in.reg_valid = true;
    in.r2 = 0.80;
    in.trend_dir = 1;
    in.trend_state = 2;
    in.trend_strength = 50.0;
    in.trend_valid = true;
    in.gap_grade = 0;
    in.daily_weight_in = 1.0;
    in.elapsed_min = 0.0;
    in.big_gap_reeval_min = 30.0;
    in.min_r2 = 0.40;
    in.min_final_strength = 40.0;
    return in;
}

static void test_unanimous_normal_market(void) {
    tr_dalign2_input_t in = make_in();
    tr_dalign2_output_t out;
    tr_dalign2_eval(&in, &out);
    TR_CHECK(out.consensus_dir == 1 && out.consensus_state == 2);
    /* 기본강도 80 + min(20, 50*0.2)=10 → 90 >= 60 → 상태 방향×2 */
    TR_CHECK(out.final_valid);
    TR_CHECK(out.final_dir == 1);
    TR_CHECK(fabs(out.final_strength - 90.0) < 1e-9);
    TR_CHECK(out.final_state == 2);
}

static void test_two_of_three_discount(void) {
    tr_dalign2_input_t in = make_in();
    in.pred_dir[2] = -1; /* 2/3 합의 */
    tr_dalign2_output_t out;
    tr_dalign2_eval(&in, &out);
    TR_CHECK(out.consensus_dir == 1 && out.consensus_state == 1);
    /* 기본강도 80*0.75=60 + 10 = 70 → 상태 방향×1 (승격 조건 미충족) */
    TR_CHECK(out.final_valid);
    TR_CHECK(fabs(out.final_strength - 70.0) < 1e-9);
    TR_CHECK(out.final_state == 1);
}

static void test_no_consensus(void) {
    tr_dalign2_input_t in = make_in();
    in.pred_dir[0] = 1;
    in.pred_dir[1] = -1;
    in.pred_dir[2] = 0;
    tr_dalign2_output_t out;
    tr_dalign2_eval(&in, &out);
    TR_CHECK(out.consensus_dir == 0);
    TR_CHECK(!out.final_valid);
}

static void test_opposite_in_normal_market_rejected(void) {
    tr_dalign2_input_t in = make_in();
    in.trend_dir = -1; /* 일봉 반대 */
    tr_dalign2_output_t out;
    tr_dalign2_eval(&in, &out);
    TR_CHECK(!out.final_valid);
}

static void test_gap_market_opposite_penalty(void) {
    tr_dalign2_input_t in = make_in();
    in.gap_grade = 1;
    in.daily_weight_in = 0.5;
    in.trend_dir = -1; /* 갭장에서 반대 */
    tr_dalign2_output_t out;
    tr_dalign2_eval(&in, &out);
    /* 기본강도 80 − min(35, 50*0.35)*0.5 = 80 − 8.75 = 71.25, 유효 */
    TR_CHECK(out.final_valid);
    TR_CHECK(fabs(out.final_strength - 71.25) < 1e-9);
}

static void test_big_gap_and_reeval_restore(void) {
    tr_dalign2_input_t in = make_in();
    in.gap_grade = 2;
    in.daily_weight_in = 0.0; /* GapRegime 출력 */
    tr_dalign2_output_t out;
    tr_dalign2_eval(&in, &out);
    /* 큰 갭: 비중 0 → 가감 없이 기본강도 80 */
    TR_CHECK(out.applied_weight == 0.0);
    TR_CHECK(out.final_valid);
    TR_CHECK(fabs(out.final_strength - 80.0) < 1e-9);

    /* 재평가 시간 경과: 비중 0.5 복원, 반대 일봉이면 감점 적용 */
    in.trend_dir = -1;
    in.elapsed_min = 35.0;
    tr_dalign2_eval(&in, &out);
    TR_CHECK(out.applied_weight == 0.5);
    TR_CHECK(fabs(out.final_strength - 71.25) < 1e-9);
}

static void test_gates(void) {
    tr_dalign2_input_t in = make_in();
    tr_dalign2_output_t out;

    in.r2 = 0.30; /* 회귀 신뢰도 미달 */
    tr_dalign2_eval(&in, &out);
    TR_CHECK(!out.final_valid);

    in = make_in();
    in.reg_valid = false;
    tr_dalign2_eval(&in, &out);
    TR_CHECK(!out.final_valid);

    in = make_in();
    in.trend_strength = 0.0; /* 강도 80 → 90 그대로, 일봉 가산 0 */
    tr_dalign2_eval(&in, &out);
    TR_CHECK(fabs(out.final_strength - 80.0) < 1e-9);

    in = make_in();
    in.min_final_strength = 95.0; /* 최종 최소 강도 미달 */
    tr_dalign2_eval(&in, &out);
    TR_CHECK(!out.final_valid);
}

int main(void) {
    test_unanimous_normal_market();
    test_two_of_three_discount();
    test_no_consensus();
    test_opposite_in_normal_market_rejected();
    test_gap_market_opposite_penalty();
    test_big_gap_and_reeval_restore();
    test_gates();
    TR_TEST_SUMMARY();
}
