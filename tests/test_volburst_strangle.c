/* #BG_VolBurst_Strangle_V3. 진입 봉의 포지션은 0, 다음 봉에 익절·소멸. */

#include <string.h>

#include "test_util.h"

#include "core/signals/volburst_strangle.h"

static tr_vbstr_cfg_t short_cfg(void) {
    tr_vbstr_cfg_t c;
    tr_vbstr_default_cfg(&c);
    c.vol.rv_len = 2;
    c.vol.atr_len = 2;
    c.vol.long_len = 3;
    c.vol.range_len = 2;
    c.vol.aft_len = 2;
    c.vol.strong_rv = 2.0;
    c.vol.strong_vol = 1.2;
    c.vol.mid_vol = 99.0;
    c.vol.cooldown = 30;
    c.vol.hold_bars = 10;
    c.vol.aft_on = 0;
    c.use_vol_exit = 0;
    c.use_time_exit = 0;
    return c;
}

static void feed(tr_vbstr_t *st, int64_t hm, double h, double l, double c, double v, int has, double call,
                 double put, int is_new, tr_vbstr_out_t *out) {
    tr_vbstr_bar_t b;
    memset(&b, 0, sizeof(b));
    b.time = hm;
    b.high = h;
    b.low = l;
    b.close = c;
    b.volume = v;
    b.has_call = has;
    b.has_put = has;
    b.call_c = call;
    b.put_c = put;
    b.is_new_bar = is_new;
    TR_CHECK(tr_vbstr_on_bar(st, &b, out));
}

static void quiet(tr_vbstr_t *st, int n) {
    int i;
    tr_vbstr_out_t o;
    for (i = 0; i < n; i++) {
        feed(st, 100000, 10, 9, 9.5, 100, 1, 1.0, 1.0, 1, &o);
        TR_CHECK(o.order == 0);
    }
}

static void test_entry_then_take_next_bar(void) {
    tr_vbstr_t st;
    tr_vbstr_cfg_t c = short_cfg();
    tr_vbstr_out_t o;
    TR_CHECK(tr_vbstr_init(&st, &c));
    quiet(&st, 4);
    feed(&st, 100000, 20, 9, 18, 400, 1, 1.0, 1.0, 1, &o);
    TR_CHECK(o.order != 0 && strcmp(o.order, "양매수진입") == 0);
    TR_CHECK(o.pos == 0);
    feed(&st, 100100, 11, 10, 10.5, 100, 1, 1.2, 1.2, 1, &o);
    TR_CHECK(o.pos == 1);
    TR_CHECK(o.order != 0 && strcmp(o.order, "익절") == 0);
}

static void test_missing_option_skips_entry(void) {
    tr_vbstr_t st;
    tr_vbstr_cfg_t c = short_cfg();
    tr_vbstr_out_t o;
    TR_CHECK(tr_vbstr_init(&st, &c));
    quiet(&st, 4);
    feed(&st, 100000, 20, 9, 18, 400, 0, 1.0, 1.0, 1, &o);
    TR_CHECK(o.order == 0);
    TR_CHECK(o.pos == 0);
}

static void test_same_bar_withdraws_entry(void) {
    tr_vbstr_t st;
    tr_vbstr_cfg_t c = short_cfg();
    tr_vbstr_out_t o;
    TR_CHECK(tr_vbstr_init(&st, &c));
    quiet(&st, 4);
    feed(&st, 100000, 20, 9, 18, 400, 1, 1.0, 1.0, 1, &o);
    TR_CHECK(o.order != 0);
    feed(&st, 100000, 20, 9, 18, 400, 0, 0, 0, 0, &o);
    TR_CHECK(o.order == 0);
    feed(&st, 100100, 11, 10, 10.5, 100, 1, 1.0, 1.0, 1, &o);
    TR_CHECK(o.pos == 0);
    TR_CHECK(o.order != 0 && strcmp(o.order, "양매수진입") == 0);
}

static void test_vol_exit_on_bar_after_zone(void) {
    tr_vbstr_t st;
    tr_vbstr_cfg_t c = short_cfg();
    tr_vbstr_out_t o;
    int i;
    c.vol.hold_bars = 3;
    c.use_vol_exit = 1;
    TR_CHECK(tr_vbstr_init(&st, &c));
    quiet(&st, 4);
    feed(&st, 100000, 20, 9, 18, 400, 1, 1.0, 1.0, 1, &o);
    TR_CHECK(strcmp(o.order, "양매수진입") == 0);
    for (i = 0; i < 3; i++) {
        feed(&st, 100100 + i, 11, 10, 10.5, 100, 1, 1.0, 1.0, 1, &o);
        TR_CHECK(o.order == 0);
        TR_CHECK(o.pos == 1);
    }
    feed(&st, 101000, 11, 10, 10.5, 100, 1, 1.0, 1.0, 1, &o);
    TR_CHECK(o.zone_end == 1);
    TR_CHECK(o.order != 0 && strcmp(o.order, "변동성소멸청산") == 0);
}

static void test_time_exit_needs_forward_window(void) {
    tr_vbstr_t st;
    tr_vbstr_cfg_t c = short_cfg();
    tr_vbstr_out_t o;
    c.use_time_exit = 1;
    c.exit_time = 101000;
    TR_CHECK(tr_vbstr_init(&st, &c));
    quiet(&st, 4);
    feed(&st, 100000, 20, 9, 18, 400, 1, 1.0, 1.0, 1, &o);
    feed(&st, 101000, 11, 10, 10.5, 100, 1, 1.0, 1.0, 1, &o);
    TR_CHECK(o.order != 0 && strcmp(o.order, "시간종료") == 0);

    c.vol.sig_start = 180000;
    c.vol.sig_end = 120000;
    TR_CHECK(tr_vbstr_init(&st, &c));
    quiet(&st, 4);
    feed(&st, 100000, 20, 9, 18, 400, 1, 1.0, 1.0, 1, &o);
    TR_CHECK(o.order != 0 && strcmp(o.order, "양매수진입") == 0);
    feed(&st, 101000, 11, 10, 10.5, 100, 1, 1.0, 1.0, 1, &o);
    TR_CHECK(o.order == 0);
}

static void test_replay_skips_unmatched_option(void) {
    tr_vbstr_t st;
    tr_vbstr_cfg_t c = short_cfg();
    tr_vbstr_px_t fut[6];
    tr_vbstr_px_t call[1];
    tr_vbstr_px_t put[1];
    tr_vbstr_event_t ev[4];
    int n;
    int i;
    TR_CHECK(tr_vbstr_init(&st, &c));
    memset(fut, 0, sizeof(fut));
    memset(call, 0, sizeof(call));
    memset(put, 0, sizeof(put));
    for (i = 0; i < 4; i++) {
        fut[i].open_us = (int64_t)(i + 1) * 1000000LL;
        fut[i].hhmmss = 100000;
        fut[i].high = 10;
        fut[i].low = 9;
        fut[i].close = 9.5;
        fut[i].volume = 100;
    }
    fut[4].open_us = 5 * 1000000LL;
    fut[4].hhmmss = 100000;
    fut[4].high = 20;
    fut[4].low = 9;
    fut[4].close = 18;
    fut[4].volume = 400;
    fut[5] = fut[3];
    fut[5].open_us = 6 * 1000000LL;
    call[0].open_us = fut[4].open_us;
    call[0].close = 1.0;
    put[0].open_us = fut[4].open_us + 1;
    put[0].close = 1.0;
    n = tr_vbstr_replay(&st, fut, 6, call, 1, put, 1, ev, 4);
    TR_CHECK(n == 0);
    put[0].open_us = fut[4].open_us;
    n = tr_vbstr_replay(&st, fut, 6, call, 1, put, 1, ev, 4);
    TR_CHECK(n == 1);
    TR_CHECK(ev[0].kind == TR_VBSTR_ENTRY);
    TR_CHECK(ev[0].time_sec == 5);
    TR_CHECK(strcmp(ev[0].name, "양매수진입") == 0);
}

int main(void) {
    test_entry_then_take_next_bar();
    test_missing_option_skips_entry();
    test_same_bar_withdraws_entry();
    test_vol_exit_on_bar_after_zone();
    test_time_exit_needs_forward_window();
    test_replay_skips_unmatched_option();
    TR_TEST_SUMMARY();
}
