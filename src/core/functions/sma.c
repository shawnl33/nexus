#include "core/functions/sma.h"

#include <string.h>

#define TR_SMA_MAX_PERIOD 64 /* buf 크기 */

void tr_sma_init(tr_sma_t *s, int period) {
    if (s == 0) {
        return;
    }
    memset(s, 0, sizeof(*s));
    if (period < 1) {
        period = 1;
    }
    if (period > TR_SMA_MAX_PERIOD) {
        period = TR_SMA_MAX_PERIOD;
    }
    s->period = period;
}

void tr_sma_on_bar(tr_sma_t *s, double close, bool is_new_bar) {
    if (s == 0 || s->period <= 0) {
        return;
    }
    if (is_new_bar || s->count == 0) {
        /* 새 봉: 슬롯 push. 가득 차면 가장 오래된 슬롯을 최신 값으로 교체한다 */
        if (s->count < s->period) {
            s->buf[(s->head + s->count) % s->period] = close;
            s->count++;
        } else {
            s->buf[s->head] = close;
            s->head = (s->head + 1) % s->period;
        }
    } else {
        /* 진행 봉 재호출: 현재 슬롯만 덮어쓴다 */
        s->buf[(s->head + s->count - 1) % s->period] = close;
    }
    if (s->count >= s->period) {
        double sum = 0.0;
        for (int i = 0; i < s->count; i++) {
            sum += s->buf[(s->head + i) % s->period];
        }
        s->value = sum / (double)s->period;
        s->valid = true;
    } else {
        s->value = 0.0;
        s->valid = false;
    }
}
