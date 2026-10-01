#include "core/market/var.h"

bool ylv_init(yl_var *s, double *storage, size_t cap) {
    if (s == 0) {
        return false;
    }
    return tr_ring_init(&s->ring, storage, sizeof(double), cap);
}

bool ylv_push(yl_var *s, double v) {
    if (s == 0) {
        return false;
    }
    return tr_ring_push(&s->ring, &v);
}

bool ylv_set_current(yl_var *s, double v) {
    if (s == 0) {
        return false;
    }
    return tr_ring_update_newest(&s->ring, &v);
}

bool ylv_at(const yl_var *s, size_t n, double *out) {
    if (s == 0) {
        return false;
    }
    return tr_ring_at(&s->ring, n, out);
}

size_t ylv_count(const yl_var *s) {
    if (s == 0) {
        return 0;
    }
    return tr_ring_count(&s->ring);
}

bool ylv_relink(yl_var *s, double *new_storage) {
    if (s == 0 || new_storage == 0) {
        return false;
    }
    s->ring.storage = new_storage;
    return true;
}

void ylv_clear(yl_var *s) {
    if (s == 0) {
        return;
    }
    tr_ring_clear(&s->ring);
}
