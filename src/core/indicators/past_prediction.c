#include "core/indicators/past_prediction.h"

#include <string.h>

bool tr_ppv_init(tr_ppv_t *s, const int32_t predict_bars[3], bool compress_min_le30,
                 tr_ppv_frame_t *storage, size_t capacity) {
    if (s == 0 || predict_bars == 0 || storage == 0) {
        return false;
    }
    int32_t max_pb = predict_bars[0];
    for (int k = 1; k < 3; k++) {
        if (predict_bars[k] > max_pb) {
            max_pb = predict_bars[k];
        }
        if (predict_bars[k] < 0) {
            return false;
        }
    }
    if (max_pb < 0 || (size_t)(max_pb + 1) > capacity) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->predict_bars[0] = predict_bars[0];
    s->predict_bars[1] = predict_bars[1];
    s->predict_bars[2] = predict_bars[2];
    s->compress_min_le30 = compress_min_le30;
    return tr_ring_init(&s->hist, storage, sizeof(tr_ppv_frame_t), capacity);
}

void tr_ppv_on_bar(tr_ppv_t *s, int64_t bar_index, bool session_first, const tr_ppv_frame_t *frame) {
    if (s == 0 || frame == 0) {
        return;
    }
    /* 세션 번호 갱신 (원본 200~205) */
    if (s->compress_min_le30 && session_first && (!s->has_mark || bar_index != s->session_mark)) {
        s->session_no++;
        s->session_mark = bar_index;
        s->has_mark = true;
    }

    /* 현재 봉을 먼저 넣고 k봉 전을 꺼낸다 ([예측봉수k] 의미) */
    tr_ppv_frame_t cur = *frame;
    cur.session_no = s->session_no;
    tr_ring_push(&s->hist, &cur);

    for (int k = 0; k < 3; k++) {
        tr_ppv_frame_t rec;
        if (tr_ring_at(&s->hist, (size_t)s->predict_bars[k], &rec)) {
            s->past_pred[k] = rec.pred_price[k];
            s->past_valid[k] = rec.reg_valid ? 1 : 0;
            s->past_r2[k] = rec.r2;
            s->past_dir[k] = rec.pred_dir[k];
            /* 세션을 넘긴 예측은 무효화 (원본 224~226) */
            if (s->compress_min_le30 && rec.session_no != s->session_no) {
                s->past_valid[k] = 0;
            }
        } else {
            s->past_pred[k] = 0.0;
            s->past_valid[k] = 0;
            s->past_r2[k] = 0.0;
            s->past_dir[k] = 0;
        }
    }
}
