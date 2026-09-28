#include "core/indicators/linreg_v3.h"

#include <string.h>

#include "core/indicators/linreg.h"

#define TR_LR3_MIN_SAMPLES 5

static uint32_t select_n(tr_compress_t compress, uint32_t interval) {
    switch (compress) {
    case TR_COMPRESS_TICK:
    case TR_COMPRESS_SEC:
        return 30;
    case TR_COMPRESS_MIN:
        if (interval <= 1) {
            return 30;
        }
        if (interval <= 5) {
            return 18;
        }
        if (interval <= 15) {
            return 10;
        }
        if (interval <= 30) {
            return 6;
        }
        return 12;
    case TR_COMPRESS_DAY:
        return 20;
    case TR_COMPRESS_WEEK:
        return 13;
    case TR_COMPRESS_MONTH:
        return 12;
    }
    return 20;
}

bool tr_lr3_init(tr_lr3_t *s, tr_compress_t compress, uint32_t bar_interval,
                 int32_t n1, int32_t n2, int32_t n3) {
    if (s == 0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->compress = compress;
    s->bar_interval = bar_interval;
    s->predict_bars[0] = n1;
    s->predict_bars[1] = n2;
    s->predict_bars[2] = n3;
    s->n = select_n(compress, bar_interval);
    if (!tr_lp4_init(&s->v4, n1, n2, n3)) {
        return false;
    }
    s->validity = TR_VALIDITY_MISSING;
    return true;
}

void tr_lr3_eval(tr_lr3_t *s, const tr_ind_eval_t *ev) {
    if (s == 0 || ev == 0 || ev->bar == 0) {
        return;
    }
    double price = ((double)ev->bar->low + (double)ev->bar->high) / 2.0;
    bool is_new_bar = !s->has_bar || ev->bar->open_time_us != s->last_bar_open;

    if (is_new_bar) {
        bool session_reset = (s->compress == TR_COMPRESS_MIN && s->bar_interval <= 30 &&
                              ev->is_session_first);
        if (session_reset) {
            memset(s->prices, 0, sizeof(s->prices));
            s->valid_count = 0;
        }
        for (int i = 98; i >= 0; i--) {
            s->prices[i + 1] = s->prices[i];
        }
        if (s->valid_count < s->n) {
            s->valid_count++;
        }
        s->last_bar_open = ev->bar->open_time_us;
        s->has_bar = true;
    }
    s->prices[0] = price;

    uint32_t calc = s->valid_count < s->n ? s->valid_count : s->n;
    if (calc >= TR_LR3_MIN_SAMPLES) {
        double y[100];
        for (uint32_t i = 0; i < calc; i++) {
            y[i] = s->prices[calc - 1 - i]; /* 오래된 순, x = 0..calc-1 */
        }
        tr_ols_result_t r;
        tr_ols_fit(y, calc, 0.0, TR_LR3_MIN_SAMPLES, &r);
        s->line = r.current;
        s->slope = r.slope;
        s->line_sign = (r.slope > 0.0) - (r.slope < 0.0);
        s->reg_valid = true;
        s->r2 = r.r2;
        s->residual = r.residual_sd;
        s->validity = TR_VALIDITY_VALID;
    } else {
        /* 원본 기본값: 회귀선=입력가격, 나머지 0, 회귀유효=0 */
        s->line = price;
        s->slope = 0.0;
        s->line_sign = 0;
        s->reg_valid = false;
        s->r2 = 0.0;
        s->residual = 0.0;
        s->validity = TR_VALIDITY_MISSING;
    }

    tr_lp4_input_t in;
    in.cur_line = s->line;
    in.slope = s->slope;
    in.reg_valid = s->reg_valid;
    in.r2 = s->r2;
    in.high = (double)ev->bar->high;
    in.low = (double)ev->bar->low;
    in.close = (double)ev->bar->close;
    in.trading_day = ev->trading_day;
    in.is_new_bar = is_new_bar;
    in.compress_min_le30 = (s->compress == TR_COMPRESS_MIN && s->bar_interval <= 30);
    tr_lp4_eval(&s->v4, &in);
}
