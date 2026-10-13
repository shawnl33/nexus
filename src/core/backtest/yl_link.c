#include "core/backtest/yl_link.h"

#include <string.h>

static const char *BUY_NAME[] = {"매수", "후보매수", "돌파매수", "선행매수", "조정매수"};
static const char *SELL_NAME[] = {"매도", "후보매도", "돌파매도", "선행매도", "조정매도"};

static const char *pick_name(const char *const *names, int n, int kind) {
    if (kind < 0 || kind >= n) {
        return names[0];
    }
    return names[kind];
}

int tr_yl_fxsig_emit(tr_yl_book_t *book, int sig_dir, int sig_exit, int sig_kind, int qty) {
    int n = 0;
    if (book == 0) {
        return -1;
    }
    if (sig_exit != 0) {
        tr_yl_order_t o;
        memset(&o, 0, sizeof(o));
        o.action = sig_exit > 0 ? TR_YL_EXIT_LONG : TR_YL_EXIT_SHORT;
        o.price_type = TR_YL_ONCLOSE;
        o.qty = 0;
        if (sig_exit == 2 || sig_exit == -2) {
            o.name = sig_exit > 0 ? "시간청산매수" : "시간청산매도";
        } else {
            o.name = sig_exit > 0 ? "파랑청산" : "빨강청산";
        }
        if (tr_yl_book_submit(book, &o)) {
            n++;
        }
    }
    if (sig_dir != 0) {
        tr_yl_order_t o;
        memset(&o, 0, sizeof(o));
        o.action = sig_dir > 0 ? TR_YL_BUY : TR_YL_SELL;
        o.price_type = TR_YL_ONCLOSE;
        o.qty = qty > 0 ? qty : 0;
        o.name = sig_dir > 0 ? pick_name(BUY_NAME, 5, sig_kind) : pick_name(SELL_NAME, 5, sig_kind);
        if (tr_yl_book_submit(book, &o)) {
            n++;
        }
    }
    return n;
}

int tr_yl_fxsig_bar(tr_yl_book_t *book, tr_fxos_t *scope, const tr_fxos_input_t *in) {
    tr_yl_bar_t bar;
    int before;
    int qty;
    if (book == 0 || scope == 0 || in == 0) {
        return -1;
    }
    scope->mkt = book->pos.side;
    scope->contracts = book->pos.contracts;
    scope->avg_entry = book->pos.avg;
    memset(&bar, 0, sizeof(bar));
    /* 차트와 거래 표는 초. bar_open 은 마이크로초라 그대로 쓰면 시각이 깨진다. */
    bar.time = (int64_t)(in->bar_open / 1000000);
    bar.open = in->close;
    bar.high = in->high;
    bar.low = in->low;
    bar.close = in->close;
    tr_yl_book_open(book, &bar);
    tr_fxos_eval(scope, in);
    before = tr_yl_book_trade_count(book);
    qty = scope->cfg.order_qty > 0 ? scope->cfg.order_qty : 1;
    tr_yl_fxsig_emit(book, scope->out.sig_dir, scope->out.sig_exit, scope->out.sig_kind, qty);
    tr_yl_book_close(book);
    return tr_yl_book_trade_count(book) - before;
}