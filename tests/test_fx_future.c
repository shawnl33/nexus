/* WSF_FXFutureValuesV1 포팅 테스트: 세션 키·리셋 전파, 18값 산출 경로 정합
 * (손계산 대조), 지속저장 래치, 세션 경계 리셋, 동일 봉 재평가 복원, relink */

#include "test_util.h"

#include <math.h>
#include <string.h>

#include "core/functions/fx_future_values_v1.h"

#define DATE 20241001

static tr_fxfv_config_t test_cfg(void) {
    tr_fxfv_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.predict_ticks = 10;
    cfg.predict_bars[0] = 5;
    cfg.predict_bars[1] = 10;
    cfg.predict_bars[2] = 15;
    cfg.predict_bars[3] = 30;
    cfg.predict_bars[4] = 60;
    cfg.min_r2 = 0.4;
    cfg.persist_bars = 2;
    cfg.market_period = 3;
    cfg.min_hold_bars = 2;
    cfg.price_scale = 1.0;
    return cfg;
}

/* 결정적 상승 시나리오: 봉 i(1기반) = 09:(i-1) 시작, H=100+i, L=99+i, C=100+i, V=100
 * → TR 항상 1 (세션ATR=1), 중간값 99.5+i (기울기 1 완전 직선) */
static tr_fxfv_input_t make_bar(int i, int64_t time) {
    tr_fxfv_input_t in;
    memset(&in, 0, sizeof(in));
    in.date = DATE;
    in.time = time;
    in.bar_open = (tr_time_us_t)i * 60000000;
    in.high = 100.0 + i;
    in.low = 99.0 + i;
    in.close = 100.0 + i;
    in.volume = 100.0;
    return in;
}

static int64_t t_at(int m) {
    int h = 9 + m / 60, mm = m % 60;
    return (int64_t)h * 10000 + (int64_t)mm * 100;
}

static double round_tick(double v) {
    return floor(v / 1.0 + 0.5) * 1.0;
}

/* 시나리오 정합: 리셋 봉 기본값, 5봉 유효, 6봉 점수·래치·VWAP 손계산 대조 */
static void test_values_match_references(void) {
    tr_fxfv_t s;
    tr_fxfv_config_t cfg = test_cfg();
    TR_CHECK(tr_fxfv_init(&s, &cfg));

    tr_fxfv_input_t in = make_bar(1, t_at(0));
    tr_fxfv_eval(&s, &in);
    TR_CHECK(s.session_reset && s.session_bars == 1 && s.is_new_bar);
    TR_CHECK(s.out.score == 0.0);        /* 리셋 봉은 0 */
    TR_CHECK(s.out.reg_flat == 0.0);     /* 회귀 무효 */
    TR_CHECK(s.out.market_center == 0.0); /* 계산봉수 1 < 2 */

    for (int i = 2; i <= 5; i++) {
        in = make_bar(i, t_at(i - 1));
        tr_fxfv_eval(&s, &in);
    }
    /* 봉5: 회귀 유효 (중간값 100.5..104.5 → 회귀선 104.5), 점수 손계산 = 3 */
    TR_CHECK(s.session_bars == 5 && !s.session_reset);
    TR_CHECK(s.out.reg_flat == round_tick(104.5));
    TR_CHECK(s.out.score == 3.0);
    /* 마켓중심가격: 봉 3~5의 (H+L+C)/3 VWAP (동일 거래량, 합산은 최신→과거 순) */
    double tp3 = (103.0 + 102.0 + 103.0) / 3.0;
    double tp4 = (104.0 + 103.0 + 104.0) / 3.0;
    double tp5 = (105.0 + 104.0 + 105.0) / 3.0;
    double pv = tp5 * 100.0;
    pv += tp4 * 100.0;
    pv += tp3 * 100.0;
    TR_CHECK(s.out.market_center == pv / 300.0);
    /* 아직 스윙 메모리·지속저장 없음 */
    TR_CHECK(s.out.lup_high == 0.0 && s.out.ldn_high == 0.0);
    TR_CHECK(s.out.persist_target[0] == 0.0);

    /* 봉6: 점수 3 (미래 11>0 +2, 미래 동일 +0, 회귀 0, 마켓 +1) + 지속저장 래치 */
    in = make_bar(6, t_at(5));
    tr_fxfv_eval(&s, &in);
    TR_CHECK(s.out.score == 3.0);
    static const int nh[5] = {5, 10, 15, 30, 60};
    for (int k = 0; k < 5; k++) {
        double mv = 0.5 * (double)nh[k];            /* 보정기울기 clamp(1×1, 1×0.5)=0.5 */
        double mx = 1.0 * 1.5 * sqrt(fmax(1.0, (double)nh[k])); /* 세션ATR=1 */
        double move = mv > mx ? mx : mv;
        TR_CHECK(s.out.persist_target[k] == round_tick(105.5 + move));
    }
}

/* 세션 경계: 낮 구간 → 저녁 구간(15:30)에서 전부 리셋 */
static void test_session_boundary_reset(void) {
    tr_fxfv_t s;
    tr_fxfv_config_t cfg = test_cfg();
    tr_fxfv_init(&s, &cfg);
    for (int i = 1; i <= 6; i++) {
        tr_fxfv_input_t in = make_bar(i, t_at(i - 1));
        tr_fxfv_eval(&s, &in);
    }
    TR_CHECK(s.out.score == 3.0);

    /* 15:30 같은 날: 세션 키가 구간 0→1로 바뀐다 → 리셋 봉 */
    tr_fxfv_input_t in = make_bar(7, 153000);
    tr_fxfv_eval(&s, &in);
    TR_CHECK(s.session_reset && s.session_bars == 1);
    TR_CHECK(s.out.score == 0.0);          /* 리셋 봉은 0 */
    TR_CHECK(s.out.reg_flat == 0.0);       /* 회귀 리셋 → 무효 */
    TR_CHECK(s.out.persist_target[0] == 0.0); /* 지속저장도 리셋 */

    /* 다음 봉: 세션봉수 2, 리셋 아님 */
    in = make_bar(8, 153100);
    tr_fxfv_eval(&s, &in);
    TR_CHECK(!s.session_reset && s.session_bars == 2);
}

/* 동일 봉 재평가: 봉 상대 시리즈가 복원되어 1회분만 반영된다 */
static void test_same_bar_restore(void) {
    tr_fxfv_t s;
    tr_fxfv_config_t cfg = test_cfg();
    tr_fxfv_init(&s, &cfg);
    for (int i = 1; i <= 6; i++) {
        tr_fxfv_input_t in = make_bar(i, t_at(i - 1));
        tr_fxfv_eval(&s, &in);
    }
    TR_CHECK(s.out.score == 3.0);
    double targets[5];
    memcpy(targets, s.out.persist_target, sizeof(targets));

    /* 봉6 재평가 (같은 bar_open, 다른 종가): 재계산되지만 시리즈는 1회분만 */
    tr_fxfv_input_t in = make_bar(6, t_at(5));
    in.close = 107.0;
    tr_fxfv_eval(&s, &in);
    TR_CHECK(s.out.score == 4.0); /* 회귀방향 +1 추가 (107 > 평탄 106) */

    /* 봉6 재평가 (원래 종가): 첫 평가와 같은 값으로 돌아와야 한다 */
    in = make_bar(6, t_at(5));
    tr_fxfv_eval(&s, &in);
    TR_CHECK(s.out.score == 3.0);
    for (int k = 0; k < 5; k++) {
        TR_CHECK(s.out.persist_target[k] == targets[k]);
    }
    TR_CHECK(s.session_bars == 6); /* 봉수가 중복 증가하지 않는다 */
}

/* relink: 통째 값 복사 후 재연결하면 두 인스턴스가 독립적으로 동작한다 */
static void test_relink_independence(void) {
    tr_fxfv_t a;
    tr_fxfv_config_t cfg = test_cfg();
    tr_fxfv_init(&a, &cfg);
    for (int i = 1; i <= 6; i++) {
        tr_fxfv_input_t in = make_bar(i, t_at(i - 1));
        tr_fxfv_eval(&a, &in);
    }
    tr_fxfv_t b = a;
    TR_CHECK(tr_fxfv_relink(&b));
    /* b에 다른 시나리오 공급 — a의 하부 모듈·윈도우가 오염되지 않아야 한다 */
    for (int i = 7; i <= 10; i++) {
        tr_fxfv_input_t in = make_bar(i, t_at(i - 1));
        in.high = 500.0;
        tr_fxfv_eval(&b, &in);
    }
    tr_fxfv_input_t in = make_bar(7, t_at(6));
    tr_fxfv_eval(&a, &in);
    /* a는 원래 시나리오의 연속값을 유지한다 (봉7 점수 손계산 = 3) */
    TR_CHECK(a.out.score == 3.0);
    TR_CHECK(a.out.persist_target[0] == round_tick(105.5 + 0.5 * 5.0));
}

int main(void) {
    test_values_match_references();
    test_session_boundary_reset();
    test_same_bar_restore();
    test_relink_independence();
    TR_TEST_SUMMARY();
}
