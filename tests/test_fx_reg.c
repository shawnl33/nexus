/* WSF_FXRegV1 포팅 테스트: 자동 회귀기간, 워밍업 경계, 세션 리셋, 같은 봉 갱신,
 * OLS 독립 계산 대조 (원본 산식 순서) */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/fx_reg_v1.h"

/* 독립 참조 OLS: oldest-first px, x=0..n-1 (원본 107~153줄과 같은 산식 순서) */
static void ref_ols(const double *px, size_t n, double *line, double *slope,
                    double *r2, double *resid) {
    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0, sumY2 = 0.0;
    for (size_t i = 0; i < n; i++) {
        sumX += (double)i;
        sumY += px[i];
        sumXY += (double)i * px[i];
        sumX2 += (double)i * (double)i;
        sumY2 += px[i] * px[i];
    }
    double dn = (double)n;
    double denom = dn * sumX2 - sumX * sumX;
    double lrs = (dn * sumXY - sumX * sumY) / denom;
    double b = (sumY * sumX2 - sumX * sumXY) / denom;
    *line = lrs * (double)(n - 1) + b;
    *slope = lrs;
    double r2n = dn * sumXY - sumX * sumY;
    double r2d = (dn * sumX2 - sumX * sumX) * (dn * sumY2 - sumY * sumY);
    *r2 = r2d > 0.0 ? fmin(1.0, fmax(0.0, (r2n * r2n) / r2d)) : 0.0;
    double sst = sumY2 - sumY * sumY / dn;
    double sse = fmax(0.0, sst * (1.0 - *r2));
    *resid = n > 2 ? sqrt(sse / (double)(n - 2)) : 0.0;
}

static tr_fxreg_input_t make_in(bool reset, double price, int64_t open) {
    tr_fxreg_input_t in;
    memset(&in, 0, sizeof(in));
    in.session_reset = reset;
    in.session_bars = 1; /* 원본 본문 미사용 — 아무 값이나 */
    in.price = price;
    in.bar_open = open;
    return in;
}

static void test_auto_period(void) {
    tr_fxreg_t s;
    TR_CHECK(tr_fxreg_init(&s, TR_COMPRESS_TICK, 0) && s.n == 30);
    TR_CHECK(tr_fxreg_init(&s, TR_COMPRESS_SEC, 0) && s.n == 30);
    TR_CHECK(tr_fxreg_init(&s, TR_COMPRESS_MIN, 1) && s.n == 30);
    TR_CHECK(tr_fxreg_init(&s, TR_COMPRESS_MIN, 5) && s.n == 18);
    TR_CHECK(tr_fxreg_init(&s, TR_COMPRESS_MIN, 15) && s.n == 10);
    TR_CHECK(tr_fxreg_init(&s, TR_COMPRESS_MIN, 30) && s.n == 6);
    TR_CHECK(tr_fxreg_init(&s, TR_COMPRESS_MIN, 60) && s.n == 12);
    TR_CHECK(tr_fxreg_init(&s, TR_COMPRESS_DAY, 0) && s.n == 20);
    TR_CHECK(tr_fxreg_init(&s, TR_COMPRESS_WEEK, 0) && s.n == 13);
    TR_CHECK(tr_fxreg_init(&s, TR_COMPRESS_MONTH, 0) && s.n == 12);
}

/* 워밍업 경계: 4봉까지 무효, 5봉째 유효. 5개 표본의 OLS를 독립 계산과 == 대조 */
static void test_warmup_boundary_and_ols(void) {
    tr_fxreg_t s;
    tr_fxreg_init(&s, TR_COMPRESS_MIN, 1); /* n=30 */
    static const double px[5] = {100.0, 102.5, 101.0, 104.0, 107.5};
    for (int i = 0; i < 4; i++) {
        tr_fxreg_input_t in = make_in(false, px[i], i + 1);
        tr_fxreg_eval(&s, &in);
        TR_CHECK(!s.reg_valid);
        TR_CHECK(s.line == px[i]); /* 무효 시 회귀선=입력가격 (원본 기본값) */
        TR_CHECK(s.slope == 0.0 && s.line_sign == 0 && s.r2 == 0.0 && s.residual == 0.0);
    }
    tr_fxreg_input_t in = make_in(false, px[4], 5);
    tr_fxreg_eval(&s, &in);
    TR_CHECK(s.reg_valid);
    double line, slope, r2, resid;
    ref_ols(px, 5, &line, &slope, &r2, &resid);
    TR_CHECK(s.line == line);
    TR_CHECK(s.slope == slope);
    TR_CHECK(s.r2 == r2);
    TR_CHECK(s.residual == resid);
    TR_CHECK(s.line_sign == (slope > 0.0) - (slope < 0.0));
}

/* 창은 최근 n개만 사용한다 (MIN/30 → n=6): 10봉 공급 시 마지막 6개와 일치 */
static void test_window_uses_latest_n(void) {
    tr_fxreg_t s;
    tr_fxreg_init(&s, TR_COMPRESS_MIN, 30); /* n=6 */
    static const double px[10] = {100.0, 101.0, 99.0, 103.0, 102.0,
                                  105.0, 104.0, 107.0, 106.0, 110.0};
    for (int i = 0; i < 10; i++) {
        tr_fxreg_input_t in = make_in(false, px[i], i + 1);
        tr_fxreg_eval(&s, &in);
    }
    TR_CHECK(s.reg_valid);
    double line, slope, r2, resid;
    ref_ols(px + 4, 6, &line, &slope, &r2, &resid); /* 최근 6개: px[4..9] */
    TR_CHECK(s.line == line);
    TR_CHECK(s.slope == slope);
    TR_CHECK(s.r2 == r2);
    TR_CHECK(s.residual == resid);
}

/* 세션 리셋: 리셋 봉에서 유효개수 1로 돌아가고, 다시 5봉째에 유효 */
static void test_session_reset(void) {
    tr_fxreg_t s;
    tr_fxreg_init(&s, TR_COMPRESS_MIN, 1);
    for (int i = 0; i < 6; i++) {
        tr_fxreg_input_t in = make_in(false, 100.0 + 2.0 * i, i + 1);
        tr_fxreg_eval(&s, &in);
    }
    TR_CHECK(s.reg_valid);

    /* 리셋 (새 봉): 배열·유효개수 초기화 → 다시 워밍업 */
    static const double px[5] = {200.0, 202.5, 201.0, 204.0, 207.5};
    tr_fxreg_input_t in = make_in(true, px[0], 7);
    tr_fxreg_eval(&s, &in);
    TR_CHECK(!s.reg_valid);
    TR_CHECK(ylv_count(&s.prices) == 1);
    for (int i = 1; i <= 3; i++) {
        in = make_in(false, px[i], 7 + i);
        tr_fxreg_eval(&s, &in);
        TR_CHECK(!s.reg_valid); /* 유효개수 2~4 */
    }
    in = make_in(false, px[4], 11);
    tr_fxreg_eval(&s, &in); /* 유효개수 5 → 다시 유효 */
    TR_CHECK(s.reg_valid);
    double line, slope, r2, resid;
    ref_ols(px, 5, &line, &slope, &r2, &resid);
    TR_CHECK(s.line == line);
    TR_CHECK(s.slope == slope);

    /* 같은 봉 재평가에 리셋 신호가 와도 리셋되지 않는다 (원본: 리셋은 새 봉 블록 안) */
    in = make_in(true, 500.0, 11); /* bar_open 동일 = 같은 봉 */
    tr_fxreg_eval(&s, &in);
    TR_CHECK(ylv_count(&s.prices) == 5); /* 리셋되지 않고 [0]만 갱신 */
}

/* 같은 봉 재평가: 시프트 없이 [0]만 갱신 */
static void test_same_bar_update(void) {
    tr_fxreg_t s;
    tr_fxreg_init(&s, TR_COMPRESS_MIN, 1);
    static const double px[5] = {100.0, 102.0, 104.0, 106.0, 108.0};
    for (int i = 0; i < 5; i++) {
        tr_fxreg_input_t in = make_in(false, px[i], i + 1);
        tr_fxreg_eval(&s, &in);
    }
    TR_CHECK(ylv_count(&s.prices) == 5);

    /* 마지막 봉(open=5)을 다른 가격으로 재평가: 창 크기 동일, 최신가만 교체 */
    double px2[5] = {100.0, 102.0, 104.0, 106.0, 120.0};
    tr_fxreg_input_t in = make_in(false, 120.0, 5);
    tr_fxreg_eval(&s, &in);
    TR_CHECK(ylv_count(&s.prices) == 5);
    double line, slope, r2, resid;
    ref_ols(px2, 5, &line, &slope, &r2, &resid);
    TR_CHECK(s.line == line);
    TR_CHECK(s.slope == slope);
}

/* 완전 평탄: y 분산 0 → 유효=1, 기울기 0, 신뢰도 0, 회귀선=입력가격 (원본 R² 분모 가드) */
static void test_flat_prices(void) {
    tr_fxreg_t s;
    tr_fxreg_init(&s, TR_COMPRESS_MIN, 1);
    for (int i = 0; i < 5; i++) {
        tr_fxreg_input_t in = make_in(false, 100.0, i + 1);
        tr_fxreg_eval(&s, &in);
    }
    TR_CHECK(s.reg_valid);
    TR_CHECK(s.slope == 0.0);
    TR_CHECK(s.line == 100.0);
    TR_CHECK(s.line_sign == 0);
    TR_CHECK(s.r2 == 0.0);
    TR_CHECK(s.residual == 0.0);
}

/* relink: 통째 값 복사 후 재연결하면 두 인스턴스가 독립적으로 동작한다 */
static void test_relink_independence(void) {
    tr_fxreg_t a;
    tr_fxreg_init(&a, TR_COMPRESS_MIN, 1);
    for (int i = 0; i < 6; i++) {
        tr_fxreg_input_t in = make_in(false, 100.0 + i, i + 1);
        tr_fxreg_eval(&a, &in);
    }
    tr_fxreg_t b = a; /* 통째 값 복사 — b의 링은 아직 a의 버퍼를 가리킨다 */
    TR_CHECK(tr_fxreg_relink(&b));
    /* b에 새 값 push → a의 이력에 영향이 없어야 한다 */
    tr_fxreg_input_t in = make_in(false, 999.0, 7);
    tr_fxreg_eval(&b, &in);
    double va = 0.0, vb = 0.0;
    TR_CHECK(ylv_at(&a.prices, 0, &va) && va == 105.0);
    TR_CHECK(ylv_at(&b.prices, 0, &vb) && vb == 999.0);
}

int main(void) {
    test_auto_period();
    test_warmup_boundary_and_ols();
    test_window_uses_latest_n();
    test_session_reset();
    test_same_bar_update();
    test_flat_prices();
    test_relink_independence();
    TR_TEST_SUMMARY();
}
