#ifndef TR_YL_LINK_H
#define TR_YL_LINK_H

/* 진입신호매매의 이름만 있는 주문을 종가 주문으로 장부에 넘긴다.
 * 실시간 경로의 포지션 계산은 바꾸지 않는다. 이 함수가 장부 포지션을 식에 넣는다.
 */

#include "core/backtest/yl_fill.h"
#include "core/indicators/fx_os_scope.h"
#include "core/indicators/fx_sig_pair.h"

/* 이미 연 봉에 식의 주문만 넣는다. 청산이 먼저, 진입이 다음이다. 넣은 주문 수. */
int tr_yl_fxsig_emit(tr_yl_book_t *book, int sig_dir, int sig_exit, int sig_kind, int qty,
                     int grade, int adj_kind, int adj_grade);

/* 봉을 열고 식을 돌린 뒤 종가 주문으로 닫는다. 이번 봉에서 넣은 주문 수. 실패 시 -1. */
int tr_yl_fxsig_bar(tr_yl_book_t *book, tr_fxos_t *scope, const tr_fxos_input_t *in);
int tr_yl_fxpair_bar(tr_yl_book_t *book, tr_fxos_t *scope, tr_fxpair_t *pair, const tr_fxos_input_t *in);

/* 가상포지션 주문을 장부에 넣는다. limit_filled 이면 1차를 다시 넣지 않는다.
 * 청산 수량이 0이면 넣지 않는다. 넣은 주문 수. 인자가 없으면 -1. */
int tr_yl_fxpair_orders(tr_yl_book_t *book, const tr_fxpair_out_t *out, int limit_filled);

#endif
