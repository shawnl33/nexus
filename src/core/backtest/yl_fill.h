#ifndef TR_YL_FILL_H
#define TR_YL_FILL_H

/* 예스트레이더 포트의 체결 장부.
 * 종가 주문은 신호를 낸 봉의 종가. AtMarket 은 다음 봉 시가.
 * AtStop, AtLimit 은 다음 봉 고저가 가격에 닿으면 체결. 시가가 그 가격을 지나면 시가.
 * 수수료와 슬리피지는 없다.
 */

#include <stdint.h>

#define TR_YL_BUY 1
#define TR_YL_SELL (-1)
#define TR_YL_EXIT_LONG 2
#define TR_YL_EXIT_SHORT (-2)

#define TR_YL_ONCLOSE 0
#define TR_YL_MARKET 1
#define TR_YL_STOP 2
#define TR_YL_LIMIT 3

#define TR_YL_NAME 64
#define TR_YL_TRADES 64
#define TR_YL_QUEUE 32

typedef struct {
    int64_t time;
    double open, high, low, close;
} tr_yl_bar_t;

typedef struct {
    const char *name;
    int action;
    int price_type;
    double price;
    int qty; /* 0 이면 진입은 기본 수량, 청산은 잔량 */
} tr_yl_order_t;

typedef struct {
    int side; /* 매수 1, 매도 -1, 없음 0 */
    double avg;
    int contracts;
} tr_yl_pos_t;

typedef struct {
    int64_t time;
    char name[TR_YL_NAME];
    int filled;
    double price;
    int qty;
    int has_pnl;
    double pnl;
} tr_yl_trade_t;

typedef struct {
    int action;
    int price_type;
    double price;
    int qty;
    int64_t time;
    char name[TR_YL_NAME];
} tr_yl_pending_t;

typedef struct tr_yl_book {
    int default_qty;
    double tick;
    tr_yl_pos_t pos;
    tr_yl_bar_t bar;
    int have_bar;
    tr_yl_pending_t queue[TR_YL_QUEUE];
    int queue_n;
    tr_yl_order_t onclose[TR_YL_QUEUE];
    char onclose_name[TR_YL_QUEUE][TR_YL_NAME];
    int onclose_n;
    tr_yl_trade_t trades[TR_YL_TRADES];
    int trade_n;
} tr_yl_book_t;

void tr_yl_book_init(tr_yl_book_t *book, int default_qty, double tick);
void tr_yl_book_open(tr_yl_book_t *book, const tr_yl_bar_t *bar);
tr_yl_pos_t tr_yl_book_pos(const tr_yl_book_t *book);
/* 1 이면 주문을 받았다. 0 이면 거부했다. */
int tr_yl_book_submit(tr_yl_book_t *book, const tr_yl_order_t *order);
void tr_yl_book_close(tr_yl_book_t *book);
/* 남은 시장가, 스톱, 지정가를 미체결로 남긴다. */
void tr_yl_book_finish(tr_yl_book_t *book);
int tr_yl_book_trade_count(const tr_yl_book_t *book);
const tr_yl_trade_t *tr_yl_book_trade(const tr_yl_book_t *book, int index);

#endif
