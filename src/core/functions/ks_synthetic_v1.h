#ifndef TR_KS_SYNTHETIC_V1_H
#define TR_KS_SYNTHETIC_V1_H

/* WSF_KSSynthetic1mV1 / WSF_KSSynthetic15V1
 * (reference/yeslanguage/functions/WSF_KSSynthetic1mV1.txt, 132줄).
 *
 * 완료된 봉으로 5/15/30분 합성봉의 평탄회귀선·마켓중심을 만든다.
 * 집계·OLS·마켓 가중은 WSF_FXSyntheticLinesV1과 같고, 시간 기준만 다르다.
 * - 세션 키는 WSF_KSSession1mV1이다. 직전 봉 키는 현재세션키[1]이고,
 *   시각을 다시 넣어 키를 계산하지 않는다 (원본 23, 46줄).
 * - 장 시작 초 = (현재초 - DayIndex*봉초 - 봉시각기준*봉초) 를 하루로 자른 값.
 *   세션 키가 바뀔 때만 다시 구한다 (원본 31~32줄).
 * - 1분봉은 봉초 60, 합성봉수 = 합성분. 15초봉은 봉초 15, 합성봉수 = 합성분*4
 *   (WSF_KSSynthetic15V1). 경과 단위는 그 봉초다.
 * - 새 봉 첫 평가에서만 직전 완료 봉을 넣는다 (원본 25~26줄).
 * - 버킷을 중간부터 읽거나 봉이 빠지면 그 버킷은 버린다 (원본 57~72줄).
 * - 출력은 버킷이 완성될 때만 갱신되고, 세션이 바뀌면 전부 리셋된다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "core/functions/ks_session_v1.h"
#include "core/market/var.h"

typedef struct {
    int32_t synth_min;   /* 합성분 (5/15/30) */
    int32_t reg_period;  /* 회귀기간 */
    int32_t mkt_period;  /* 마켓기간 */
    int32_t time_basis;  /* 봉시각기준: 0=봉 시작, 1=봉 끝 */
    double price_scale;  /* PriceScale (>0) */
    int32_t bar_sec;     /* 60=1분, 15=15초 */
} tr_kssyn_config_t;

typedef struct {
    int64_t bdate;
    int32_t day_index;
    int32_t current_bar; /* CurrentBar */
    int64_t cur_time;    /* sTime */
    bool is_new_bar;     /* Index 변화 */
    bool has_prev;       /* CurrentBar > 1 */
    int64_t prev_time;   /* sTime[1] */
    double prev_h, prev_l, prev_c, prev_v;
} tr_kssyn_input_t;

typedef struct {
    tr_kssyn_config_t cfg;
    tr_ks_session_t session; /* 이 호출부의 WSF_KSSession 상태 */
    int32_t bar_sec;
    int32_t synth_bars;      /* 합성봉수 */
    int32_t session_start;   /* 장시작초 */
    int64_t saved_key;       /* 저장세션키 */
    bool has_key;
    int32_t saved_bucket;    /* 저장버킷 */
    int32_t agg_count;       /* 집계개수 */
    int32_t prev_elapsed;    /* 직전경과 (봉 단위) */
    double agg_high, agg_low, agg_close, agg_vol;
    double mids_buf[100];
    yl_var mids;             /* 완료중간[N] */
    double typical_buf[100];
    yl_var typical;          /* 완료대표[N] */
    double vols_buf[100];
    yl_var vols;             /* 완료거래량[N] */
    double out_reg;
    double out_mkt;
    int out_reg_valid;
    int out_mkt_valid;
} tr_kssyn_t;

/* 합성분 5/15/30, 봉시각기준 0/1, price_scale>0, bar_sec 60 또는 15. */
bool tr_kssyn_init(tr_kssyn_t *s, const tr_kssyn_config_t *cfg);

void tr_kssyn_eval(tr_kssyn_t *s, const tr_kssyn_input_t *in);

bool tr_kssyn_relink(tr_kssyn_t *s);

#endif
