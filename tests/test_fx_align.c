/* WSF_FXAlignV1: 1분 합의와 갭에 따른 일봉 비중 */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/fx_align_v1.h"

static tr_fxalign_input_t base(void) {
    tr_fxalign_input_t in;
    memset(&in, 0, sizeof(in));
    in.pred_dir[0] = 1;
    in.pred_dir[1] = 1;
    in.pred_dir[2] = 1;
    in.reg_valid = 1;
    in.r2 = 0.8;
    in.trend_dir = 1;
    in.trend_strength = 100;
    in.trend_valid = 1;
    in.daily_weight_in = 1;
    in.min_r2 = 0.4;
    in.min_final_strength = 40;
    in.big_gap_reeval_min = 30;
    return in;
}

static void test_full_agreement_same_daily(void) {
    tr_fxalign_input_t in = base();
    tr_fxalign_output_t o;
    tr_fxalign_eval(&in, &o);
    TR_CHECK(o.consensus_dir == 1 && o.consensus_state == 2);
    TR_CHECK(o.applied_weight == 1.0);
    TR_CHECK(o.final_valid && o.final_dir == 1 && o.final_state == 2);
    TR_CHECK(fabs(o.final_strength - 100.0) < 1e-9); /* 80 + min(20, 20) */
}

static void test_big_gap_ignores_daily_until_reeval(void) {
    tr_fxalign_input_t in = base();
    in.gap_grade = 2;
    in.trend_dir = -1; /* 반대 일봉 */
    in.elapsed_min = 0;
    tr_fxalign_output_t o;
    tr_fxalign_eval(&in, &o);
    TR_CHECK(o.applied_weight == 0.0);
    TR_CHECK(o.final_valid && o.final_dir == 1);
    TR_CHECK(fabs(o.final_strength - 80.0) < 1e-9); /* 비중 0이라 감점 없음 */

    in.elapsed_min = 30;
    tr_fxalign_eval(&in, &o);
    TR_CHECK(o.applied_weight == 0.5);
    /* 80 - min(35, 35)*0.5 = 62.5 */
    TR_CHECK(fabs(o.final_strength - 62.5) < 1e-9);
}

int main(void) {
    test_full_agreement_same_daily();
    test_big_gap_ignores_daily_until_reeval();
    TR_TEST_SUMMARY();
}
