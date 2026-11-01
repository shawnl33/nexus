#include "core/backtest/yl_link.h"

#include "core/indicators/fx_os_filter.h"
#include "core/indicators/fx_sig_pair.h"
#include "core/paintbar/fx_judge_paint95.h"

#include <stdlib.h>
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

int tr_yl_fxwr_bar(tr_yl_book_t *book, tr_fxos_t *scope, const tr_fxos_input_t *in, tr_yl_wr_hold_t *hold) {
    tr_yl_bar_t bar;
    int before;
    int qty;
    if (book == 0 || scope == 0 || in == 0 || hold == 0) {
        return -1;
    }
    scope->mkt = book->pos.side;
    scope->contracts = book->pos.contracts;
    scope->avg_entry = book->pos.avg;
    memset(&bar, 0, sizeof(bar));
    bar.time = (int64_t)(in->bar_open / 1000000);
    /* 시가가 스톱·지정가를 지나면 체결가는 시가다. 종가로 바꾸면 갭이 사라진다. */
    bar.open = in->open;
    bar.high = in->high;
    bar.low = in->low;
    bar.close = in->close;
    tr_yl_book_open(book, &bar);
    tr_fxos_eval(scope, in);
    if (scope->wr == 0) {
        scope->wr = (tr_fxwr_t *)calloc(1, sizeof(*scope->wr));
    }
    if (scope->wr != 0) {
        tr_fxwr_bar_t wb;
        memset(&wb, 0, sizeof(wb));
        wb.date = in->date;
        wb.time = in->time;
        wb.bar_open = in->bar_open;
        wb.open = in->open;
        wb.high = in->high;
        wb.low = in->low;
        wb.close = in->close;
        wb.volume = in->volume;
        wb.price_scale = scope->price_scale;
        wb.market = book->pos.side;
        wb.is_new_bar = in->is_new_bar;
        tr_fxwr_on_bar(scope->wr, &wb);
    }
    before = tr_yl_book_trade_count(book);
    (void)hold;
    if (scope->wr != 0) {
        int i;
        for (i = 0; i < scope->wr->ord_n; i++) {
            tr_yl_order_t o;
            const tr_fxwr_ord_t *src = &scope->wr->ords[i];
            memset(&o, 0, sizeof(o));
            o.action = src->action;
            o.price_type = src->price_type;
            o.price = src->price;
            o.qty = src->qty;
            o.name = src->name;
            tr_yl_book_submit(book, &o);
        }
    }
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

static const char *exit_fallback(int code) {
    if (code == -1) {
        return "빨강청산";
    }
    if (code == 2) {
        return "시간청산매수";
    }
    if (code == -2) {
        return "시간청산매도";
    }
    return "파랑청산";
}

static void drop_limits(tr_yl_book_t *book) {
    int i = 0;
    while (i < book->queue_n) {
        if (book->queue[i].price_type != TR_YL_LIMIT) {
            i++;
            continue;
        }
        {
            int n = book->queue_n - i - 1;
            if (n > 0) {
                memmove(&book->queue[i], &book->queue[i + 1], (size_t)n * sizeof(book->queue[0]));
            }
            book->queue_n--;
        }
    }
}

int tr_yl_fxpair_orders(tr_yl_book_t *book, const tr_fxpair_out_t *out, int limit_filled) {
    int n = 0;
    if (book == 0 || out == 0) {
        return -1;
    }
    if (out->exit_code != 0 && out->exit_qty > 0) {
        int action = out->exit_code > 0 ? TR_YL_EXIT_LONG : TR_YL_EXIT_SHORT;
        submit_named(book, action, TR_YL_ONCLOSE, 0, out->exit_qty,
                     out->exit_name != 0 ? out->exit_name : exit_fallback(out->exit_code));
        n++;
    }
    if (out->entry_dir != 0) {
        submit_named(book, out->entry_dir > 0 ? TR_YL_BUY : TR_YL_SELL, TR_YL_ONCLOSE, 0, out->entry_qty, out->entry_name);
        n++;
    }
    /* 1차는 하나만 대기한다. 체결된 뒤에는 다시 넣지 않아 잔량이 남는다. */
    if (limit_filled) {
        drop_limits(book);
    } else if (out->limit_dir != 0 && out->limit_qty > 0) {
        const char *lname = out->limit_name != 0 ? out->limit_name
                            : (out->limit_dir > 0 ? "1차목표청산매수" : "1차목표청산매도");
        drop_limits(book);
        submit_named(book, out->limit_dir > 0 ? TR_YL_EXIT_LONG : TR_YL_EXIT_SHORT, TR_YL_LIMIT, out->limit_px,
                     out->limit_qty, lname);
        n++;
    }
    return n;
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
    tr_fxpair_on_bar(pair, &pin, &pout);
    before = tr_yl_book_trade_count(book);
    tr_yl_fxpair_orders(book, &pout, pair->limit_filled);
    tr_yl_book_close(book);
    return tr_yl_book_trade_count(book) - before;
}
