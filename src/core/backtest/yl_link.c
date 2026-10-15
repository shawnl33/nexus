#include "core/backtest/yl_link.h"

#include "core/indicators/fx_os_filter.h"
#include "core/indicators/fx_sig_pair.h"

#include <string.h>

int tr_yl_fxsig_emit(tr_yl_book_t *book, int sig_dir, int sig_exit, int sig_kind, int qty,
                     int grade, int adj_kind, int adj_grade) {
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
        if (sig_kind <= 0) {
            o.name = sig_dir > 0 ? "매수" : "매도";
        } else {
            o.name = tr_fxos_order_name(sig_dir, sig_kind, grade, adj_kind, adj_grade);
        }
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
    tr_yl_fxsig_emit(book, scope->out.sig_dir, scope->out.sig_exit, scope->out.sig_kind, qty,
                     scope->out.sig_grade, scope->out.sig_adj_kind, scope->out.sig_adj_grade);
    tr_yl_book_close(book);
    return tr_yl_book_trade_count(book) - before;
}

static void submit_named(tr_yl_book_t *book, int action, int price_type, double price, int qty, const char *name) {
    tr_yl_order_t o;
    memset(&o, 0, sizeof(o));
    o.action = action;
    o.price_type = price_type;
    o.price = price;
    o.qty = qty;
    o.name = name;
    tr_yl_book_submit(book, &o);
}

int tr_yl_fxpair_bar(tr_yl_book_t *book, tr_fxos_t *scope, tr_fxpair_t *pair, const tr_fxos_input_t *in) {
    tr_yl_bar_t bar;
    tr_fxpair_in_t pin;
    tr_fxpair_out_t pout;
    int before;
    if (book == 0 || scope == 0 || pair == 0 || in == 0) {
        return -1;
    }
    scope->mkt = book->pos.side;
    scope->contracts = book->pos.contracts;
    scope->avg_entry = book->pos.avg;
    memset(&bar, 0, sizeof(bar));
    bar.time = (int64_t)(in->bar_open / 1000000);
    bar.open = in->close;
    bar.high = in->high;
    bar.low = in->low;
    bar.close = in->close;
    tr_yl_book_open(book, &bar);
    tr_fxos_eval(scope, in);
    memset(&pin, 0, sizeof(pin));
    pin.dir = scope->out.raw_dir;
    pin.kind = scope->out.raw_kind;
    pin.grade = scope->out.sig_grade;
    pin.adj_kind = scope->out.sig_adj_kind;
    pin.adj_grade = scope->out.sig_adj_grade;
    pin.unified = scope->out.judge;
    pin.slope = scope->out.flat_slope;
    pin.time_hms = (int)in->time;
    pin.rth_hms = scope->out.rth_open;
    pin.session_first = scope->out.session_first;
    pin.color_dir = scope->out.color_dir;
    pin.high = in->high;
    pin.low = in->low;
    pin.close = in->close;
    pin.price_scale = scope->price_scale > 0 ? scope->price_scale : 1;
    pin.price_unit = in->price_unit;
    pin.rise_hi = scope->applied.rise_hi;
    pin.rise_lo = scope->applied.rise_lo;
    pin.fall_hi = scope->applied.fall_hi;
    pin.fall_lo = scope->applied.fall_lo;
    pin.width_ratio = scope->applied.width_ratio;
    pin.swing = scope->applied.swing;
    pin.lead_valid = scope->out.flat_on;
    pin.pos = scope->out.flat_pos;
    pin.di = scope->out.di;
    pin.prev_dn = scope->out.flat_dn_slope < 0 ? -scope->out.flat_dn_slope : scope->out.flat_dn_slope;
    pin.three_ratio = scope->applied.three_ratio;
    pin.sq_hold = scope->applied.sq_bars;
    pin.has_market = 1;
    pin.market = book->pos.side;
    tr_fxpair_on_bar(pair, &pin, &pout);
    before = tr_yl_book_trade_count(book);
    if (pout.exit_code != 0) {
        int action = pout.exit_code > 0 ? TR_YL_EXIT_LONG : TR_YL_EXIT_SHORT;
        const char *name = "파랑청산";
        if (pout.exit_code == -1) {
            name = "빨강청산";
        } else if (pout.exit_code == 2) {
            name = "시간청산매수";
        } else if (pout.exit_code == -2) {
            name = "시간청산매도";
        }
        submit_named(book, action, TR_YL_ONCLOSE, 0, 0, pout.exit_name != 0 ? pout.exit_name : name);
    }
    if (pout.entry_dir != 0) {
        submit_named(book, pout.entry_dir > 0 ? TR_YL_BUY : TR_YL_SELL, TR_YL_ONCLOSE, 0, pout.entry_qty, pout.entry_name);
    }
    if (pout.limit_dir != 0 && pout.limit_qty > 0) {
        const char *lname = pout.limit_name != 0 ? pout.limit_name
                            : (pout.limit_dir > 0 ? "1차목표청산매수" : "1차목표청산매도");
        submit_named(book, pout.limit_dir > 0 ? TR_YL_EXIT_LONG : TR_YL_EXIT_SHORT, TR_YL_LIMIT, pout.limit_px,
                     pout.limit_qty, lname);
    }
    tr_yl_book_close(book);
    return tr_yl_book_trade_count(book) - before;
}
