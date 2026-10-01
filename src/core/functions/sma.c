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
    /* memset 다음에 창 저장소를 연결한다 (순서 고정). 유효 용량은 period개 */
    ylv_init(&s->win, s->buf, (size_t)period);
}

void tr_sma_on_bar(tr_sma_t *s, double close, bool is_new_bar) {
    if (s == 0 || s->period <= 0) {
        return;
    }
    if (is_new_bar || ylv_count(&s->win) == 0) {
        /* 새 봉: 슬롯 push. 가득 차면 가장 오래된 슬롯이 밀려난다 */
        ylv_push(&s->win, close);
    } else {
        /* 진행 봉 재호출: 현재 슬롯만 덮어쓴다 */
        ylv_set_current(&s->win, close);
    }
    size_t count = ylv_count(&s->win);
    if (count >= (size_t)s->period) {
        double sum = 0.0;
        for (size_t i = 0; i < count; i++) {
            double v = 0.0;
            ylv_at(&s->win, count - 1 - i, &v); /* 오래된→최신 합산 순서 유지 */
            sum += v;
        }
        s->value = sum / (double)s->period;
        s->valid = true;
    } else {
        s->value = 0.0;
        s->valid = false;
    }
}

bool tr_sma_relink(tr_sma_t *s) {
    if (s == 0) {
        return false;
    }
    return ylv_relink(&s->win, s->buf);
}
