/* WSF_FXSyntheticLinesV1 포팅 테스트: 버킷 집계·완성, 누락 분 제외, 봉시각기준 시프트,
 * 세션 경계 스킵·리셋, 출력 유지, relink */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/fx_synthetic_lines_v1.h"

#define DATE 20241001

/* 09:00 기준 m분 후의 hhmmss */
static int64_t t_at(int m) {
    int h = 9 + m / 60, mm = m % 60;
    return (int64_t)h * 10000 + (int64_t)mm * 100;
}

/* 결정적 1분봉 (m = 09:00 기준 분): H=101+m, L=99+m, C=100+m, V=100 */
static tr_fxsyn_input_t make_in(int cur_m, int prev_m) {
    tr_fxsyn_input_t in;
    memset(&in, 0, sizeof(in));
    in.cur_date = DATE;
    in.cur_time = t_at(cur_m);
    in.is_new_bar = true;
    in.has_prev = true;
    in.prev_date = DATE;
    in.prev_time = t_at(prev_m);
    in.prev_h = 101.0 + prev_m;
    in.prev_l = 99.0 + prev_m;
    in.prev_c = 100.0 + prev_m;
    in.prev_v = 100.0;
    return in;
}

static void feed_minutes(tr_fxsyn_t *s, int from, int to) {
    for (int m = from; m <= to; m++) {
        tr_fxsyn_input_t in = make_in(m, m - 1);
        tr_fxsyn_eval(s, &in);
    }
}

/* 5분 합성 완성·회귀·마켓: 25분 공급 → 완성 5개, 완전 직선 기울기 5 */
static void test_completion_and_lines(void) {
    tr_fxsyn_t s;
    tr_fxsyn_config_t cfg = {5, 5, 2, 0, 1.0};
    TR_CHECK(tr_fxsyn_init(&s, &cfg));

    /* eval(cur=09:01, prev=09:00)부터 — 첫 완성은 cur=09:05(prev=09:04)에서 */
    feed_minutes(&s, 1, 5);
    TR_CHECK(ylv_count(&s.mids) == 1);
    double v = 0.0;
    TR_CHECK(ylv_at(&s.mids, 0, &v) && v == 102.0); /* 집계 (105+99)/2 */
    TR_CHECK(s.out_reg_valid == 0); /* 완료개수 1 < 5 */

    /* 25분까지: 완성 5개 (버킷 k=1..5의 중간값 102,107,112,117,122 — 기울기 5) */
    feed_minutes(&s, 6, 25);
    TR_CHECK(ylv_count(&s.mids) == 5);
    TR_CHECK(s.out_reg_valid == 1);
    TR_CHECK(s.out_reg == 122.0); /* 5×4+102, ps=1 → 그대로 */
    /* 마켓: 최근 2개 완성봉의 대표값 VWAP (거래량 각 500)
     * 버킷4 (고120,저114,종119) 대표 353/3, 버킷5 (고125,저119,종124) 대표 368/3 */
    TR_CHECK(s.out_mkt_valid == 1);
    TR_CHECK(s.out_mkt == ((353.0 / 3.0) * 500.0 + (368.0 / 3.0) * 500.0) / 1000.0);
}

/* 누락 분이 있는 버킷은 채우지 않는다 */
static void test_gap_drops_bucket(void) {
    tr_fxsyn_t s;
    tr_fxsyn_config_t cfg = {5, 5, 2, 0, 1.0};
    tr_fxsyn_init(&s, &cfg);

    /* 09:01~09:12 정상 (버킷0·1 완성 예정... 버킷0: 경과분 0~4 = 분 0~4) */
    feed_minutes(&s, 1, 13);
    size_t before = ylv_count(&s.mids); /* 버킷0(elapsed 0~4 완성, cur=09:05), 버킷1(cur=09:10) = 2개 */
    TR_CHECK(before == 2);

    /* 09:13 건 skipped → 버킷2(경과분 10~14)는 폐기 */
    tr_fxsyn_input_t in = make_in(15, 14); /* prev=09:14: 직전경과분 12와 비연속 */
    tr_fxsyn_eval(&s, &in);
    in = make_in(16, 15);
    tr_fxsyn_eval(&s, &in); /* 버킷3 시작(경과분 15) */
    TR_CHECK(ylv_count(&s.mids) == before); /* 버킷2는 완성되지 않았다 */
}

/* 봉시각기준==1: 버킷 배정이 1분 당겨진다 */
static void test_time_basis_close(void) {
    tr_fxsyn_t s;
    tr_fxsyn_config_t cfg = {5, 5, 2, 1, 1.0};
    tr_fxsyn_init(&s, &cfg);

    /* basis=1: prev=09:01 → 이전분 420 → 버킷 시작. prev=09:05(이전분 424)에서 완성 */
    feed_minutes(&s, 2, 6);
    TR_CHECK(ylv_count(&s.mids) == 1);
    /* 집계 구간은 분 1~5 (basis=0보다 1분씩 당겨진 버킷) */
    double v = 0.0;
    TR_CHECK(ylv_at(&s.mids, 0, &v) && v == (106.0 + 100.0) / 2.0);
}

/* 세션 경계: 직전 봉이 이전 세션이면 처리하지 않고, 키 변경 시 출력 리셋 */
static void test_session_boundary(void) {
    tr_fxsyn_t s;
    tr_fxsyn_config_t cfg = {5, 5, 2, 0, 1.0};
    tr_fxsyn_init(&s, &cfg);
    feed_minutes(&s, 1, 10);
    TR_CHECK(ylv_count(&s.mids) == 2);
    s.out_reg = 111.0; /* 보고용 관측값 대신 — 리셋 확인을 위해 저장값을 확인한다 */
    s.out_reg_valid = 1;

    /* 15:30 첫 봉: 직전 봉(15:29)은 낮 세션 — 세션 키 불일치로 처리 스킵 + 출력 리셋 */
    tr_fxsyn_input_t in;
    memset(&in, 0, sizeof(in));
    in.cur_date = DATE;
    in.cur_time = 153000;
    in.is_new_bar = true;
    in.has_prev = true;
    in.prev_date = DATE;
    in.prev_time = 152900;
    in.prev_h = 200.0; in.prev_l = 190.0; in.prev_c = 195.0; in.prev_v = 100.0;
    tr_fxsyn_eval(&s, &in);
    TR_CHECK(ylv_count(&s.mids) == 0); /* 키 변경 리셋 */
    TR_CHECK(s.out_reg == 0.0 && s.out_reg_valid == 0);
}

/* 출력 유지: 완성 사이 평가에서는 저장값이 바뀌지 않는다 */
static void test_output_held_between_completions(void) {
    tr_fxsyn_t s;
    tr_fxsyn_config_t cfg = {5, 5, 2, 0, 1.0};
    tr_fxsyn_init(&s, &cfg);
    feed_minutes(&s, 1, 10); /* 완성 2개 시점의 저장값 확정 (버킷1 완성은 아직 — cur=09:10에서 버킷1 완성) */
    double held = s.out_mkt;
    int held_valid = s.out_mkt_valid;
    /* 같은 봉 재평가(is_new_bar=false)와 미완성 구간 평가에서 유지 */
    tr_fxsyn_input_t in = make_in(10, 9);
    in.is_new_bar = false;
    tr_fxsyn_eval(&s, &in);
    in = make_in(11, 10);
    tr_fxsyn_eval(&s, &in);
    TR_CHECK(s.out_mkt == held && s.out_mkt_valid == held_valid);
}

/* relink: 통째 값 복사 후 재연결하면 두 인스턴스가 독립적으로 동작한다 */
static void test_relink_independence(void) {
    tr_fxsyn_t a;
    tr_fxsyn_config_t cfg = {5, 5, 2, 0, 1.0};
    tr_fxsyn_init(&a, &cfg);
    feed_minutes(&a, 1, 10);
    tr_fxsyn_t b = a;
    TR_CHECK(tr_fxsyn_relink(&b));
    tr_fxsyn_input_t in = make_in(11, 10);
    in.prev_h = 999.0;
    tr_fxsyn_eval(&b, &in);
    double va = 0.0, vb = 0.0;
    TR_CHECK(ylv_at(&a.mids, 0, &va));
    TR_CHECK(ylv_at(&b.mids, 0, &vb));
    TR_CHECK(va == vb); /* b의 집계는 a의 완료 이력을 오염시키지 않는다 (아직 미완성) */
    TR_CHECK(ylv_count(&a.mids) == 2 && ylv_count(&b.mids) == 2);
}

int main(void) {
    test_completion_and_lines();
    test_gap_drops_bucket();
    test_time_basis_close();
    test_session_boundary();
    test_output_held_between_completions();
    test_relink_independence();
    TR_TEST_SUMMARY();
}
