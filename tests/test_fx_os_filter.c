#include "test_util.h"

#include <string.h>

#include "core/indicators/fx_os_filter.h"

/* 1분통합판정 V56 필터. 막는 조건이 없으면 등급 3 매수 후보는 그대로 남는다. */
static void test_grade3_buy_passes(void) {
    tr_fxos_filter_cfg_t cfg;
    tr_fxos_filter_in_t in;
    tr_fxos_filter_out_t out;

    tr_fxos_filter_cfg_default(&cfg);
    TR_CHECK(cfg.dst_auto == 1);

    memset(&in, 0, sizeof(in));
    in.cand_dir = 1;
    in.cand_grade = 3;
    tr_fxos_filter(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 1);
    TR_CHECK(out.cand_grade == 3);
}

/* V39·V45. 선행과 구조돌파는 통합상태가 ±100이어도 항상 지운다. */
static void test_lead_and_break_always_drop(void) {
    tr_fxos_filter_cfg_t cfg;
    tr_fxos_filter_in_t in;
    tr_fxos_filter_out_t out;

    tr_fxos_filter_cfg_default(&cfg);
    memset(&in, 0, sizeof(in));
    in.unified = 100;
    in.lead_dir = 1;
    in.brk_dir = 1;
    tr_fxos_filter(&cfg, &in, &out);
    TR_CHECK(out.lead_dir == 0);
    TR_CHECK(out.brk_dir == 0);

    memset(&in, 0, sizeof(in));
    in.unified = -100;
    in.lead_dir = -1;
    in.brk_dir = -1;
    tr_fxos_filter(&cfg, &in, &out);
    TR_CHECK(out.lead_dir == 0);
    TR_CHECK(out.brk_dir == 0);
}

/* V49·V53. 후보B매도와 후보구조매수는 항상 지운다. 반대 방향은 남긴다. */
static void test_grade2_sell_and_grade4_buy_drop(void) {
    tr_fxos_filter_cfg_t cfg;
    tr_fxos_filter_in_t in;
    tr_fxos_filter_out_t out;

    tr_fxos_filter_cfg_default(&cfg);

    memset(&in, 0, sizeof(in));
    in.cand_dir = -1;
    in.cand_grade = 2;
    tr_fxos_filter(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);

    memset(&in, 0, sizeof(in));
    in.cand_dir = 1;
    in.cand_grade = 2;
    tr_fxos_filter(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 1);
    TR_CHECK(out.cand_grade == 2);

    memset(&in, 0, sizeof(in));
    in.cand_dir = 1;
    in.cand_grade = 4;
    tr_fxos_filter(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);

    memset(&in, 0, sizeof(in));
    in.cand_dir = -1;
    in.cand_grade = 4;
    tr_fxos_filter(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == -1);
    TR_CHECK(out.cand_grade == 4);
}

/* V50·V53. 조정 중·강 네 조합은 항상 지운다. 조정A매수_약은 남긴다. */
static void test_adj_mid_and_strong_drop(void) {
    tr_fxos_filter_cfg_t cfg;
    tr_fxos_filter_in_t in;
    tr_fxos_filter_out_t out;

    tr_fxos_filter_cfg_default(&cfg);
    cfg.adj_a_mid_block = 0;

    memset(&in, 0, sizeof(in));
    in.adj_dir = -1;
    in.adj_kind = 1;
    in.adj_grade = 2;
    tr_fxos_filter(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 0);

    memset(&in, 0, sizeof(in));
    in.adj_dir = 1;
    in.adj_kind = 2;
    in.adj_grade = 3;
    tr_fxos_filter(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 0);

    memset(&in, 0, sizeof(in));
    in.adj_dir = -1;
    in.adj_kind = 2;
    in.adj_grade = 2;
    tr_fxos_filter(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 0);

    memset(&in, 0, sizeof(in));
    in.adj_dir = 1;
    in.adj_kind = 1;
    in.adj_grade = 2;
    tr_fxos_filter(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 0);

    memset(&in, 0, sizeof(in));
    in.adj_dir = 1;
    in.adj_kind = 1;
    in.adj_grade = 1;
    tr_fxos_filter(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 1);
    TR_CHECK(out.adj_kind == 1);
    TR_CHECK(out.adj_grade == 1);
}

/* V56. 봉 날짜로 본장 시작을 22:30 / 23:30 으로 고른다. 일요일이 0이다. */
static void test_rth_open_follows_us_dst(void) {
    tr_fxos_filter_cfg_t cfg;

    tr_fxos_filter_cfg_default(&cfg);
    TR_CHECK(tr_fxos_rth_open(20260715, &cfg) == 223000);
    TR_CHECK(tr_fxos_rth_open(20260115, &cfg) == 233000);
    TR_CHECK(tr_fxos_rth_open(20260308, &cfg) == 223000);
    TR_CHECK(tr_fxos_rth_open(20260307, &cfg) == 233000);
    TR_CHECK(tr_fxos_rth_open(20261101, &cfg) == 233000);
    TR_CHECK(tr_fxos_rth_open(20261031, &cfg) == 223000);

    cfg.dst_auto = 0;
    cfg.rth_manual = 223000;
    TR_CHECK(tr_fxos_rth_open(20260115, &cfg) == 223000);
}

static void feed(const tr_fxos_filter_cfg_t *cfg, tr_fxos_filter_in_t *in, tr_fxos_filter_out_t *out) {
    tr_fxos_filter(cfg, in, out);
}

/* V26. 본장 5분 전은 매수 100, 매도 -100 일 때만 후보·조정·전환이 남는다. */
static void test_preopen_needs_unified_100(void) {
    tr_fxos_filter_cfg_t cfg;
    tr_fxos_filter_in_t in;
    tr_fxos_filter_out_t out;

    tr_fxos_filter_cfg_default(&cfg);
    memset(&in, 0, sizeof(in));
    in.date = 20260715;
    in.time = 222400;
    in.unified = 50;
    in.cand_dir = 1;
    in.cand_grade = 3;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 1);

    in.time = 223000;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 1);

    in.time = 222700;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);

    in.unified = 100;
    in.cand_dir = 1;
    in.adj_dir = 1;
    in.adj_kind = 1;
    in.adj_grade = 1;
    in.turn_dir = 1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 1);
    TR_CHECK(out.adj_dir == 1);
    TR_CHECK(out.turn_dir == 1);

    cfg.adj_sell_allow = 0;
    cfg.turn_sell_u = 0;
    cfg.turn_opp_bars = 0;
    in.unified = -50;
    in.cand_dir = -1;
    in.cand_grade = 3;
    in.adj_dir = -1;
    in.adj_kind = 1;
    in.adj_grade = 1;
    in.turn_dir = -1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);
    TR_CHECK(out.adj_dir == 0);
    TR_CHECK(out.turn_dir == 0);

    in.unified = -100;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == -1);
    TR_CHECK(out.adj_dir == -1);
    TR_CHECK(out.turn_dir == -1);
    cfg.adj_sell_allow = 60;
    cfg.turn_sell_u = 1;
    cfg.turn_opp_bars = 10;

    cfg.pre_min = 0;
    in.unified = 50;
    in.cand_dir = 1;
    in.cand_grade = 3;
    in.adj_dir = 0;
    in.turn_dir = 0;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 1);
}

/* V40. 07:00부터 본장+60분 전 조정매도는 지운다. 자정 넘긴 시각은 sTime < 070000 이라 남긴다. */
static void test_adj_sell_blocked_until_rth_plus_hour(void) {
    tr_fxos_filter_cfg_t cfg;
    tr_fxos_filter_in_t in;
    tr_fxos_filter_out_t out;

    tr_fxos_filter_cfg_default(&cfg);
    memset(&in, 0, sizeof(in));
    in.date = 20260715;
    in.adj_dir = -1;
    in.adj_kind = 1;
    in.adj_grade = 1;

    in.time = 232900;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 0);
    in.time = 233000;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == -1);
    in.time = 100;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == -1);
    in.time = 65900;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == -1);

    in.date = 20260115;
    in.time = 235900;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 0);
    in.time = 0;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == -1);

    cfg.adj_sell_allow = 0;
    in.date = 20260715;
    in.time = 220000;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == -1);
}

/* V52·V55. 후보S는 본장부터 150분. 압축중 금지는 기본 0이라 매수를 남긴다. */
static void test_grade5_window_and_squeeze(void) {
    tr_fxos_filter_cfg_t cfg;
    tr_fxos_filter_in_t in;
    tr_fxos_filter_out_t out;

    tr_fxos_filter_cfg_default(&cfg);
    memset(&in, 0, sizeof(in));
    in.date = 20260715;
    in.time = 5900;
    in.cand_dir = 1;
    in.cand_grade = 5;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 1);
    in.cand_dir = -1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == -1);

    in.time = 10000;
    in.cand_dir = 1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);
    in.cand_dir = -1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);

    in.cand_dir = 1;
    in.cand_grade = 3;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 1);

    cfg.s_buy_min = 0;
    cfg.s_sell_min = 0;
    in.cand_grade = 5;
    in.cand_dir = 1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 1);
    in.cand_dir = -1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == -1);

    tr_fxos_filter_cfg_default(&cfg);
    in.time = 223000;
    in.cand_dir = 1;
    in.cand_grade = 5;
    in.sq_bars = 3;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 1);
    cfg.s_buy_sq = 1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);
    in.cand_dir = -1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == -1);
}

/* V48·V49·V55. 후보B는 삼선·두구간·본장 전 금지. 등급 2 매도는 이미 없다. */
static void test_grade2_three_two_and_preopen(void) {
    tr_fxos_filter_cfg_t cfg;
    tr_fxos_filter_in_t in;
    tr_fxos_filter_out_t out;

    tr_fxos_filter_cfg_default(&cfg);
    memset(&in, 0, sizeof(in));
    in.date = 20260715;
    in.time = 223000;
    in.cand_grade = 2;
    in.cand_dir = 1;
    in.three_ratio = 30;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);
    in.cand_dir = -1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);

    in.three_ratio = 29;
    in.two_ratio = 60;
    in.cand_dir = 1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);

    in.two_ratio = 0;
    in.time = 70000;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);
    in.time = 223000;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 1);

    cfg.b_three_max = 0;
    in.three_ratio = 100;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 1);
    cfg.b_three_max = 30;
    in.three_ratio = 0;
    cfg.b_two_max = 0;
    in.two_ratio = 100;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 1);
    cfg.b_two_max = 60;
    in.two_ratio = 0;
    cfg.b_buy_pre = 0;
    in.time = 70000;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 1);
}

/* V14·V17·V25·V54·V27. 후보매도 가격 조건. 각 기준이 0이면 그 조건만 통과한다. */
static void test_sell_price_filters(void) {
    tr_fxos_filter_cfg_t cfg;
    tr_fxos_filter_in_t in;
    tr_fxos_filter_out_t out;

    tr_fxos_filter_cfg_default(&cfg);
    memset(&in, 0, sizeof(in));
    in.cand_dir = -1;
    in.cand_grade = 3;
    in.lead_valid = 1;
    in.lead_pos = -11;
    in.low = 11;
    in.target1 = 10;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);
    cfg.sell_pos_lim = 0;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == -1);

    tr_fxos_filter_cfg_default(&cfg);
    in.lead_pos = 0;
    in.low = 0;
    in.target1 = 0;
    in.width_ratio = 100;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);
    cfg.sell_width_lim = 0;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == -1);

    tr_fxos_filter_cfg_default(&cfg);
    in.width_ratio = 0;
    in.rise_hi = 20;
    in.rise_lo = 10;
    in.fall_hi = 18;
    in.fall_lo = 12;
    in.high = 11;
    in.unified = 0;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);
    cfg.sell_engulf = 0;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == -1);

    tr_fxos_filter_cfg_default(&cfg);
    memset(&in, 0, sizeof(in));
    in.cand_dir = -1;
    in.cand_grade = 8;
    in.close = 100;
    in.target5 = 104;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);
    in.target5 = 105;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == -1);
    in.target5 = 104;
    cfg.brk_gap = 0;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == -1);

    tr_fxos_filter_cfg_default(&cfg);
    memset(&in, 0, sizeof(in));
    in.cand_dir = -1;
    in.cand_grade = 3;
    in.rise_hi = 20;
    in.rise_lo = 10;
    in.fall_hi = 18;
    in.fall_lo = 12;
    in.three_ratio = 100;
    in.two_ratio = 99;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);
    cfg.sell_engulf2 = 0;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == -1);
}

/* V43. 장대봉은 최근 확인봉의 범위가 그 앞 평균의 배수일 때 꼬리·몸통으로 방향을 정한다. */
static void test_tall_bar_drops_chase(void) {
    tr_fxos_filter_cfg_t cfg;
    tr_fxos_filter_in_t in;
    tr_fxos_filter_out_t out;
    double o[23], h[23], l[23], c[23];
    int buy = 0;
    int sell = 0;
    int i;

    for (i = 0; i < 23; i++) {
        o[i] = 10;
        h[i] = 10.5;
        l[i] = 9.5;
        c[i] = 10;
    }
    h[0] = 13;
    l[0] = 10;
    o[0] = 10.2;
    c[0] = 10.1;
    tr_fxos_tall_flags(o, h, l, c, 20, 3, 3, 60, 85, &buy, &sell);
    TR_CHECK(buy == 1);
    TR_CHECK(sell == 0);

    h[0] = 13;
    l[0] = 10;
    o[0] = 12.8;
    c[0] = 12.9;
    tr_fxos_tall_flags(o, h, l, c, 20, 3, 3, 60, 85, &buy, &sell);
    TR_CHECK(buy == 0);
    TR_CHECK(sell == 1);

    tr_fxos_filter_cfg_default(&cfg);
    memset(&in, 0, sizeof(in));
    in.cand_dir = 1;
    in.cand_grade = 3;
    in.amp = 80;
    in.tall_buy = 1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);
    in.cand_dir = -1;
    in.tall_buy = 0;
    in.tall_sell = 1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == 0);
    in.amp = 79;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == -1);
    in.amp = 80;
    cfg.tall_on = 0;
    feed(&cfg, &in, &out);
    TR_CHECK(out.cand_dir == -1);
}

/* V14·V15·V16·V19·V20·V21·V24·V29. 조정 가격 필터. 약 등급만 써서 항상 삭제와 겹치지 않는다. */
static void test_adj_price_filters(void) {
    tr_fxos_filter_cfg_t cfg;
    tr_fxos_filter_in_t in;
    tr_fxos_filter_out_t out;

    tr_fxos_filter_cfg_default(&cfg);
    memset(&in, 0, sizeof(in));
    in.adj_dir = 1;
    in.adj_kind = 1;
    in.adj_grade = 1;
    in.lead_valid = 1;
    in.prev_up = 31;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 0);
    in.prev_up = 30;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 1);

    in.prev_up = 0;
    in.adj_dir = -1;
    in.swing = 90;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 0);
    in.adj_dir = 1;
    in.swing = 100;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 0);
    in.swing = 99;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 1);

    in.swing = 0;
    in.adj_dir = -1;
    in.slope = -21;
    in.width_seen = 1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 0);
    in.width_seen = 0;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == -1);

    memset(&in, 0, sizeof(in));
    in.adj_dir = 1;
    in.adj_kind = 1;
    in.adj_grade = 1;
    in.rise_hi = 20;
    in.rise_lo = 8;
    in.fall_hi = 18;
    in.fall_lo = 10;
    in.close = 19;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 0);

    in.rise_hi = 18;
    in.rise_lo = 10;
    in.fall_hi = 20;
    in.fall_lo = 8;
    in.close = 17;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 0);

    in.rise_hi = 20;
    in.rise_lo = 8;
    in.fall_hi = 18;
    in.fall_lo = 10;
    in.close = 21;
    in.low = 19;
    in.three_ratio = 100;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 0);

    in.low = 21;
    in.three_ratio = 0;
    in.two_ratio = 91;
    in.high = 11;
    in.target5 = 10;
    in.flat_ratio = 91;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 0);
    in.flat_ratio = 90;
    feed(&cfg, &in, &out);
    TR_CHECK(out.adj_dir == 1);
}

/* V30·V33. 기울기가 막 바뀌고 직전 구간의 고저가 핵심가를 넘으면 전환이 된다. */
static void test_turn_qualifies(void) {
    tr_fxos_filter_cfg_t cfg;
    tr_fxos_filter_in_t in;
    tr_fxos_filter_out_t out;

    tr_fxos_filter_cfg_default(&cfg);
    memset(&in, 0, sizeof(in));
    in.lead_valid = 1;
    in.slope = 1;
    in.pos = -1;
    in.fall_lo = 10;
    in.prev_dn = 5;
    in.prior_low = 11;
    in.swing = 49;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 1);

    in.swing = 50;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);
    in.swing = 49;
    in.session_first = 1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);
    in.session_first = 0;
    cfg.turn_buy_on = 0;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);

    tr_fxos_filter_cfg_default(&cfg);
    memset(&in, 0, sizeof(in));
    in.lead_valid = 1;
    in.slope = -1;
    in.pos = 1;
    in.rise_hi = 20;
    in.prev_up = 5;
    in.prior_high = 19;
    in.swing = 49;
    in.unified = -50;
    in.bars_since_buy = 999;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == -1);
    in.prior_high = 20;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);
}

/* V31~V46. 전환 제외. 삼선 회색·압축색·반대전환·통합 -50·직전 -100은 매도만 지운다. */
static void test_turn_exclusions_and_mark(void) {
    tr_fxos_filter_cfg_t cfg;
    tr_fxos_filter_in_t in;
    tr_fxos_filter_out_t out;

    tr_fxos_filter_cfg_default(&cfg);
    memset(&in, 0, sizeof(in));
    in.turn_dir = 1;
    in.two_ratio = 91;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);
    in.turn_dir = -1;
    in.unified = -50;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);

    in.two_ratio = 0;
    in.turn_dir = 1;
    in.unified = -1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);
    in.unified = 0;
    in.sq_bars = 60;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);
    in.sq_bars = 0;
    in.hot_up = 1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);

    in.hot_up = 0;
    in.turn_dir = -1;
    in.unified = 1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);
    in.unified = -50;
    in.sq_bars = 60;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);
    in.sq_bars = 0;
    in.hot_dn = 1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);

    in.hot_dn = 0;
    in.gray = 1;
    in.turn_dir = -1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);
    in.turn_dir = 1;
    in.unified = 0;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 1);

    in.gray = 0;
    in.turn_dir = -1;
    in.unified = -50;
    in.red_n = 6;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);
    in.red_n = 0;
    in.bars_since_buy = 10;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);
    in.bars_since_buy = 11;
    in.unified = 0;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);
    in.unified = -50;
    in.had_m100 = 1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);
    in.had_m100 = 0;
    in.turn_dir = 1;
    in.unified = 0;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 1);

    in.had_p100 = 1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);
    in.had_p100 = 0;
    in.had_m100 = 1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 0);

    in.had_m100 = 0;
    in.turn_dir = 1;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == 1);
    TR_CHECK(out.turn_y == -190);
    TR_CHECK(out.turn_rgb == 0x006400);
    TR_CHECK(out.turn_w == 7);

    in.turn_dir = -1;
    in.unified = -50;
    feed(&cfg, &in, &out);
    TR_CHECK(out.turn_dir == -1);
    TR_CHECK(out.turn_y == 190);
    TR_CHECK(out.turn_rgb == 0x960050);
    TR_CHECK(out.turn_w == 7);
}

/* 우선순위는 후보, 구조돌파, 선행, 전환, 조정. 주문 이름은 등급별로 갈린다. */
static void test_priority_and_order_names(void) {
    int dir = 0;
    int kind = 0;

    tr_fxos_pick(1, 1, 1, -1, -1, &dir, &kind);
    TR_CHECK(dir == 1);
    TR_CHECK(kind == 1);
    tr_fxos_pick(0, 0, 0, 1, -1, &dir, &kind);
    TR_CHECK(dir == 1);
    TR_CHECK(kind == 5);
    tr_fxos_pick(0, 0, 1, 1, 0, &dir, &kind);
    TR_CHECK(dir == 1);
    TR_CHECK(kind == 3);

    TR_CHECK(strcmp(tr_fxos_order_name(1, 1, 8, 0, 0), "후보돌파매수") == 0);
    TR_CHECK(strcmp(tr_fxos_order_name(1, 1, 7, 0, 0), "후보T매수") == 0);
    TR_CHECK(strcmp(tr_fxos_order_name(1, 1, 6, 0, 0), "후보R매수") == 0);
    TR_CHECK(strcmp(tr_fxos_order_name(1, 1, 5, 0, 0), "후보S매수") == 0);
    TR_CHECK(strcmp(tr_fxos_order_name(1, 1, 4, 0, 0), "후보구조매수") == 0);
    TR_CHECK(strcmp(tr_fxos_order_name(1, 1, 3, 0, 0), "후보A매수") == 0);
    TR_CHECK(strcmp(tr_fxos_order_name(1, 1, 2, 0, 0), "후보B매수") == 0);
    TR_CHECK(strcmp(tr_fxos_order_name(1, 1, 1, 0, 0), "후보C매수") == 0);
    TR_CHECK(strcmp(tr_fxos_order_name(1, 1, 0, 0, 0), "후보매수") == 0);
    TR_CHECK(strcmp(tr_fxos_order_name(-1, 1, 8, 0, 0), "후보돌파매도") == 0);
    TR_CHECK(strcmp(tr_fxos_order_name(1, 2, 0, 0, 0), "돌파매수") == 0);
    TR_CHECK(strcmp(tr_fxos_order_name(-1, 3, 0, 0, 0), "선행매도") == 0);
    TR_CHECK(strcmp(tr_fxos_order_name(1, 5, 0, 0, 0), "전환매수") == 0);
    TR_CHECK(strcmp(tr_fxos_order_name(-1, 5, 0, 0, 0), "전환매도") == 0);
    TR_CHECK(strcmp(tr_fxos_order_name(1, 4, 0, 1, 3), "조정A매수_강") == 0);
    TR_CHECK(strcmp(tr_fxos_order_name(-1, 4, 0, 2, 2), "조정B매도_중") == 0);
    TR_CHECK(strcmp(tr_fxos_order_name(1, 4, 0, 1, 1), "조정A매수_약") == 0);
    TR_CHECK(strcmp(tr_fxos_exit_name(1), "파랑청산") == 0);
    TR_CHECK(strcmp(tr_fxos_exit_name(-1), "빨강청산") == 0);
    TR_CHECK(strcmp(tr_fxos_exit_name(2), "시간청산매수") == 0);
    TR_CHECK(strcmp(tr_fxos_exit_name(-2), "시간청산매도") == 0);
}

/* 기어 키는 기본값 위에 덮어쓴다. */
static void test_filter_cfg_set_overlays_defaults(void) {
    tr_fxos_filter_cfg_t cfg;
    tr_fxos_filter_cfg_default(&cfg);
    TR_CHECK(cfg.dst_auto == 1);
    TR_CHECK(cfg.b_buy_pre == 1);
    TR_CHECK(cfg.tall_on == 1);
    TR_CHECK(cfg.turn_buy_on == 1);
    TR_CHECK(cfg.adj_prev_up == 30);
    TR_CHECK(tr_fxos_filter_cfg_set(&cfg, "서머타임자동", 0) == 1);
    TR_CHECK(tr_fxos_filter_cfg_set(&cfg, "후보B매수본장전금지", 0) == 1);
    TR_CHECK(tr_fxos_filter_cfg_set(&cfg, "장대봉필터", 0) == 1);
    TR_CHECK(tr_fxos_filter_cfg_set(&cfg, "전환매수사용", 0) == 1);
    TR_CHECK(tr_fxos_filter_cfg_set(&cfg, "조정매수직전상승제한", 0) == 1);
    TR_CHECK(cfg.dst_auto == 0);
    TR_CHECK(cfg.b_buy_pre == 0);
    TR_CHECK(cfg.tall_on == 0);
    TR_CHECK(cfg.turn_buy_on == 0);
    TR_CHECK(cfg.adj_prev_up == 0);
    TR_CHECK(cfg.turn_sell_on == 1);
    TR_CHECK(tr_fxos_filter_cfg_set(&cfg, "없는키", 1) == 0);
    TR_CHECK(tr_fxos_filter_cfg_set(&cfg, "조정A중하락구조금지", 0) == 0);
    TR_CHECK(tr_fxos_filter_cfg_set(&cfg, "장대봉배수", 9) == 0);
}

/* 목표5 여유 5는 틱 수나 raw 정수가 아니라 가격 5포인트다. */
static void test_breakout_gap_uses_price_units(void) {
    const double units[] = {1, 100};
    const struct { double gap; int want; } cases[] = {{4.75, 0}, {5, -1}, {5.25, -1}};
    for (size_t u = 0; u < sizeof(units) / sizeof(units[0]); u++) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            tr_fxos_filter_cfg_t cfg;
            tr_fxos_filter_in_t in;
            tr_fxos_filter_out_t out;
            tr_fxos_filter_cfg_default(&cfg);
            memset(&in, 0, sizeof(in));
            in.cand_dir = -1;
            in.cand_grade = 8;
            in.close = 7000 * units[u];
            in.target5 = (7000 + cases[i].gap) * units[u];
            in.price_unit = units[u];
            feed(&cfg, &in, &out);
            TR_CHECK(out.cand_dir == cases[i].want);
        }
    }
}

int main(void) {
    test_breakout_gap_uses_price_units();
    test_grade3_buy_passes();
    test_lead_and_break_always_drop();
    test_grade2_sell_and_grade4_buy_drop();
    test_adj_mid_and_strong_drop();
    test_rth_open_follows_us_dst();
    test_preopen_needs_unified_100();
    test_adj_sell_blocked_until_rth_plus_hour();
    test_grade5_window_and_squeeze();
    test_grade2_three_two_and_preopen();
    test_sell_price_filters();
    test_tall_bar_drops_chase();
    test_adj_price_filters();
    test_turn_qualifies();
    test_turn_exclusions_and_mark();
    test_priority_and_order_names();
    test_filter_cfg_set_overlays_defaults();
    TR_TEST_SUMMARY();
}
