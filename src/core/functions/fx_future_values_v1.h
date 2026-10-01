#ifndef TR_FX_FUTURE_VALUES_V1_H
#define TR_FX_FUTURE_VALUES_V1_H

/* WSF_FXFutureValuesV1 포팅 (원본 359줄, 완전 제공). 해외선물 오케스트레이터:
 * 세션 키·리셋 신호를 만들어 하부 4모듈(FXRegV1×2 호출부, FXPredictV2, FXCurveV1,
 * FXSwingV1)을 순서대로 호출해 핵심 18값을 출력한다. 표시 없음 (원본 1줄).
 *
 * - 세션 키: WSF_FXSessionKeyV1(sDate,sTime) (원본 150줄). FX초기화 =
 *   CurrentBar==1 || 키 != 키[1], FX세션봉수는 리셋 시 1, 아니면 [1]+1 (152~155줄).
 *   포팅은 호출자가 넘기는 date/time으로 키를 계산하고, 봉 상대 시리즈([1] 참조)는
 *   봉 말 스냅샷으로 재현한다 (새 봉 첫 평가에 갱신, 같은 봉 재평가는 복원).
 * - 호출 순서 (원본): FXRegV1#1(예측용) → FXPredictV2 → 평탄/지속저장/마켓VWAP →
 *   FXCurveV1 → FXRegV1#2(회귀방향용) → 핵심마켓 → 단계화 → FXSwingV1.
 *   두 FXRegV1 호출부는 원본의 호출부별 Var 상태 대응으로 별도 인스턴스다.
 * - 실행 게이트 (원본 148줄): 1분봉 && PriceScale > 0. 포팅은 init에서 검증한다
 *   (설정은 상수) — 유효하지 않으면 init이 false를 돌려준다.
 * - Round(v/PriceScale,0)*PriceScale은 국내 엔진과 같은 floor(v/ps+0.5)*ps 패턴.
 *
 * 18값 산출 경로:
 *   단계화_1분_통합 = IFF(미래>0,2,IFF(<0,−2,0)) + 미래↔미래[1] 비교 ±1 +
 *     핵심마켓방향 + 핵심회귀방향 (리셋 봉은 0) — 미래=FXCurve 방향(리셋 봉 0),
 *     회귀방향=FXReg#2 유효&&신뢰도 충족 시 C vs 평탄회귀선. 국내판과 달리 호가방향 항이 없다.
 *   곡선회귀선_평탄 = FXReg#1 유효 시 Round(회귀선/PS)×PS (매 평가 갱신, 유효 시에만 출력).
 *   마켓중심가격 = 최근 Min(Max(1,마켓계산기간),세션봉수)봉의 (H+L+C)/3 거래량가중평균
 *     (누적거래량>0 && 계산봉수>=2일 때만 출력) — 캔들 시리즈 윈도우(tr_ring, yl_var 대상 아님).
 *   지속저장목표1~5 = MTF예측방향2가 Max(1,지속봉수)봉 연속 && 회귀 유효·신뢰도 충족 시
 *     래치되는 Round(FXPredict 예측가격k/PS)×PS (유효 래치 동안만 출력).
 *   핵심지난상승/하락 최고·최저·382·500·618 = FXSwingV1의 지난구간 메모리 출력.
 *
 * 원본 기록(불명): 핵심미래방향부호(285~286줄)는 계산만 하고 이후 사용하지 않는다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/functions/fx_curve_v1.h"
#include "core/functions/fx_predict_v2.h"
#include "core/functions/fx_reg_v1.h"
#include "core/functions/fx_session_key_v1.h"
#include "core/functions/fx_swing_v1.h"
#include "core/market/ring.h"
#include "core/model/time_us.h"

typedef struct {
    int32_t predict_ticks;   /* 예측변수 (곡선예측틱수) */
    int32_t predict_bars[5]; /* 예측봉수1~5 */
    double min_r2;           /* 최소신뢰도 */
    int32_t persist_bars;    /* 지속봉수 */
    int32_t market_period;   /* 마켓계산기간 */
    int32_t min_hold_bars;   /* 최소전환유지봉수 */
    double price_scale;      /* PriceScale (>0 필수) */
} tr_fxfv_config_t;

typedef struct {
    int64_t date;          /* sDate (yyyymmdd) */
    int64_t time;          /* sTime (hhmmss) */
    tr_time_us_t bar_open; /* 새 봉 판정 (하부 모듈 공용) */
    double high, low, close;
    double volume;         /* V (현재 봉 누적 거래량) */
} tr_fxfv_input_t;

typedef struct {
    double score;            /* 단계화_1분_통합 */
    double reg_flat;         /* 곡선회귀선_평탄 */
    double market_center;    /* 마켓중심가격 */
    double persist_target[5];/* 지속저장목표1~5 */
    double lup_high, lup_low, lup_382, lup_500, lup_618; /* 핵심지난상승 */
    double ldn_high, ldn_low, ldn_382, ldn_500, ldn_618; /* 핵심지난하락 */
} tr_fxfv_output_t;

/* 마켓 윈도우의 봉 요소 (chart H/L/C/V[j] 대응) */
typedef struct {
    double h, l, c, v;
} tr_fxfv_bar_t;

/* 봉 상대 시리즈 상태 (원본의 [1] 참조 대상 — 봉 말 스냅샷으로 복원) */
typedef struct {
    int64_t key;             /* FX세션키[1] */
    int32_t session_bars;    /* FX세션봉수[1] */
    double persist_streak;   /* 지속연속봉수 */
    int persist_valid;       /* 지속저장유효 */
    int persist_dir;         /* 지속저장방향 */
    double persist_target[5];/* 상태_지속저장목표1~5 */
    int prev_pred_dir2;      /* MTF예측방향2[1] */
    double prev_future_dir;  /* 핵심세션미래방향[1] */
    double prev_core_mkt;    /* 핵심마켓중심[1] */
} tr_fxfv_series_t;

typedef struct {
    tr_fxfv_config_t cfg;
    /* 하부 모듈 인스턴스 (호출부별 독립 상태) */
    tr_fxreg_t reg_pred;     /* 곡선회귀 (예측용, 원본 158줄 호출부) */
    tr_fxreg_t reg_core;     /* 핵심곡선회귀 (회귀방향용, 원본 288줄 호출부) */
    tr_fxp2_t predict;
    tr_fxc_t curve;
    tr_fxsw_t swing;
    /* 세션 추적 */
    tr_fxfv_series_t series;   /* 현재 봉의 시리즈 값 */
    tr_fxfv_series_t prev_bar; /* 직전 봉 말 스냅샷 */
    bool has_bar;
    tr_time_us_t last_bar_open;
    bool session_reset;        /* FX초기화 (이번 봉) */
    int32_t session_bars;      /* FX세션봉수 (이번 봉) */
    bool is_new_bar;           /* 이번 평가의 새 봉 여부 (지표가 합성 모듈에 전달) */
    /* 마켓 윈도우 (캔들 시리즈 — 상태 구조체 안 저장소, relink 대상) */
    tr_fxfv_bar_t bar_win_buf[100];
    tr_ring bar_win;
    /* 평가 중간 상태 (매 평가 재계산) */
    double state_reg_flat;     /* 상태_곡선회귀선_평탄 */
    double state_mkt_center;   /* 상태_마켓중심가격 */
    bool mkt_valid;            /* 마켓계산유효 */
    /* 출력 (원본 NumericRef 18값 대응) */
    tr_fxfv_output_t out;
} tr_fxfv_t;

/* cfg 유효성: 마켓계산기간>=1 && price_scale>0 (원본 148줄의 실행 게이트 대응 —
 * 1분봉 조건은 호출 계약으로 init 인자 검증에 통합한다). 실패 시 false. */
bool tr_fxfv_init(tr_fxfv_t *s, const tr_fxfv_config_t *cfg);

/* 매 평가 호출 (새 봉·진행 봉 재평가 모두). */
void tr_fxfv_eval(tr_fxfv_t *s, const tr_fxfv_input_t *in);

/* tr_fxfv_t를 포함한 구조체의 통째 값 복사(이식) 후 호출: 하부 모듈 relink를
 * cascade하고 마켓 윈도우 저장소도 이 인스턴스 자신의 버퍼로 다시 연결한다. */
bool tr_fxfv_relink(tr_fxfv_t *s);

#endif
