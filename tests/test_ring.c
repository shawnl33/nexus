/* Ring Buffer 경계 테스트 (계획서 §26: 빈 상태, 용량 1, 순환·덮어쓰기, 범위 초과, 현재 봉 반복 갱신) */

#include "test_util.h"

#include "core/market/ring.h"

#define CAP 4
static int storage[CAP];

static void test_empty_state(void) {
    tr_ring rb;
    TR_CHECK(tr_ring_init(&rb, storage, sizeof(int), CAP));
    TR_CHECK(tr_ring_count(&rb) == 0);
    TR_CHECK(!tr_ring_is_full(&rb));
    int out = -1;
    TR_CHECK(!tr_ring_at(&rb, 0, &out));      /* 빈 상태 조회 실패 */
    TR_CHECK(!tr_ring_update_newest(&rb, &out)); /* 빈 상태 갱신 실패 */
}

static void test_invalid_init(void) {
    tr_ring rb;
    int v = 0;
    TR_CHECK(!tr_ring_init(&rb, 0, sizeof(int), CAP));
    TR_CHECK(!tr_ring_init(&rb, storage, 0, CAP));
    TR_CHECK(!tr_ring_init(&rb, storage, sizeof(int), 0));
    TR_CHECK(!tr_ring_push(&rb, 0));
    TR_CHECK(!tr_ring_at(&rb, 0, 0));
    (void)v;
}

static void test_push_and_order(void) {
    tr_ring rb;
    tr_ring_init(&rb, storage, sizeof(int), CAP);
    for (int i = 1; i <= 3; i++) {
        TR_CHECK(!tr_ring_push(&rb, &i)); /* 아직 가득 차지 않음 */
    }
    TR_CHECK(tr_ring_count(&rb) == 3);
    int out;
    TR_CHECK(tr_ring_at(&rb, 0, &out) && out == 3); /* 최신 */
    TR_CHECK(tr_ring_at(&rb, 1, &out) && out == 2);
    TR_CHECK(tr_ring_at(&rb, 2, &out) && out == 1);
    TR_CHECK(!tr_ring_at(&rb, 3, &out)); /* 범위 초과 */
}

static void test_overwrite_eviction(void) {
    tr_ring rb;
    tr_ring_init(&rb, storage, sizeof(int), CAP);
    for (int i = 1; i <= CAP; i++) {
        TR_CHECK(!tr_ring_push(&rb, &i));
    }
    TR_CHECK(tr_ring_is_full(&rb));

    int v = 99;
    TR_CHECK(tr_ring_push(&rb, &v)); /* 1이 밀려남 */
    TR_CHECK(tr_ring_count(&rb) == CAP);
    int out;
    TR_CHECK(tr_ring_at(&rb, 0, &out) && out == 99);
    TR_CHECK(tr_ring_at(&rb, CAP - 1, &out) && out == 2); /* 가장 오래된 값 */
    TR_CHECK(!tr_ring_at(&rb, CAP, &out));

    /* 추가로 2개 밀어내고 순환 확인 */
    v = 100; tr_ring_push(&rb, &v);
    v = 101; tr_ring_push(&rb, &v);
    TR_CHECK(tr_ring_at(&rb, 0, &out) && out == 101);
    TR_CHECK(tr_ring_at(&rb, 1, &out) && out == 100);
    TR_CHECK(tr_ring_at(&rb, 2, &out) && out == 99);
    TR_CHECK(tr_ring_at(&rb, 3, &out) && out == 4);
    TR_CHECK(rb.total_pushed == CAP + 3);
}

static void test_capacity_one(void) {
    int one_slot;
    tr_ring rb;
    TR_CHECK(tr_ring_init(&rb, &one_slot, sizeof(int), 1));
    int v = 1;
    TR_CHECK(!tr_ring_push(&rb, &v));
    v = 2;
    TR_CHECK(tr_ring_push(&rb, &v)); /* 즉시 덮어쓰기 */
    int out;
    TR_CHECK(tr_ring_at(&rb, 0, &out) && out == 2);
    TR_CHECK(!tr_ring_at(&rb, 1, &out));
}

static void test_update_newest_keeps_history(void) {
    tr_ring rb;
    tr_ring_init(&rb, storage, sizeof(int), CAP);
    for (int i = 1; i <= 3; i++) {
        tr_ring_push(&rb, &i);
    }
    /* 현재(최신) 봉을 반복 갱신핵도 과거 봉이 밀리지 않아야 한다 */
    for (int k = 0; k < 5; k++) {
        int v = 300 + k;
        TR_CHECK(tr_ring_update_newest(&rb, &v));
    }
    TR_CHECK(tr_ring_count(&rb) == 3);
    TR_CHECK(rb.total_pushed == 3); /* update는 push로 세지 않음 */
    int out;
    TR_CHECK(tr_ring_at(&rb, 0, &out) && out == 304);
    TR_CHECK(tr_ring_at(&rb, 1, &out) && out == 2);
    TR_CHECK(tr_ring_at(&rb, 2, &out) && out == 1);
}

static void test_clear(void) {
    tr_ring rb;
    tr_ring_init(&rb, storage, sizeof(int), CAP);
    int v = 7;
    tr_ring_push(&rb, &v);
    tr_ring_clear(&rb);
    TR_CHECK(tr_ring_count(&rb) == 0);
    TR_CHECK(rb.total_pushed == 0);
    int out;
    TR_CHECK(!tr_ring_at(&rb, 0, &out));
}

static void test_get_mut(void) {
    tr_ring rb;
    tr_ring_init(&rb, storage, sizeof(int), CAP);
    for (int i = 1; i <= 3; i++) {
        tr_ring_push(&rb, &i);
    }
    TR_CHECK(tr_ring_get_mut(&rb, 3) == 0); /* 범위 초과 */
    int *p = (int *)tr_ring_get_mut(&rb, 0);
    TR_CHECK(p != 0 && *p == 3);
    *p = 30; /* 제자리 수정이 조회에 반영되어야 한다 */
    int out;
    TR_CHECK(tr_ring_at(&rb, 0, &out) && out == 30);
    p = (int *)tr_ring_get_mut(&rb, 2);
    TR_CHECK(p != 0 && *p == 1);
}

int main(void) {
    test_empty_state();
    test_invalid_init();
    test_push_and_order();
    test_overwrite_eviction();
    test_capacity_one();
    test_update_newest_keeps_history();
    test_clear();
    test_get_mut();
    TR_TEST_SUMMARY();
}
