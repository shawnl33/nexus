#include "core/backtest/yl_fill.h"

#include <string.h>

static void copy_name(char *dst, const char *src) {
    size_t i = 0;
    if (src == 0) {
        src = "";
    }
    for (; i + 1 < TR_YL_NAME && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int same_order_name(const char *a, const char *b) {
    return a != 0 && b != 0 && a[0] != '\0' && strcmp(a, b) == 0;
}

static void store_pending(tr_yl_pending_t *p, const tr_yl_order_t *order, int64_t time) {
    memset(p, 0, sizeof(*p));
    p->action = order->action;
    p->price_type = order->price_type;
    p->price = order->price;
    p->qty = order->qty;
    p->time = time;
    copy_name(p->name, order->name);
}

static int is_exit(int action) {
    return action == TR_YL_EXIT_LONG || action == TR_YL_EXIT_SHORT;
}

static int order_sign(int action) {
    if (action == TR_YL_BUY || action == TR_YL_EXIT_SHORT) {
        return 1;
    }
    return -1;
}

static int exit_matches(const tr_yl_pos_t *pos, int action) {
    if (action == TR_YL_EXIT_LONG) {
        return pos->side == 1;
    }
    if (action == TR_YL_EXIT_SHORT) {
        return pos->side == -1;
    }
    return 0;
}

void tr_yl_book_init(tr_yl_book_t *book, int default_qty, double tick) {
    memset(book, 0, sizeof(*book));
    book->default_qty = default_qty;
    book->tick = tick;
}

tr_yl_pos_t tr_yl_book_pos(const tr_yl_book_t *book) {
    return book->pos;
}

int tr_yl_book_trade_count(const tr_yl_book_t *book) {
    return book->trade_n;
}

const tr_yl_trade_t *tr_yl_book_trade(const tr_yl_book_t *book, int index) {
    if (index < 0 || index >= book->trade_n) {
        return 0;
    }
    return &book->trades[index];
}

static int push_trade(tr_yl_book_t *book, int64_t time, const char *name, int filled, double price,
                      int qty, int has_pnl, double pnl) {
    tr_yl_trade_t *t;
    if (book->trade_n >= TR_YL_TRADES) {
        return 0;
    }
    t = &book->trades[book->trade_n++];
    memset(t, 0, sizeof(*t));
    t->time = time;
    copy_name(t->name, name);
    t->filled = filled;
    t->price = price;
    t->qty = qty;
    t->has_pnl = has_pnl;
    t->pnl = pnl;
    return 1;
}

/* qty 는 주문 수량. 반대 방향이면 기존 포지션을 먼저 닫고 나머지로 새로 연다. */
static void apply_fill(tr_yl_book_t *book, int sign, int qty, double price, const char *name,
                       int64_t time) {
    int closed = 0;
    int has_pnl = 0;
    double pnl = 0;
    if (book->pos.side == 0) {
        book->pos.side = sign;
        book->pos.contracts = qty;
        book->pos.avg = price;
    } else if (book->pos.side == sign) {
        double total = book->pos.avg * (double)book->pos.contracts + price * (double)qty;
        book->pos.contracts += qty;
        book->pos.avg = total / (double)book->pos.contracts;
    } else {
        closed = qty < book->pos.contracts ? qty : book->pos.contracts;
        if (book->tick > 0 && closed > 0) {
            has_pnl = 1;
            pnl = (price - book->pos.avg) / book->tick * (double)closed * (double)book->pos.side;
        }
        book->pos.contracts -= closed;
        if (book->pos.contracts == 0) {
            book->pos.side = 0;
            book->pos.avg = 0;
        }
        if (qty > closed) {
            book->pos.side = sign;
            book->pos.contracts = qty - closed;
            book->pos.avg = price;
        }
    }
    push_trade(book, time, name, 1, price, qty, has_pnl, pnl);
}

static int resolve_qty(const tr_yl_book_t *book, int action, int qty) {
    if (is_exit(action)) {
        if (!exit_matches(&book->pos, action)) {
            return 0;
        }
        if (qty == 0 || qty > book->pos.contracts) {
            return book->pos.contracts;
        }
        return qty;
    }
    if (qty == 0) {
        return book->default_qty;
    }
    return qty;
}

static int triggered(int sign, int price_type, double order_px, const tr_yl_bar_t *bar, double *fill) {
    int buy = sign > 0;
    if (price_type == TR_YL_MARKET) {
        *fill = bar->open;
        return 1;
    }
    if (price_type == TR_YL_STOP) {
        if (buy) {
            if (bar->open >= order_px) {
                *fill = bar->open;
                return 1;
            }
            if (bar->high >= order_px) {
                *fill = order_px;
                return 1;
            }
        } else if (bar->open <= order_px) {
            *fill = bar->open;
            return 1;
        } else if (bar->low <= order_px) {
            *fill = order_px;
            return 1;
        }
        return 0;
    }
    if (price_type == TR_YL_LIMIT) {
        if (buy) {
            if (bar->open <= order_px) {
                *fill = bar->open;
                return 1;
            }
            if (bar->low <= order_px) {
                *fill = order_px;
                return 1;
            }
        } else if (bar->open >= order_px) {
            *fill = bar->open;
            return 1;
        } else if (bar->high >= order_px) {
            *fill = order_px;
            return 1;
        }
    }
    return 0;
}

void tr_yl_book_open(tr_yl_book_t *book, const tr_yl_bar_t *bar) {
    int i = 0;
    book->bar = *bar;
    book->have_bar = 1;
    while (i < book->queue_n) {
        tr_yl_pending_t *p = &book->queue[i];
        int sign = order_sign(p->action);
        int qty = resolve_qty(book, p->action, p->qty);
        double fill = 0;
        if (qty > 0 && triggered(sign, p->price_type, p->price, bar, &fill)) {
            int n;
            apply_fill(book, sign, qty, fill, p->name, bar->time);
            n = book->queue_n - i - 1;
            if (n > 0) {
                memmove(&book->queue[i], &book->queue[i + 1], (size_t)n * sizeof(book->queue[0]));
            }
            book->queue_n--;
        } else if (qty == 0 && is_exit(p->action)) {
            int n = book->queue_n - i - 1;
            if (n > 0) {
                memmove(&book->queue[i], &book->queue[i + 1], (size_t)n * sizeof(book->queue[0]));
            }
            book->queue_n--;
        } else {
            i++;
        }
    }
}

int tr_yl_book_submit(tr_yl_book_t *book, const tr_yl_order_t *order) {
    int i;
    if (book == 0 || order == 0 || !book->have_bar) {
        return 0;
    }
    if (order->qty < 0) {
        return 0;
    }
    if (!is_exit(order->action) && order->qty == 0 && book->default_qty < 1) {
        return 0;
    }
    if (order->price_type == TR_YL_ONCLOSE) {
        tr_yl_order_t *dst;
        for (i = 0; i < book->onclose_n; i++) {
            if (!same_order_name(book->onclose_name[i], order->name)) {
                continue;
            }
            dst = &book->onclose[i];
            *dst = *order;
            copy_name(book->onclose_name[i], order->name);
            dst->name = book->onclose_name[i];
            return 1;
        }
        if (book->onclose_n >= TR_YL_QUEUE) {
            return 0;
        }
        dst = &book->onclose[book->onclose_n];
        *dst = *order;
        copy_name(book->onclose_name[book->onclose_n], order->name);
        dst->name = book->onclose_name[book->onclose_n];
        book->onclose_n++;
        return 1;
    }
    if (order->price_type != TR_YL_MARKET && order->price_type != TR_YL_STOP &&
        order->price_type != TR_YL_LIMIT) {
        return 0;
    }
    for (i = 0; i < book->queue_n; i++) {
        if (!same_order_name(book->queue[i].name, order->name)) {
            continue;
        }
        store_pending(&book->queue[i], order, book->bar.time);
        return 1;
    }
    if (book->queue_n >= TR_YL_QUEUE) {
        return 0;
    }
    store_pending(&book->queue[book->queue_n++], order, book->bar.time);
    return 1;
}

void tr_yl_book_close(tr_yl_book_t *book) {
    int i;
    if (!book->have_bar) {
        return;
    }
    for (i = 0; i < book->onclose_n; i++) {
        tr_yl_order_t *o = &book->onclose[i];
        int sign = order_sign(o->action);
        int qty = resolve_qty(book, o->action, o->qty);
        if (qty > 0) {
            apply_fill(book, sign, qty, book->bar.close, o->name, book->bar.time);
        }
    }
    book->onclose_n = 0;
}

void tr_yl_book_finish(tr_yl_book_t *book) {
    int i;
    for (i = 0; i < book->queue_n; i++) {
        tr_yl_pending_t *p = &book->queue[i];
        int qty = p->qty;
        if (qty == 0) {
            qty = is_exit(p->action) ? book->pos.contracts : book->default_qty;
        }
        if (qty < 1) {
            qty = 0;
        }
        push_trade(book, p->time, p->name, 0, 0, qty, 0, 0);
    }
    book->queue_n = 0;
}
