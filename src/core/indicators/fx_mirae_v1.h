#ifndef TR_FX_MIRAE_V1_H
#define TR_FX_MIRAE_V1_H

/* #WSF_해외선물미래곡선V1 포팅 (원본 160줄, 완전 제공). 해외선물 경량 지표.
 *
 * FXFutureValuesV1의 핵심 18값 + 합성 5/15/30분 기준선 6개를 Plot으로 표시한다.
 * 포팅은 Plot 호출을 값·표시여부·스타일(RGB·두께)의 구조체 배열로 대응한다
 * (엔진 파이프라인 연결은 별도 과제 — 이 모듈은 표시값 산출까지만).
 *
 * Plot 매핑 (원본 75~160줄):
 *   Plot1 단계화_1분_통합 (단계색상, 두께 2) — 항상 표시
 *   Plot2 곡선회귀선_평탄 (단계색상, 3) — 값 != 0
 *   Plot3 마켓중심가격 (단계색상, 3) — 값 != 0
 *   Plot4~8 지속저장목표1~5 (고유 RGB, 2) — 각 값 != 0
 *   Plot9~13 핵심지난상승 최고/최저/382/500/618 (Orange, 2) — 최고가 > 0 게이트 공통
 *   Plot14~18 핵심지난하락 최고/최저/382/500/618 (Green, 2) — 최고가 > 0 게이트 공통
 *   Plot19/20 평탄회귀/마켓중심 5분 (RGB(240,130,30), 3/1) — 합성 가능 && 회귀/마켓 유효
 *   Plot21/22 15분 (RGB(140,70,190), 3/1), Plot23/24 30분 (RGB(20,130,180), 3/1)
 * 함수계산가능(FXFutureValuesV1 반환)이 0이면 전부 NoPlot — 포팅은 init 검증으로 대응한다
 * (init이 false면 유효하지 않은 설정이다).
 *
 * 단계색상 (원본 65~71줄): >=4 RGB(220,0,0), >=2 RGB(255,100,70), ==1 RGB(255,185,185),
 * <=-4 RGB(0,0,180), <=-2 RGB(60,130,255), ==-1 RGB(180,210,255), 나머지 RGB(150,150,150).
 *
 * Input 기본값 (원본 3~14줄): 예측변수 10, 예측봉수 5/10/15/30/60, 최소신뢰도 0.4,
 * 지속봉수 5, 마켓계산기간 20, 최소전환유지봉수 3, 합성봉시각기준 0,
 * 회귀기간5분 18, 15분 10, 30분 6.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/functions/fx_future_values_v1.h"
#include "core/functions/fx_synthetic_lines_v1.h"

#define TR_FXMIRAE_PLOTS 24

typedef struct {
    tr_fxfv_config_t fv;     /* 오케스트레이터 설정 (예측변수~최소전환유지봉수 + price_scale) */
    int32_t synth_time_basis;/* 합성봉시각기준 (기본 0) */
    int32_t reg_period_5m;   /* 회귀기간5분 (기본 18) */
    int32_t reg_period_15m;  /* 회귀기간15분 (기본 10) */
    int32_t reg_period_30m;  /* 회귀기간30분 (기본 6) */
} tr_fxmirae_config_t;

typedef struct {
    tr_fxfv_input_t bar;        /* 오케스트레이터·합성 공용 현재 봉 */
    bool has_prev;              /* CurrentBar > 1 대응 */
    int64_t prev_date, prev_time;
    double prev_h, prev_l, prev_c, prev_v; /* 직전 완료 1분봉 */
} tr_fxmirae_input_t;

/* Plot 하나의 표시 계약 (값·표시여부·스타일) */
typedef struct {
    bool on;         /* Plot 조건 충족 (아니면 NoPlot) */
    double value;
    uint32_t rgb;    /* 0xRRGGBB */
    int width;       /* 두께 */
} tr_fxmirae_plot_t;

typedef struct {
    tr_fxfv_t fv;
    tr_fxsyn_t syn5, syn15, syn30;
    /* 표시 계약 */
    tr_fxmirae_plot_t plots[TR_FXMIRAE_PLOTS]; /* [0]=Plot1 .. [23]=Plot24 */
    uint32_t stage_rgb;                        /* 단계색상 (Plot1~3 공용) */
} tr_fxmirae_t;

/* 원본 Input 기본값으로 cfg를 채운다 (price_scale은 차트 값이라 인자로 받는다). */
void tr_fxmirae_default_config(tr_fxmirae_config_t *cfg, double price_scale);

bool tr_fxmirae_init(tr_fxmirae_t *s, const tr_fxmirae_config_t *cfg);

/* 매 평가 호출. */
void tr_fxmirae_eval(tr_fxmirae_t *s, const tr_fxmirae_input_t *in);

/* tr_fxmirae_t를 포함한 구조체의 통째 값 복사(이식) 후 호출: relink cascade. */
bool tr_fxmirae_relink(tr_fxmirae_t *s);

#endif
