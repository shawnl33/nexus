#ifndef TR_FX_SYNTHETIC_LINES_V1_H
#define TR_FX_SYNTHETIC_LINES_V1_H

/* WSF_FXSyntheticLinesV1 포팅 (원본 160줄, 완전 제공). 해외선물 합성봉 기준선.
 *
 * 완료된 1분봉으로 합성한 5/15/30분봉의 평탄회귀선·마켓중심가격.
 * - 새 1분봉 첫 평가에서 직전 완료 1분봉만 처리한다 (원본 3줄 주석, 27~28줄).
 *   포팅은 호출자가 현재(date/time)와 직전 완료 봉(date/time/H/L/C/V)을 넘긴다.
 * - 구간 시작 07:00/15:30 (세션 키와 같은 경과분 기준, 원본 76~78줄).
 * - 누락 분이 있는 버킷(중간부터 읽기 시작하거나 분이 건 skipped된 버킷)은 채우지
 *   않는다 (원본 85~100줄): 버킷 첫 분(경과분%합성분==0)이 아니면 시작하지 않고,
 *   연속분(직전경과분+1)이 아니면 집계를 버린다.
 * - 세션 첫 봉의 직전 봉은 세션이 달라 처리하지 않는다 (원본 74줄).
 * - 출력(저장회귀/마켓/유효)은 버킷 완성 시에만 갱신되고 그 사이에는 유지된다
 *   (원본 4줄 주석 "이전 출력은 유지", 117~151줄). 세션 키가 바뀌면 전부 리셋된다
 *   (원본 30~39줄).
 * - 실행 게이트 (원본 20~22줄): 합성분 5/15/30, 봉시각기준 0/1, PriceScale>0 —
 *   포팅은 init에서 검증한다.
 * - 회귀는 완료중간((H+L)/2) 표본에 대한 OLS — 절편 산식이 공용 tr_ols_fit와
 *   같아(slope 형태) 재사용한다. 평탄회귀 = Round(회귀선/PS)×PS.
 * - 마켓은 완료대표((H+L+C)/3)의 거래량가중평균.
 * - 완료중간/대표/거래량 이력은 yl_var로 (저장소 구조체 안, relink 제공).
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/market/var.h"

typedef struct {
    int32_t synth_min;   /* 합성분 (5/15/30) */
    int32_t reg_period;  /* 회귀기간 */
    int32_t mkt_period;  /* 마켓기간 */
    int32_t time_basis;  /* 봉시각기준: 0=1분봉 시작시각, 1=종료시각 */
    double price_scale;  /* PriceScale (>0 필수) */
} tr_fxsyn_config_t;

typedef struct {
    int64_t cur_date, cur_time;   /* sDate/sTime (현재 평가 시각) */
    bool is_new_bar;              /* Index 변화 (새 1분봉 첫 평가) */
    bool has_prev;                /* CurrentBar > 1 대응 (직전 봉 존재) */
    int64_t prev_date, prev_time; /* sDate[1]/sTime[1] */
    double prev_h, prev_l, prev_c, prev_v; /* H/L/C/V[1] (직전 완료 1분봉) */
} tr_fxsyn_input_t;

typedef struct {
    tr_fxsyn_config_t cfg;
    /* 상태 */
    int64_t saved_key;       /* 저장세션키 */
    bool has_key;
    int32_t saved_bucket;    /* 저장버킷 */
    int32_t agg_count;       /* 집계개수 */
    int32_t prev_elapsed;    /* 직전경과분 */
    double agg_high, agg_low, agg_close, agg_vol; /* 집계고가/저가/종가/거래량 */
    double mids_buf[100];    /* 완료중간 저장소 */
    yl_var mids;             /* 완료중간[N] ([0]=최신) */
    double typical_buf[100]; /* 완료대표 저장소 */
    yl_var typical;          /* 완료대표[N] */
    double vols_buf[100];    /* 완료거래량 저장소 */
    yl_var vols;             /* 완료거래량[N] */
    /* 출력 (저장* — 버킷 완성 시에만 갱신, 사이에는 유지) */
    double out_reg;          /* 평탄회귀출력 */
    double out_mkt;          /* 마켓중심출력 */
    int out_reg_valid;       /* 회귀유효출력 */
    int out_mkt_valid;       /* 마켓유효출력 */
} tr_fxsyn_t;

/* cfg 유효성: 합성분 5/15/30, 봉시각기준 0/1, price_scale>0 (원본 20~22줄). */
bool tr_fxsyn_init(tr_fxsyn_t *s, const tr_fxsyn_config_t *cfg);

/* 매 평가 호출. 처리(집계·완성)는 is_new_bar && has_prev일 때만 일어난다. */
void tr_fxsyn_eval(tr_fxsyn_t *s, const tr_fxsyn_input_t *in);

/* tr_fxsyn_t를 포함한 구조체의 통째 값 복사(이식) 후 호출: 시계열 저장소 포인터를
 * 이 인스턴스 자신의 버퍼로 다시 연결한다 (yl_var 값 복사 불안전 — var.h 참조). */
bool tr_fxsyn_relink(tr_fxsyn_t *s);

#endif
