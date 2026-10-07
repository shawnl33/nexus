#include "test_util.h"

#include <string.h>

#include "core/indicators/fx_entry_cand.h"

static void fill_old(tr_fxec_mem_t *m) {
    memset(m, 0, sizeof(*m));
    m->rel_age = 999;
    m->buy_age = m->sell_age = m->strong_buy_age = m->strong_sell_age = 9999;
}

static void test_t_sell_breaks_blue_low(void) {
    tr_fxec_cfg_t cfg;
    tr_fxec_default_cfg(&cfg);
    tr_fxec_mem_t old, now;
    fill_old(&old);
    old.three_ok = 1;
    old.three_ratio = 30;
    old.three_gap = 20;
    old.sq_ratio1 = 20;
    old.sok = 1;
    old.wpct = 40;
    old.blue = 1;
    old.dn_lo = 100;
    old.close1 = 101;
    old.p_n = 5;
    old.p_prev_ok = 1;
    old.p_prev_lo = 100;
    old.p_prev_hi = 110;
    old.uni[0] = 0;
    tr_fxec_bar_t b;
    memset(&b, 0, sizeof(b));
    b.bars = 10;
    b.cur_bar = 10;
    b.time_hms = 230000;
    b.high = 102;
    b.low = 97;
    b.close = 98;
    b.price_scale = 1;
    b.unified = -100;
    b.v1_ok = 1;
    b.tgt[0] = 100;
    b.tgt[1] = 110;
    b.tgt[2] = 120;
    tr_fxec_out_t o;
    tr_fxec_decide(&now, &old, &cfg, &b, &o);
    TR_CHECK(o.dir == -1);
    TR_CHECK(o.grade == 7);
    TR_CHECK(o.price == 102 + 4);
    TR_CHECK(o.emph == 1);
}

int main(void) {
    test_t_sell_breaks_blue_low();
    TR_TEST_SUMMARY();
}
