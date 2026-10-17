/* #BG_Vol_burst_V2 강조. 강신호 봉, 진입 구간, 하늘색, 같은 봉 재계산. */

#include "test_util.h"

#include "core/indicators/volburst_paint.h"

static tr_vb_cfg_t short_cfg(void) {
    tr_vb_cfg_t c;
    tr_vb_default_cfg(&c);
    c.rv_len = 2;
    c.atr_len = 2;
    c.long_len = 3;
    c.range_len = 2;
    c.aft_len = 2;
    c.strong_rv = 2.0;
    c.strong_vol = 1.2;
    c.mid_vol = 99.0;
    c.cooldown = 10;
    c.entry_bars = 1;
    c.hold_bars = 3;
    c.show_hold = 1;
    c.aft_on = 0;
    return c;
}

static uint32_t feed(tr_vb_t *s, double h, double l, double c, double v, int64_t hm, int is_new) {
    tr_vb_bar_t b;
    b.time = hm;
    b.high = h;
    b.low = l;
    b.close = c;
    b.volume = v;
    b.is_new_bar = is_new;
    return tr_vb_on_bar(s, &b);
}

static void quiet(tr_vb_t *s, int n) {
    int i;
    for (i = 0; i < n; i++) {
        TR_CHECK(feed(s, 10, 9, 9.5, 100, 100000, 1) == 0);
    }
}

static void test_defaults(void) {
    tr_vb_cfg_t c;
    tr_vb_default_cfg(&c);
    TR_CHECK(c.rv_len == 20);
    TR_CHECK(c.atr_len == 10);
    TR_CHECK(c.long_len == 390);
    TR_CHECK(c.range_len == 30);
    TR_CHECK(c.strong_rv == 2.5);
    TR_CHECK(c.strong_vol == 1.2);
    TR_CHECK(c.mid_vol == 1.3);
    TR_CHECK(c.mid_range == 8.0);
    TR_CHECK(c.mid_rv == 1.5);
    TR_CHECK(c.cooldown == 30);
    TR_CHECK(c.sig_start == 90000);
    TR_CHECK(c.sig_end == 150000);
    TR_CHECK(c.entry_bars == 3);
    TR_CHECK(c.hold_bars == 30);
    TR_CHECK(c.show_hold == 1);
    TR_CHECK(c.aft_on == 1);
    TR_CHECK(c.aft_start == 123000);
    TR_CHECK(c.aft_len == 120);
    TR_CHECK(c.aft_rv == 2.0);
}

static void test_strong_then_zones(void) {
    tr_vb_t s;
    tr_vb_cfg_t c = short_cfg();
    TR_CHECK(tr_vb_init(&s, &c));
    quiet(&s, 4);
    /* 거래량 4배, 진폭 급증. 쿨다운 안이라 다음 큰 거래량도 새 신호가 아니다. */
    TR_CHECK(feed(&s, 20, 9, 18, 400, 100000, 1) == TR_VB_STRONG);
    TR_CHECK(feed(&s, 11, 10, 10.5, 1000, 100000, 1) == TR_VB_STRONG_ZONE);
    TR_CHECK(feed(&s, 11, 10, 10.5, 100, 100000, 1) == TR_VB_HOLD);
    TR_CHECK(feed(&s, 11, 10, 10.5, 100, 100000, 1) == TR_VB_HOLD);
    TR_CHECK(feed(&s, 11, 10, 10.5, 100, 100000, 1) == 0);
}

static void test_same_bar_recompute(void) {
    tr_vb_t s;
    tr_vb_cfg_t c = short_cfg();
    TR_CHECK(tr_vb_init(&s, &c));
    quiet(&s, 4);
    TR_CHECK(feed(&s, 20, 9, 18, 400, 100000, 1) == TR_VB_STRONG);
    /* 같은 봉을 다시 계산하면 봉 수가 늘지 않고, 거래량이 식으면 신호도 빠진다. */
    TR_CHECK(feed(&s, 11, 10, 10.5, 100, 100000, 0) == 0);
    TR_CHECK(feed(&s, 11, 10, 10.5, 100, 100000, 1) == 0);
}

static void test_show_hold_off(void) {
    tr_vb_t s;
    tr_vb_cfg_t c = short_cfg();
    c.show_hold = 0;
    TR_CHECK(tr_vb_init(&s, &c));
    quiet(&s, 4);
    TR_CHECK(feed(&s, 20, 9, 18, 400, 100000, 1) == TR_VB_STRONG);
    TR_CHECK(feed(&s, 11, 10, 10.5, 100, 100000, 1) == TR_VB_STRONG_ZONE);
    TR_CHECK(feed(&s, 11, 10, 10.5, 100, 100000, 1) == 0);
}

static void test_afternoon_and_hours(void) {
    tr_vb_t s;
    tr_vb_cfg_t c = short_cfg();
    c.aft_on = 1;
    c.strong_rv = 100.0;
    c.aft_rv = 1.5;
    TR_CHECK(tr_vb_init(&s, &c));
    quiet(&s, 4);
    TR_CHECK(feed(&s, 20, 9, 18, 400, 130000, 1) == TR_VB_STRONG);

    TR_CHECK(tr_vb_init(&s, &c));
    quiet(&s, 4);
    TR_CHECK(feed(&s, 20, 9, 18, 400, 160000, 1) == 0);
}

static void test_mid_level(void) {
    tr_vb_t s;
    tr_vb_cfg_t c = short_cfg();
    c.strong_rv = 100.0;
    c.strong_vol = 99.0;
    c.mid_vol = 1.0;
    c.mid_range = 1.0;
    c.mid_rv = 1.5;
    TR_CHECK(tr_vb_init(&s, &c));
    quiet(&s, 4);
    TR_CHECK(feed(&s, 20, 9, 18, 400, 100000, 1) == TR_VB_MID);
    TR_CHECK(feed(&s, 11, 10, 10.5, 100, 100000, 1) == TR_VB_MID_ZONE);
}

static void test_reject_long_window(void) {
    tr_vb_t s;
    tr_vb_cfg_t c;
    tr_vb_default_cfg(&c);
    c.long_len = 391;
    TR_CHECK(!tr_vb_init(&s, &c));
}

int main(void) {
    test_defaults();
    test_strong_then_zones();
    test_same_bar_recompute();
    test_show_hold_off();
    test_afternoon_and_hours();
    test_mid_level();
    test_reject_long_window();
    TR_TEST_SUMMARY();
}
