/* yl_var 테스트 — 예스랭귀지 변수 1:1 대응 타입 (docs/PORTING.md "암묵 시계열 원칙")
 *
 * - 오프셋 의미: [0]=최신, [1]=1봉 전, ...
 * - 용량 초과 시 가장 오래된 값 덮어쓰기 후 [0..cap-1] 유지
 * - set_current는 과거를 밀어내지 않고 현재([0])를 제자리 교체
 * - 범위 초과 at=false, count/clear
 * - 포팅 규약 1:1 예시: 예스랭귀지 스니펫과 같은 값을 내는지 (살아있는 규약 예시)
 */

#include "test_util.h"

#include "core/market/var.h"

#define CAP 4
static YL_VAR_STORAGE(s, CAP);

static void test_init_invalid(void) {
    yl_var x;
    double v = 0.0;
    TR_CHECK(!ylv_init(&x, 0, CAP));          /* 저장소 NULL */
    TR_CHECK(!ylv_init(&x, s_buf, 0));        /* 용량 0 */
    TR_CHECK(!ylv_init(0, s_buf, CAP));       /* 변수 NULL */
    TR_CHECK(!ylv_push(0, 1.0));
    TR_CHECK(!ylv_at(0, 0, &v));
    TR_CHECK(ylv_count(0) == 0);
    (void)v;
}

static void test_push_at_offsets(void) {
    yl_var x;
    TR_CHECK(ylv_init(&x, s_buf, CAP));
    TR_CHECK(ylv_count(&x) == 0);
    double out = -1.0;
    TR_CHECK(!ylv_at(&x, 0, &out));           /* 빈 상태 참조 실패 */
    TR_CHECK(!ylv_set_current(&x, 9.0));      /* 빈 상태 갱신 실패 */

    for (int i = 1; i <= 3; i++) {
        TR_CHECK(!ylv_push(&x, (double)i));   /* 아직 덮어쓰기 없음 */
    }
    TR_CHECK(ylv_count(&x) == 3);
    TR_CHECK(ylv_at(&x, 0, &out) && out == 3.0);  /* [0]=현재 */
    TR_CHECK(ylv_at(&x, 1, &out) && out == 2.0);  /* [1]=1봉 전 */
    TR_CHECK(ylv_at(&x, 2, &out) && out == 1.0);
    TR_CHECK(!ylv_at(&x, 3, &out));           /* 범위 초과 */
}

static void test_overwrite_keeps_window(void) {
    yl_var x;
    ylv_init(&x, s_buf, CAP);
    for (int i = 1; i <= CAP; i++) {
        ylv_push(&x, (double)i);
    }
    TR_CHECK(ylv_count(&x) == CAP);

    TR_CHECK(ylv_push(&x, 99.0));             /* 1이 밀려남 */
    TR_CHECK(ylv_count(&x) == CAP);
    double out;
    TR_CHECK(ylv_at(&x, 0, &out) && out == 99.0);
    TR_CHECK(ylv_at(&x, CAP - 1, &out) && out == 2.0); /* 가장 오래된 값 */
    TR_CHECK(!ylv_at(&x, CAP, &out));

    /* 2개 더 밀어내고 순환 후에도 [0..cap-1] 창 유지 */
    ylv_push(&x, 100.0);
    ylv_push(&x, 101.0);
    TR_CHECK(ylv_at(&x, 0, &out) && out == 101.0);
    TR_CHECK(ylv_at(&x, 1, &out) && out == 100.0);
    TR_CHECK(ylv_at(&x, 2, &out) && out == 99.0);
    TR_CHECK(ylv_at(&x, 3, &out) && out == 4.0);
    TR_CHECK(!ylv_at(&x, 4, &out));
}

static void test_set_current_in_place(void) {
    yl_var x;
    ylv_init(&x, s_buf, CAP);
    for (int i = 1; i <= 3; i++) {
        ylv_push(&x, (double)i);
    }
    /* 같은 봉의 반복 대입: 과거 봉이 밀리지 않아야 한다 */
    for (int k = 0; k < 5; k++) {
        TR_CHECK(ylv_set_current(&x, 300.0 + k));
    }
    TR_CHECK(ylv_count(&x) == 3);
    double out;
    TR_CHECK(ylv_at(&x, 0, &out) && out == 304.0); /* 현재 봉만 교체 */
    TR_CHECK(ylv_at(&x, 1, &out) && out == 2.0);
    TR_CHECK(ylv_at(&x, 2, &out) && out == 1.0);
}

static void test_clear(void) {
    yl_var x;
    ylv_init(&x, s_buf, CAP);
    ylv_push(&x, 7.0);
    ylv_push(&x, 8.0);
    TR_CHECK(ylv_count(&x) == 2);
    ylv_clear(&x);
    TR_CHECK(ylv_count(&x) == 0);
    double out;
    TR_CHECK(!ylv_at(&x, 0, &out));
    TR_CHECK(ylv_push(&x, 9.0) == false);     /* clear 후 재사용 가능 */
    TR_CHECK(ylv_at(&x, 0, &out) && out == 9.0);
}

/* 포팅 규약 1:1 예시 (docs/PORTING.md의 살아있는 예시).
 *
 * 예스랭귀지 원본 스니펫:
 *   var diff(0), sig(0);
 *   diff = close - open;
 *   sig  = diff[1] + diff[2];
 *
 * 매 봉 위 대입문이 순서대로 평가된다. 같은 봉에 갱신된 diff가 아니라
 * 1·2봉 전 diff를 참조한다는 점이 핵심이다. 이력이 부족한 봉(처음 2봉)의
 * sig은 계산하지 않는다 (ylv_at 범위 초과 = false).
 */
static void test_yeslanguage_1to1_example(void) {
    static const double open_[5]  = {100.0, 101.0, 103.0, 102.0, 105.0};
    static const double close_[5] = {101.0, 103.0, 102.0, 105.0, 104.0};
    /* 손계산 diff: 1, 2, -1, 3, -1
     * sig(봉2) = 2+1 = 3, sig(봉3) = -1+2 = 1, sig(봉4) = 3+(-1) = 2 */
    static const double sig_expect[3] = {3.0, 1.0, 2.0};

    YL_VAR_STORAGE(diff, 8);
    yl_var diff_s;
    TR_CHECK(ylv_init(&diff_s, diff_buf, 8));

    int sig_n = 0;
    for (int bar = 0; bar < 5; bar++) {
        ylv_push(&diff_s, close_[bar] - open_[bar]);        /* diff = close - open; */
        double d1, d2;
        if (ylv_at(&diff_s, 1, &d1) && ylv_at(&diff_s, 2, &d2)) {
            double sig = d1 + d2;                           /* sig = diff[1] + diff[2]; */
            TR_CHECK(sig == sig_expect[sig_n]);
            sig_n++;
        }
    }
    TR_CHECK(sig_n == 3); /* 처음 2봉은 이력 부족으로 미계산 */
}

int main(void) {
    test_init_invalid();
    test_push_at_offsets();
    test_overwrite_keeps_window();
    test_set_current_in_place();
    test_clear();
    test_yeslanguage_1to1_example();
    TR_TEST_SUMMARY();
}
