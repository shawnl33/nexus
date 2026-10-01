/* WSF_FXCurveV1 포팅 테스트: 봉변화 게이트, 실표본 회귀(0 채움 없음), 세션 상대 X,
 * 세션 리셋, 곡선하[1], OLS 독립 대조, relink */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/fx_curve_v1.h"

/* 독립 참조 회귀 (원본 46~60줄 산식): mids는 오래된 순, X는 마지막 봉의 세션 상대 카운터 */
static void ref_curve(const double *mids, int count, int64_t x, int ticks,
                      double *slope, double *b, double *high, double *pred) {
    double sumXY = 0.0, sumX = 0.0, sumY = 0.0, sumX2 = 0.0;
    for (int j = 0; j < count; j++) {
        double v = mids[count - 1 - j]; /* [j] 최신 기준 */
        double xj = (double)(x - j);
        sumXY += xj * v;
        sumX += xj;
        sumY += v;
        sumX2 += xj * xj;
    }
    *slope = 0.0;
    *b = mids[count - 1];
    if (count >= 2) {
        double dn = (double)count;
        double denom = dn * sumX2 - sumX * sumX;
        *slope = (dn * sumXY - sumX * sumY) / denom;
        *b = (sumY * sumX2 - sumX * sumXY) / denom;
    }
    *high = *slope * (double)x + *b;
    *pred = *slope * (double)(x + ticks) + *b;
}

static tr_fxc_input_t make_in(bool reset, double mid, int ticks, bool new_bar) {
    tr_fxc_input_t in;
    memset(&in, 0, sizeof(in));
    in.session_reset = reset;
    in.high = mid + 1.0;
    in.low = mid - 1.0; /* (L+H)/2 = mid */
    in.ticks = ticks;
    in.is_new_bar = new_bar;
    return in;
}

/* 완전 직선: i번째 봉 중간값 = 98+2i → LRS=2, B=98 (세션 상대 X=i) */
static void test_perfect_line(void) {
    tr_fxc_t s;
    TR_CHECK(tr_fxc_init(&s));
    for (int i = 1; i <= 25; i++) {
        tr_fxc_input_t in = make_in(i == 1, 98.0 + 2.0 * i, 10, true);
        tr_fxc_eval(&s, &in);
    }
    /* X=25: 곡선상 = 2×25+98 = 148, 곡선하 = 직전 평가 값 = 146 */
    TR_CHECK(fabs(s.slope - 2.0) < 1e-9);
    TR_CHECK(fabs(s.intercept - 98.0) < 1e-9);
    TR_CHECK(fabs(s.high_curve - 148.0) < 1e-9);
    TR_CHECK(fabs(s.low_curve - 146.0) < 1e-9);
    /* 예측 = 2×(25+10)+98 = 168, 방향 = 168−146 = 22 (가격 단위 수치) */
    TR_CHECK(fabs(s.pred_price - 168.0) < 1e-9);
    TR_CHECK(fabs(s.direction - 22.0) < 1e-9);
    TR_CHECK(fabs(s.change - 22.0) < 1e-9);
}

/* 워밍업 n=1: 기울기 0·절편=현재 중간값 — 0 채움 회귀가 아니다 (htf와 다른 점) */
static void test_warmup_single_sample(void) {
    tr_fxc_t s;
    tr_fxc_init(&s);
    tr_fxc_input_t in = make_in(true, 100.0, 10, true);
    tr_fxc_eval(&s, &in);
    TR_CHECK(s.slope == 0.0);
    TR_CHECK(s.intercept == 100.0);
    TR_CHECK(s.high_curve == 100.0);
    TR_CHECK(s.low_curve == 100.0);       /* 리셋 봉은 곡선하=현재값 */
    TR_CHECK(s.pred_price == 100.0);      /* 0×(X+10)+100 */
    TR_CHECK(s.direction == 0.0);
    TR_CHECK(s.change == 0.0);
}

/* 실표본 회귀: 3봉만으로 n=3 회귀 (0 채움 없음을 독립 계산으로 고정) */
static void test_warmup_actual_samples(void) {
    tr_fxc_t s;
    tr_fxc_init(&s);
    static const double mids[3] = {100.0, 103.0, 101.0};
    for (int i = 0; i < 3; i++) {
        tr_fxc_input_t in = make_in(i == 0, mids[i], 10, true);
        tr_fxc_eval(&s, &in);
    }
    double slope, b, high, pred;
    ref_curve(mids, 3, 3, 10, &slope, &b, &high, &pred);
    TR_CHECK(s.slope == slope);
    TR_CHECK(s.intercept == b);
    TR_CHECK(s.high_curve == high);
    TR_CHECK(s.pred_price == pred);
    /* 곡선하 = 직전 평가(2봉 시점)의 MinLRL */
    double slope2, b2, high2, pred2;
    ref_curve(mids, 2, 2, 10, &slope2, &b2, &high2, &pred2);
    TR_CHECK(s.low_curve == high2);
}

/* 세션 리셋: 배열·X 초기화 → 리셋 봉은 n=1, X=1 */
static void test_session_reset(void) {
    tr_fxc_t s;
    tr_fxc_init(&s);
    for (int i = 1; i <= 5; i++) {
        tr_fxc_input_t in = make_in(i == 1, 100.0 + i, 10, true);
        tr_fxc_eval(&s, &in);
    }
    TR_CHECK(s.slope != 0.0);

    tr_fxc_input_t in = make_in(true, 200.0, 10, true); /* 리셋 봉 */
    tr_fxc_eval(&s, &in);
    TR_CHECK(ylv_count(&s.minclose) == 1);
    TR_CHECK(s.x == 1);
    TR_CHECK(s.slope == 0.0);
    TR_CHECK(s.high_curve == 200.0);
    TR_CHECK(s.low_curve == 200.0); /* 리셋 봉은 곡선하=현재값 */

    /* 다음 봉: n=2 회귀 (리셋 이후 표본만) */
    static const double mids2[2] = {200.0, 203.0};
    in = make_in(false, 203.0, 10, true);
    tr_fxc_eval(&s, &in);
    double slope, b, high, pred;
    ref_curve(mids2, 2, 2, 10, &slope, &b, &high, &pred);
    TR_CHECK(s.slope == slope);
    TR_CHECK(s.high_curve == high);
}

/* 같은 봉 재평가: 시프트 없이 [0]만 갱신, X 불변 */
static void test_same_bar_update(void) {
    tr_fxc_t s;
    tr_fxc_init(&s);
    static const double mids[5] = {100.0, 102.0, 104.0, 106.0, 108.0};
    for (int i = 0; i < 5; i++) {
        tr_fxc_input_t in = make_in(i == 0, mids[i], 10, true);
        tr_fxc_eval(&s, &in);
    }
    TR_CHECK(ylv_count(&s.minclose) == 5);
    TR_CHECK(s.x == 5);

    /* 마지막 봉 재평가 (is_new_bar=false): 중간값 120으로 갱신 */
    double mids2[5] = {100.0, 102.0, 104.0, 106.0, 120.0};
    tr_fxc_input_t in = make_in(false, 120.0, 10, false);
    tr_fxc_eval(&s, &in);
    TR_CHECK(ylv_count(&s.minclose) == 5);
    TR_CHECK(s.x == 5);
    double slope, b, high, pred;
    ref_curve(mids2, 5, 5, 10, &slope, &b, &high, &pred);
    TR_CHECK(s.slope == slope);
    TR_CHECK(s.high_curve == high);
}

/* relink: 통째 값 복사 후 재연결하면 두 인스턴스가 독립적으로 동작한다 */
static void test_relink_independence(void) {
    tr_fxc_t a;
    tr_fxc_init(&a);
    for (int i = 1; i <= 5; i++) {
        tr_fxc_input_t in = make_in(i == 1, 100.0 + i, 10, true);
        tr_fxc_eval(&a, &in);
    }
    tr_fxc_t b = a;
    TR_CHECK(tr_fxc_relink(&b));
    tr_fxc_input_t in = make_in(false, 999.0, 10, true);
    tr_fxc_eval(&b, &in);
    double va = 0.0, vb = 0.0;
    TR_CHECK(ylv_at(&a.minclose, 0, &va) && va == 105.0);
    TR_CHECK(ylv_at(&b.minclose, 0, &vb) && vb == 999.0);
    TR_CHECK(a.x == 5 && b.x == 6);
}

int main(void) {
    test_perfect_line();
    test_warmup_single_sample();
    test_warmup_actual_samples();
    test_session_reset();
    test_same_bar_update();
    test_relink_independence();
    TR_TEST_SUMMARY();
}
