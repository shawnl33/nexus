#ifndef TR_VOLBURST_STRANGLE_H
#define TR_VOLBURST_STRANGLE_H

/* #BG_VolBurst_Strangle_V3. 선물 1분봉(Data1)으로 변동성을 보고,
 * 같은 시각의 콜(Data2)·풋(Data3) 종가로 양매수 표시 주문을 낸다.
 * 실주문은 없다. Buy를 낸 봉의 포지션은 0이고, 다음 봉에서 1이 된다.
 * 청산도 낸 봉에서는 포지션이 1로 남고 다음 봉에서 0이 된다.
 * 진입가는 그 봉의 콜·풋 종가다. EntryBars는 쓰지 않는다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/paintbar/volburst_paint.h"

#define TR_VBSTR_ENTRY 1
#define TR_VBSTR_EXIT 3

typedef struct {
    tr_vb_cfg_t vol;
    double diff_pct;
    double min_price;
    double take_pct;
    double stop_pct;
    int use_vol_exit;
    int reentry;
    int use_time_exit;
    int64_t exit_time;
} tr_vbstr_cfg_t;

typedef struct {
    int64_t time; /* HHMMSS */
    double open, high, low, close, volume;
    int has_call;
    int has_put;
    double call_c;
    double put_c;
    int is_new_bar;
} tr_vbstr_bar_t;

typedef struct {
    int pos; /* 이 봉의 식에서 본 MarketPosition. 이번 주문은 반영 전 */
    int in_zone;
    int zone_end;
    int level;
    const char *order; /* 없으면 NULL */
    double tot_ret;
} tr_vbstr_out_t;

typedef struct {
    int pos;
    int pending; /* 1 매수, -1 청산 */
    int traded;
    int in_zone;
    double call_in;
    double put_in;
} tr_vbstr_book_t;

typedef struct {
    tr_vbstr_cfg_t cfg;
    tr_vb_t vol;
    tr_vbstr_book_t cur;
    tr_vbstr_book_t open;
    int has_open;
} tr_vbstr_t;

typedef struct {
    int64_t open_us;
    int64_t hhmmss;
    double open, high, low, close, volume;
} tr_vbstr_px_t;

typedef struct {
    int64_t time_sec;
    int kind;
    const char *name;
} tr_vbstr_event_t;

void tr_vbstr_default_cfg(tr_vbstr_cfg_t *cfg);
bool tr_vbstr_init(tr_vbstr_t *st, const tr_vbstr_cfg_t *cfg);
/* 실패 시 false. out은 NULL이면 상태만 갱신한다. */
bool tr_vbstr_on_bar(tr_vbstr_t *st, const tr_vbstr_bar_t *bar, tr_vbstr_out_t *out);

/* 오래된 봉이 앞이다. 콜·풋은 open_us가 선물과 같을 때만 있는 봉이다.
 * 성공 시 주문 개수. ev에는 시간순으로 최근 cap개. 실패 시 -1. */
int tr_vbstr_replay(tr_vbstr_t *st, const tr_vbstr_px_t *fut, size_t nfut,
                    const tr_vbstr_px_t *call, size_t ncall, const tr_vbstr_px_t *put, size_t nput,
                    tr_vbstr_event_t *ev, size_t cap);

#endif
