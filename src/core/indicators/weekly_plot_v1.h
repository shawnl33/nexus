#ifndef TR_WEEKLY_PLOT_V1_H
#define TR_WEEKLY_PLOT_V1_H

/* 위클리 옵션 네 보조지표의 선.
 * 합산수익률 양매수: reference/yeslanguage/indicators/#우드스탁_위클리_합산수익률_양매수.txt 699~785.
 * 합산수익률 양매도: reference/yeslanguage/indicators/#우드스탁_위클리_합산수익률_양매도.txt 698~775.
 * 프라이스링크 양매수: reference/yeslanguage/indicators/#우드스탁_위클리_프라이스링크_양매수.txt 699~772.
 * 프라이스링크 양매도: reference/yeslanguage/indicators/#우드스탁_위클리_프라이스링크_양매도.txt 698~762.
 * 한 쪽을 한 번 걸으면 프라이스링크와 합산수익률이 같이 나온다.
 * 점수 Plot1~7은 원본에서 주석이다. 계산하지 않는다.
 * 양매수 첫 만남만 삼선 비율을 본다. 그 값은 호출자가 넣거나, replay가 선물 봉으로 계산한다.
 * 주문은 내지 않는다.
 */

#include <stddef.h>
#include <stdint.h>

#include "core/functions/ks_score_v1.h"
#include "core/signals/weekly_pair_v1.h"

#define TR_WPLOT_LONG 1
#define TR_WPLOT_SHORT (-1)

typedef struct {
    int trade_off; /* 매매종료. 1이면 새 만남만 막는다 */
    int meet_mode; /* 만남방식. 1 겹침 중간값, 2 종가 중간값 */
    double tolerance; /* 허용오차 */
    int32_t entry_start_bar; /* 양매수 진입시작봉수 */
    int32_t afternoon_switch; /* 양매수 오후전환봉수 */
    double morning_three_max; /* 오전삼선만남최대비율 */
    double afternoon_three_max; /* 오후삼선만남최대비율 */
    int64_t entry_start_time; /* 양매도 진입시작 HHMMSS */
} tr_wplot_config_t;

/* 이 봉의 Data1·Data3·Data2. 없는 다리는 compress를 0으로 둔다. */
typedef struct {
    int64_t bdate;
    int64_t sdate;
    int64_t stime;
    int compress;
    int interval;
    double high, low, close;
    int64_t d3_date, d3_time;
    int d3_compress, d3_interval;
    double d3_high, d3_low, d3_close;
    int64_t d2_date, d2_time;
    int d2_compress, d2_interval;
    int32_t d2_day_index;
    int calc_ready;
    int three_ready;
    double three_peak;
    double three_ratio;
    int break_ready;
    double two_max;
    double price_ratio;
} tr_wplot_bar_t;

typedef struct {
    int link_on; /* 1이면 D3_첫만남가격 */
    double link;
    int ret_on; /* 1이면 합산수익률. 영점선은 화면이 그린다 */
    double ret;
} tr_wplot_out_t;

typedef struct {
    int side;
    tr_wplot_config_t cfg;
    int64_t saved_bdate;
    int has_day;
    int valid;
    double stored;
    double self_meet;
    double opp_meet;
} tr_wplot_t;

typedef struct {
    int64_t time_sec;
    double value;
} tr_wplot_pt_t;

void tr_wplot_default_config(tr_wplot_config_t *cfg);

/* side는 TR_WPLOT_LONG 또는 TR_WPLOT_SHORT. 실패 시 -1. */
int tr_wplot_init(tr_wplot_t *st, int side, const tr_wplot_config_t *cfg);

/* 봉 사이에 cfg를 바꿔도 된다. 실패 시 -1. */
int tr_wplot_eval(tr_wplot_t *st, const tr_wplot_bar_t *bar, tr_wplot_out_t *out);

/* 오래된 봉이 앞이다. 양매수는 시각이 맞는 선물 봉의 점수만 복사한다.
 * 점은 그린 봉만. cap을 넘으면 -1. 성공 시 0.
 * score_cfg가 NULL이고 양매수면 틱 0.05. 양매도는 점수를 쓰지 않는다.
 * 재진입하지 않는다. */
int tr_wplot_replay(tr_wplot_t *st, const tr_wpair_pxbar_t *self, size_t nself,
                    const tr_wpair_pxbar_t *opp, size_t nopp, const tr_wpair_pxbar_t *fut,
                    size_t nfut, const tr_ksscore_config_t *score_cfg, tr_wplot_pt_t *link,
                    size_t link_cap, size_t *nlink, tr_wplot_pt_t *ret, size_t ret_cap,
                    size_t *nret);

/* 링크는 [시작초,끝초,가격], 수익률은 [시작초,값...]. 60초로 이어진 점만 한 덩어리다.
 * 버퍼에 안 들어가면 오래된 점을 버린다. 성공 시 0. 버퍼가 껍질보다 작으면 -1. */
int tr_wplot_pack(char *buf, size_t cap, int64_t t0, int64_t t1, const tr_wplot_pt_t *ll,
                  size_t nll, const tr_wplot_pt_t *lr, size_t nlr, const tr_wplot_pt_t *sl,
                  size_t nsl, const tr_wplot_pt_t *sr, size_t nsr);

#endif
