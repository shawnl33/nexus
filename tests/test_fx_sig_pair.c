#include "test_util.h"

#include <string.h>

#include "core/indicators/fx_sig_pair.h"

static void base(tr_fxpair_in_t *in) {
    memset(in, 0, sizeof(*in));
    in->time_hms = 210000;
    in->rth_hms = 223000;
    in->price_scale = 1;
    in->close = 100;
    in->high = 100;
    in->low = 100;
}

static void test_two_lots_and_fifteen_tick_limit(void) {
    tr_fxpair_t s;
    tr_fxpair_in_t in;
    tr_fxpair_out_t out;
    tr_fxpair_init(&s);
    base(&in);
    in.dir = 1;
    in.kind = 1;
    in.grade = 3;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.entry_dir == 1);
    TR_CHECK(out.entry_qty == 2);
    TR_CHECK(strcmp(out.entry_name, "후보A매수") == 0);
    TR_CHECK(out.limit_qty == 1);
    TR_CHECK(out.limit_px == 115);
}

static void test_early_turn_is_blocked(void) {
    tr_fxpair_t s;
    tr_fxpair_in_t in;
    tr_fxpair_out_t out;
    tr_fxpair_init(&s);
    base(&in);
    in.time_hms = 223000;
    in.dir = 1;
    in.kind = 5;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.entry_dir == 0);
}

static void test_opposite_name_and_late_target(void) {
    tr_fxpair_t s;
    tr_fxpair_in_t in;
    tr_fxpair_out_t out;
    tr_fxpair_init(&s);
    base(&in);
    in.dir = 1;
    in.kind = 1;
    in.grade = 1;
    tr_fxpair_on_bar(&s, &in, &out);
    in.color_dir = -1;
    in.dir = 0;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.exit_code == 1);
    in.session_first = 1;
    in.color_dir = 0;
    in.dir = -1;
    in.kind = 1;
    in.grade = 1;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.entry_dir == -1);
    TR_CHECK(strcmp(out.entry_name, "후보C매도_반대방향") == 0);
    TR_CHECK(out.limit_px == 85);
    tr_fxpair_init(&s);
    base(&in);
    in.time_hms = 10000;
    in.dir = 1;
    in.kind = 1;
    in.grade = 1;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.entry_dir == 1);
    TR_CHECK(out.limit_px == 110);
}

static void test_weak_session_uses_one_contract(void) {
    tr_fxpair_t s;
    tr_fxpair_in_t in;
    tr_fxpair_out_t out;
    int i;
    tr_fxpair_init(&s);
    base(&in);
    for (i = 0; i < 10; i++) {
        in.dir = 1;
        in.kind = 1;
        in.grade = 1;
        in.color_dir = 0;
        in.session_first = 0;
        tr_fxpair_on_bar(&s, &in, &out);
        in.dir = 0;
        in.color_dir = -1;
        tr_fxpair_on_bar(&s, &in, &out);
    }
    in.dir = 1;
    in.color_dir = 0;
    in.kind = 1;
    in.grade = 1;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.entry_qty == 1);
    TR_CHECK(strcmp(out.entry_name, "후보C매수_장약세") == 0);
}

static void test_v68_group_name_and_loss_exit(void) {
    tr_fxpair_t s;
    tr_fxpair_in_t in;
    tr_fxpair_out_t out;
    tr_fxpair_init(&s);
    s.v68 = 1;
    base(&in);
    in.time_hms = 220000;
    in.dir = 1;
    in.kind = 1;
    in.grade = 8;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.entry_dir == 1);
    TR_CHECK(out.entry_name != 0 && strcmp(out.entry_name, "돌파매수") == 0);
    TR_CHECK(strcmp(out.limit_name, "1차청산매수") == 0);
    in.dir = 0;
    in.color_dir = -1;
    in.close = 90;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.exit_code == 1);
    TR_CHECK(out.exit_name != 0 && strcmp(out.exit_name, "색청산매수_손절") == 0);
}

static void test_v68_weak_breakout_buy_blocked(void) {
    tr_fxpair_t s;
    tr_fxpair_in_t in;
    tr_fxpair_out_t out;
    int i;
    tr_fxpair_init(&s);
    s.v68 = 1;
    base(&in);
    for (i = 0; i < 10; i++) {
        in.dir = 1;
        in.kind = 1;
        in.grade = 1;
        in.color_dir = 0;
        tr_fxpair_on_bar(&s, &in, &out);
        in.dir = 0;
        in.color_dir = -1;
        in.close = 100;
        tr_fxpair_on_bar(&s, &in, &out);
    }
    in.time_hms = 220000;
    in.dir = 1;
    in.color_dir = 0;
    in.kind = 1;
    in.grade = 8;
    in.close = 100;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.entry_dir == 0);
}

static void short_profit_then(tr_fxpair_t *s, tr_fxpair_in_t *in, tr_fxpair_out_t *out) {
    int i;
    tr_fxpair_init(s);
    s->v68 = 1;
    base(in);
    in->time_hms = 220000;
    in->unified = 100;
    in->color_dir = 1;
    in->sq_hold = 1;
    for (i = 0; i < 30; i++) {
        in->dir = 0;
        tr_fxpair_on_bar(s, in, out);
    }
    in->dir = -1;
    in->kind = 1;
    in->grade = 5;
    in->color_dir = 0;
    in->close = 100;
    in->high = 100;
    in->low = 100;
    tr_fxpair_on_bar(s, in, out);
    in->dir = 0;
    in->color_dir = 1;
    in->close = 90;
    in->low = 90;
    in->sq_hold = 2;
    tr_fxpair_on_bar(s, in, out);
}

/* 매도 수익 청산 후 60봉 안에 압축이 늘고 통합카운트가 30 이상이면 압축매수는 없다. */
static void test_v68_blocks_squeeze_buy_after_short_profit(void) {
    tr_fxpair_t s;
    tr_fxpair_in_t in;
    tr_fxpair_out_t out;
    short_profit_then(&s, &in, &out);
    TR_CHECK(out.exit_code == -1);
    in.dir = 1;
    in.kind = 1;
    in.grade = 5;
    in.color_dir = 1;
    in.unified = 100;
    in.close = 90;
    in.sq_hold = 3;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.entry_dir == 0);
}

/* 같은 조건이어도 돌파매수(등급 8)는 남는다. */
static void test_v68_breakout_buy_survives_squeeze_ban(void) {
    tr_fxpair_t s;
    tr_fxpair_in_t in;
    tr_fxpair_out_t out;
    short_profit_then(&s, &in, &out);
    in.dir = 1;
    in.kind = 1;
    in.grade = 8;
    in.color_dir = 1;
    in.unified = 100;
    in.close = 90;
    in.sq_hold = 3;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.entry_dir == 1);
    TR_CHECK(out.entry_name != 0 && strcmp(out.entry_name, "돌파매수") == 0);
}

/* 매도 진입 다음 봉부터 저가가 1차 목표에 닿으면 그 봉에만 1차청산매도를 낸다. */
static void test_v68_first_target_marks_the_touch_bar(void) {
    tr_fxpair_t s;
    tr_fxpair_in_t in;
    tr_fxpair_out_t out;
    tr_fxpair_init(&s);
    s.v68 = 1;
    base(&in);
    in.time_hms = 220000;
    in.dir = -1;
    in.kind = 1;
    in.grade = 3;
    in.close = 100;
    in.high = 100;
    in.low = 80;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.entry_dir == -1);
    TR_CHECK(out.limit_hit == 0);
    in.dir = 0;
    in.low = 86;
    in.high = 100;
    in.close = 90;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.limit_hit == 0);
    in.low = 85;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.limit_hit == -1);
    TR_CHECK(out.limit_name != 0 && strcmp(out.limit_name, "1차청산매도") == 0);
    TR_CHECK(out.exit_code == 0);
    in.low = 70;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.limit_hit == 0);
}

/* 1계약이 1차 지정가로 이미 없으면 색청산 주문은 없다. 가상포지션은 닫는다. */
static void test_v68_flat_market_skips_color_order(void) {
    tr_fxpair_t s;
    tr_fxpair_in_t in;
    tr_fxpair_out_t out;
    tr_fxpair_init(&s);
    s.v68 = 1;
    base(&in);
    in.dir = -1;
    in.kind = 1;
    in.grade = 3;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(s.virt == -1);
    in.dir = 0;
    in.color_dir = 1;
    in.has_market = 1;
    in.market = 0;
    in.close = 90;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.exit_code == 0);
    TR_CHECK(s.virt == 0);
}

/* 실현손익이 있고 직전 하락 기울기 길이가 40을 넘으면 조정B매도는 없다. */
static void test_v68_long_down_leg_blocks_adj_sell(void) {
    tr_fxpair_t s;
    tr_fxpair_in_t in;
    tr_fxpair_out_t out;
    tr_fxpair_init(&s);
    s.v68 = 1;
    s.pnl_buy = 1;
    base(&in);
    in.dir = -1;
    in.kind = 4;
    in.adj_kind = 2;
    in.adj_grade = 1;
    in.lead_valid = 1;
    in.prev_dn = 41;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.entry_dir == 0);
}

static void test_v68_late_first_entry_and_di(void) {
    tr_fxpair_t s;
    tr_fxpair_in_t in;
    tr_fxpair_out_t out;
    tr_fxpair_init(&s);
    s.v68 = 1;
    base(&in);
    in.time_hms = 14500;
    in.dir = 1;
    in.kind = 1;
    in.grade = 8;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.entry_dir == 0);
    in.dir = -1;
    in.grade = 3;
    s.last_dir = 1;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.entry_dir == 0);
    tr_fxpair_init(&s);
    s.v68 = 1;
    base(&in);
    in.dir = 1;
    in.kind = 5;
    in.di = -1;
    tr_fxpair_on_bar(&s, &in, &out);
    TR_CHECK(out.entry_dir == 0);
}

/* 100틱 수익 후 돌파매도 금지는 직전 거래가 매수일 때만 적용한다. */
static void test_v68_profit_sell_ban_depends_on_previous_side(void) {
    const struct { int side, ticks, grade, want; } cases[] = {
        {1, 100, 8, 0}, {-1, 100, 8, -1}, {1, 99, 8, -1}, {1, 100, 3, -1}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        tr_fxpair_t s;
        tr_fxpair_in_t in;
        tr_fxpair_out_t out;
        tr_fxpair_init(&s);
        s.v68 = 1;
        base(&in);
        in.time_hms = 234000;
        in.dir = cases[i].side;
        in.kind = 1;
        in.grade = 3;
        in.high = in.low = in.close = 1000;
        tr_fxpair_on_bar(&s, &in, &out);
        TR_CHECK(out.entry_dir == cases[i].side);
        in.dir = 0;
        in.color_dir = -cases[i].side;
        in.high = in.low = in.close = 1000 + cases[i].side * cases[i].ticks;
        tr_fxpair_on_bar(&s, &in, &out);
        TR_CHECK(s.move_ticks == cases[i].ticks);
        TR_CHECK(s.virt == 0);
        in.dir = -1;
        in.color_dir = 0;
        in.grade = cases[i].grade;
        tr_fxpair_on_bar(&s, &in, &out);
        TR_CHECK(out.entry_dir == cases[i].want);
    }
}

/* 같은 2계약 손실은 원가격·raw 가격 모두 -2.5포인트 경계에서 차단한다. */
static void test_failed_buy_loss_threshold_uses_price_units(void) {
    const double units[] = {1, 100};
    const struct { double loss; int want; } cases[] = {{1, 1}, {1.25, 0}, {1.5, 0}};
    for (size_t u = 0; u < sizeof(units) / sizeof(units[0]); u++) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            tr_fxpair_t s;
            tr_fxpair_in_t in;
            tr_fxpair_out_t out;
            tr_fxpair_init(&s);
            s.v68 = 1;
            base(&in);
            in.time_hms = 234000;
            in.price_unit = units[u];
            in.price_scale = 0.25 * units[u];
            in.high = in.low = in.close = 7000 * units[u];
            in.dir = 1;
            in.kind = 1;
            in.grade = 3;
            tr_fxpair_on_bar(&s, &in, &out);
            TR_CHECK(out.entry_qty == 2);
            in.dir = 0;
            in.color_dir = -1;
            in.high = in.low = in.close = (7000 - cases[i].loss) * units[u];
            tr_fxpair_on_bar(&s, &in, &out);
            TR_CHECK(s.prev_pnl == -2 * cases[i].loss * units[u]);
            in.dir = 1;
            in.color_dir = 0;
            in.kind = 4;
            in.adj_kind = 1;
            in.adj_grade = 1;
            tr_fxpair_on_bar(&s, &in, &out);
            TR_CHECK(out.entry_dir == cases[i].want);
        }
    }
}

static void test_exit_quantity_before_first_target(void) {
    for (int v68 = 0; v68 <= 1; v68++) {
        for (int dir = -1; dir <= 1; dir += 2) {
            tr_fxpair_t s;
            tr_fxpair_in_t in;
            tr_fxpair_out_t out;
            tr_fxpair_init(&s);
            s.v68 = v68;
            base(&in);
            in.time_hms = 234000;
            in.dir = dir;
            in.kind = 1;
            in.grade = 3;
            tr_fxpair_on_bar(&s, &in, &out);
            TR_CHECK(out.entry_qty == 2);
            in.dir = 0;
            if (dir > 0) in.color_dir = -1;
            else in.time_hms = 20000;
            tr_fxpair_on_bar(&s, &in, &out);
            TR_CHECK(out.exit_code == (dir > 0 ? 1 : -2));
            TR_CHECK(out.exit_qty == 2);
            TR_CHECK(out.limit_hit_qty == 0);
            tr_fxpair_on_bar(&s, &in, &out);
            TR_CHECK(out.exit_qty == 0);
        }
    }
}

static void test_exit_quantity_after_first_target(void) {
    for (int v68 = 0; v68 <= 1; v68++) {
        for (int lots = 1; lots <= 2; lots++) {
            for (int same_bar = 0; same_bar <= 1; same_bar++) {
                tr_fxpair_t s;
                tr_fxpair_in_t in;
                tr_fxpair_out_t out;
                tr_fxpair_init(&s);
                s.v68 = v68;
                base(&in);
                in.time_hms = lots == 1 ? 223000 : 234000;
                in.dir = -1;
                in.kind = 1;
                in.grade = 3;
                tr_fxpair_on_bar(&s, &in, &out);
                TR_CHECK(out.entry_qty == lots);
                in.dir = 0;
                in.low = 85;
                in.close = 90;
                in.color_dir = same_bar ? 1 : 0;
                tr_fxpair_on_bar(&s, &in, &out);
                TR_CHECK(out.limit_hit == -1);
                TR_CHECK(out.limit_hit_qty == 1);
                if (!same_bar) {
                    in.color_dir = 1;
                    tr_fxpair_on_bar(&s, &in, &out);
                    TR_CHECK(out.limit_hit_qty == 0);
                }
                TR_CHECK(out.exit_code == -1);
                TR_CHECK(out.exit_qty == lots - 1);
            }
        }
    }
}

int main(void) {
    test_exit_quantity_before_first_target();
    test_exit_quantity_after_first_target();
    test_v68_profit_sell_ban_depends_on_previous_side();
    test_failed_buy_loss_threshold_uses_price_units();
    test_two_lots_and_fifteen_tick_limit();
    test_early_turn_is_blocked();
    test_opposite_name_and_late_target();
    test_weak_session_uses_one_contract();
    test_v68_group_name_and_loss_exit();
    test_v68_weak_breakout_buy_blocked();
    test_v68_blocks_squeeze_buy_after_short_profit();
    test_v68_breakout_buy_survives_squeeze_ban();
    test_v68_first_target_marks_the_touch_bar();
    test_v68_flat_market_skips_color_order();
    test_v68_long_down_leg_blocks_adj_sell();
    test_v68_late_first_entry_and_di();
    TR_TEST_SUMMARY();
}
