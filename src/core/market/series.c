#include "core/market/series.h"

bool yls_init(yl_series *s, double *storage, size_t cap) {
    if (s == 0) {
        return false;
    }
    return tr_ring_init(&s->ring, storage, sizeof(double), cap);
}

bool yls_push(yl_series *s, double v) {
    if (s == 0) {
        return false;
    }
    return tr_ring_push(&s->ring, &v);
}

bool yls_set_current(yl_series *s, double v) {
    if (s == 0) {
        return false;
    }
    return tr_ring_update_newest(&s->ring, &v);
}

bool yls_at(const yl_series *s, size_t n, double *out) {
    if (s == 0) {
        return false;
    }
    return tr_ring_at(&s->ring, n, out);
}

size_t yls_count(const yl_series *s) {
    if (s == 0) {
        return 0;
    }
    return tr_ring_count(&s->ring);
}

bool yls_relink(yl_series *s, double *new_storage) {
    if (s == 0 || new_storage == 0) {
        return false;
    }
    s->ring.storage = new_storage;
    return true;
}

void yls_clear(yl_series *s) {
    if (s == 0) {
        return;
    }
    tr_ring_clear(&s->ring);
}
