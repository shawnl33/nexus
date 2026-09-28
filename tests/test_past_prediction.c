/* 과거예측 검증 테스트: 가변 룩백, 세션 번호, 세션 교차 무효화 */

#include "test_util.h"

#include <math.h>

#include "core/indicators/past_prediction.h"

#define CAP 8
static tr_ppv_frame_t storage[CAP];

static tr_ppv_frame_t frame_for(int bar) {
    tr_ppv_frame_t f;
    f.pred_price[0] = bar * 100 + 1;
    f.pred_price[1] = bar * 100 + 2;
    f.pred_price[2] = bar * 100 + 3;
    f.r2 = bar * 0.01;
    f.pred_dir[0] = 1;
    f.pred_dir[1] = 1;
    f.pred_dir[2] = 1;
    f.reg_valid = true;
    f.session_no = 0;
    return f;
}

static void test_variable_lookback(void) {
    tr_ppv_t s;
    int32_t pb[3] = {2, 3, 4};
    TR_CHECK(tr_ppv_init(&s, pb, true, storage, CAP));

    /* 봉 1: 세션 첫 봉 → 세션 번호 1. 룩백 부족으로 과거값 없음 */
    tr_ppv_frame_t f = frame_for(1);
    tr_ppv_on_bar(&s, 1, true, &f);
    TR_CHECK(s.session_no == 1);
    TR_CHECK(s.past_valid[0] == 0 && s.past_pred[0] == 0.0);

    for (int i = 2; i <= 5; i++) {
        f = frame_for(i);
        tr_ppv_on_bar(&s, i, false, &f);
    }
    /* 봉 5: [2]→봉3, [3]→봉2, [4]→봉1 */
    TR_CHECK(fabs(s.past_pred[0] - 301.0) < 1e-9);
    TR_CHECK(fabs(s.past_pred[1] - 202.0) < 1e-9);
    TR_CHECK(fabs(s.past_pred[2] - 103.0) < 1e-9);
    TR_CHECK(s.past_valid[0] == 1 && s.past_valid[1] == 1 && s.past_valid[2] == 1);
    TR_CHECK(fabs(s.past_r2[0] - 0.03) < 1e-9);
}

static void test_session_crossing_invalidates(void) {
    tr_ppv_t s;
    int32_t pb[3] = {2, 3, 4};
    tr_ppv_init(&s, pb, true, storage, CAP);

    for (int i = 1; i <= 5; i++) {
        tr_ppv_frame_t f = frame_for(i);
        tr_ppv_on_bar(&s, i, i == 1, &f);
    }
    /* 봉 6: 새 세션 → 세션 번호 2. 룩백 대상은 전부 세션 1 → 무효 */
    tr_ppv_frame_t f = frame_for(6);
    tr_ppv_on_bar(&s, 6, true, &f);
    TR_CHECK(s.session_no == 2);
    TR_CHECK(s.past_valid[0] == 0 && s.past_valid[1] == 0 && s.past_valid[2] == 0);

    /* 봉 8: [2]→봉6(세션2) 유효, [3]→봉5(세션1) 무효, [4]→봉4(세션1) 무효 */
    f = frame_for(7);
    tr_ppv_on_bar(&s, 7, false, &f);
    f = frame_for(8);
    tr_ppv_on_bar(&s, 8, false, &f);
    TR_CHECK(s.past_valid[0] == 1);
    TR_CHECK(fabs(s.past_pred[0] - 601.0) < 1e-9);
    TR_CHECK(s.past_valid[1] == 0);
    TR_CHECK(s.past_valid[2] == 0);
}

static void test_same_bar_no_double_count(void) {
    tr_ppv_t s;
    int32_t pb[3] = {2, 3, 4};
    tr_ppv_init(&s, pb, true, storage, CAP);
    /* 같은 Index로 session_first가 두 번 와도 세션 번호는 한 번만 증가 */
    tr_ppv_frame_t f = frame_for(1);
    tr_ppv_on_bar(&s, 1, true, &f);
    uint64_t no = s.session_no;
    tr_ppv_on_bar(&s, 1, true, &f);
    TR_CHECK(s.session_no == no);
}

int main(void) {
    test_variable_lookback();
    test_session_crossing_invalidates();
    test_same_bar_no_double_count();
    TR_TEST_SUMMARY();
}
