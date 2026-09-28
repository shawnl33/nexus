#include "core/market/ring.h"

#include <string.h>

bool tr_ring_init(tr_ring *rb, void *storage, size_t elem_size, size_t capacity) {
    if (rb == 0 || storage == 0 || elem_size == 0 || capacity == 0) {
        return false;
    }
    rb->storage = storage;
    rb->elem_size = elem_size;
    rb->capacity = capacity;
    rb->count = 0;
    rb->head = 0;
    rb->total_pushed = 0;
    return true;
}

bool tr_ring_push(tr_ring *rb, const void *elem) {
    if (rb == 0 || elem == 0) {
        return false;
    }
    bool evicted = (rb->count == rb->capacity);
    size_t slot;
    if (evicted) {
        slot = rb->head; /* 가장 오래된 슬롯을 재사용 */
        rb->head = (rb->head + 1) % rb->capacity;
    } else {
        slot = (rb->head + rb->count) % rb->capacity;
        rb->count++;
    }
    memcpy((char *)rb->storage + slot * rb->elem_size, elem, rb->elem_size);
    rb->total_pushed++;
    return evicted;
}

bool tr_ring_update_newest(tr_ring *rb, const void *elem) {
    if (rb == 0 || elem == 0 || rb->count == 0) {
        return false;
    }
    size_t slot = (rb->head + rb->count - 1) % rb->capacity;
    memcpy((char *)rb->storage + slot * rb->elem_size, elem, rb->elem_size);
    return true;
}

bool tr_ring_at(const tr_ring *rb, size_t back_index, void *out) {
    if (rb == 0 || out == 0 || back_index >= rb->count) {
        return false;
    }
    size_t slot = (rb->head + rb->count - 1 - back_index) % rb->capacity;
    memcpy(out, (const char *)rb->storage + slot * rb->elem_size, rb->elem_size);
    return true;
}

size_t tr_ring_count(const tr_ring *rb) {
    return rb == 0 ? 0 : rb->count;
}

bool tr_ring_is_full(const tr_ring *rb) {
    return rb != 0 && rb->count == rb->capacity;
}

void tr_ring_clear(tr_ring *rb) {
    if (rb == 0) {
        return;
    }
    rb->count = 0;
    rb->head = 0;
    rb->total_pushed = 0;
}
