#include "core/functions/fx_wave_adj_v6.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect_i(int got, int want, const char *msg) {
    if (got != want) {
        fprintf(stderr, "fail %s got %d want %d\n", msg, got, want);
        fails++;
    }
}

static void base(tr_fxadj_in_t *in) {
    memset(in, 0, sizeof(*in));
    in->price_scale = 1;
    in->min_leg = 1;
    in->min_trend = 3;
    in->min_opp = 2;
    in->confirm_back = 2;
    in->strong_px = 38.2;
    in->strong_time = 23.6;
    in->mid_px = 61.8;
    in->struct_px = 78.6;
    in->start_b_only = 1;
    in->struct_boost = 1;
}

static void step(tr_fxadj_t *s, tr_fxadj_in_t *in, tr_fxadj_out_t *o, double flat, double prev_flat,
                 double high, double low, double close, double prev_close) {
    in->flat = flat;
    in->flat_prev = prev_flat;
    in->high = high;
    in->low = low;
    in->close = close;
    in->close_prev = prev_close;
    in->new_bar = 1;
    tr_fxadj_eval(s, in, o);
}

static void test_trend_then_kind1_and_replay(void) {
    tr_fxadj_t s;
    tr_fxadj_in_t in;
    tr_fxadj_out_t o;
    tr_fxadj_init(&s);
    base(&in);
    step(&s, &in, &o, 10, 0, 30, 10, 25, 0);
    step(&s, &in, &o, 11, 10, 30, 10, 25, 25);
    step(&s, &in, &o, 12, 11, 30, 10, 25, 25);
    step(&s, &in, &o, 13, 12, 30, 10, 25, 25);
    expect_i(o.valid, 1, "trend valid");
    expect_i(o.trend, 1, "trend up");
    expect_i(o.in_adj, 0, "not adj yet");
    step(&s, &in, &o, 14, 13, 30, 10, 25, 25);
    step(&s, &in, &o, 20, 14, 16, 12, 15, 25);
    step(&s, &in, &o, 21, 20, 16, 12, 15, 15);
    expect_i(o.in_adj, 1, "adj started");
    expect_i(o.sig_dir, 0, "no signal on first down close");
    step(&s, &in, &o, 22, 21, 16, 12, 15, 15);
    step(&s, &in, &o, 16, 22, 20, 12, 18, 15);
    step(&s, &in, &o, 17, 16, 20, 12, 19, 18);
    step(&s, &in, &o, 18, 17, 21, 12, 20, 19);
    expect_i(o.sig_dir, 1, "kind1 dir");
    expect_i(o.sig_kind, 1, "kind1");
    expect_i(o.in_adj, 0, "adj cleared by signal");
    in.new_bar = 0;
    tr_fxadj_eval(&s, &in, &o);
    expect_i(o.sig_dir, 1, "replay keeps kind1");
    expect_i(o.sig_kind, 1, "replay kind");
    expect_i(o.trend, 1, "replay trend");
}

static void test_signal_limit_hides_output_only(void) {
    tr_fxadj_t s;
    tr_fxadj_in_t in;
    tr_fxadj_out_t o;
    tr_fxadj_init(&s);
    base(&in);
    in.signal_limit = 1;
    step(&s, &in, &o, 10, 0, 30, 10, 25, 0);
    step(&s, &in, &o, 11, 10, 30, 10, 25, 25);
    step(&s, &in, &o, 12, 11, 30, 10, 25, 25);
    step(&s, &in, &o, 13, 12, 30, 10, 25, 25);
    step(&s, &in, &o, 14, 13, 30, 10, 25, 25);
    step(&s, &in, &o, 20, 14, 16, 12, 15, 25);
    step(&s, &in, &o, 21, 20, 16, 12, 15, 15);
    step(&s, &in, &o, 22, 21, 16, 12, 15, 15);
    step(&s, &in, &o, 16, 22, 20, 12, 18, 15);
    step(&s, &in, &o, 17, 16, 20, 12, 19, 18);
    step(&s, &in, &o, 18, 17, 21, 12, 20, 19);
    expect_i(o.sig_dir, 0, "limit suppresses");
    expect_i(o.trend, 1, "state kept");
    expect_i(o.in_adj, 0, "adj still ends");
}

static void test_kind2(void) {
    tr_fxadj_t s;
    tr_fxadj_in_t in;
    tr_fxadj_out_t o;
    tr_fxadj_init(&s);
    base(&in);
    in.confirm_back = 99;
    in.flip_opp_pct = 0;
    step(&s, &in, &o, 10, 0, 12, 10, 11, 0);
    step(&s, &in, &o, 11, 10, 13, 11, 12, 11);
    step(&s, &in, &o, 12, 11, 14, 12, 13, 12);
    step(&s, &in, &o, 13, 12, 15, 13, 14, 13);
    step(&s, &in, &o, 14, 13, 16, 14, 15, 14);
    step(&s, &in, &o, 13, 14, 13, 11, 12, 15);
    step(&s, &in, &o, 12, 13, 12, 10, 11, 12);
    step(&s, &in, &o, 13, 12, 15, 13, 14, 11);
    expect_i(o.sig_kind, 2, "kind2");
    expect_i(o.sig_dir, 1, "kind2 dir");
}

static void test_grade_uses_max_mid(void) {
    tr_fxadj_t s;
    tr_fxadj_in_t in;
    tr_fxadj_out_t o;
    tr_fxadj_init(&s);
    base(&in);
    in.up_hi = 10;
    in.up_lo = 4;
    in.up_382 = 9;
    in.dn_hi = 8;
    in.dn_lo = 1;
    in.dn_618 = 6;
    step(&s, &in, &o, 10, 0, 200, 100, 150, 0);
    step(&s, &in, &o, 11, 10, 200, 100, 150, 150);
    step(&s, &in, &o, 12, 11, 200, 100, 150, 150);
    step(&s, &in, &o, 13, 12, 200, 100, 150, 150);
    expect_i(o.trend, 1, "wide trend");
    step(&s, &in, &o, 14, 13, 200, 100, 150, 150);
    step(&s, &in, &o, 140, 14, 200, 130, 130, 150);
    step(&s, &in, &o, 141, 140, 200, 130, 130, 130);
    expect_i(o.in_adj, 1, "pullback adj");
    step(&s, &in, &o, 142, 141, 200, 130, 130, 130);
    step(&s, &in, &o, 143, 142, 200, 130, 150, 130);
    step(&s, &in, &o, 144, 143, 200, 130, 150, 150);
    step(&s, &in, &o, 145, 144, 200, 130, 151, 150);
    expect_i(o.sig_dir, 1, "grade signal");
    expect_i(o.sig_grade, 2, "struct widens mid to 78.6");

    tr_fxadj_init(&s);
    base(&in);
    in.struct_boost = 0;
    step(&s, &in, &o, 10, 0, 200, 100, 150, 0);
    step(&s, &in, &o, 11, 10, 200, 100, 150, 150);
    step(&s, &in, &o, 12, 11, 200, 100, 150, 150);
    step(&s, &in, &o, 13, 12, 200, 100, 150, 150);
    step(&s, &in, &o, 14, 13, 200, 100, 150, 150);
    step(&s, &in, &o, 140, 14, 200, 130, 130, 150);
    step(&s, &in, &o, 141, 140, 200, 130, 130, 130);
    step(&s, &in, &o, 142, 141, 200, 130, 130, 130);
    step(&s, &in, &o, 143, 142, 200, 130, 150, 130);
    step(&s, &in, &o, 144, 143, 200, 130, 150, 150);
    step(&s, &in, &o, 145, 144, 200, 130, 151, 150);
    expect_i(o.sig_grade, 1, "61.8 mid stays grade 1");
}

static void test_flip_spends_up_structure(void) {
    tr_fxadj_t s;
    tr_fxadj_in_t in;
    tr_fxadj_out_t o;
    tr_fxadj_init(&s);
    base(&in);
    in.up_hi = 10;
    in.up_lo = 1;
    in.up_382 = 9;
    in.dn_hi = 8;
    in.dn_lo = 2;
    in.dn_618 = 6;
    step(&s, &in, &o, 10, 0, 12, 10, 11, 0);
    step(&s, &in, &o, 11, 10, 13, 11, 12, 11);
    step(&s, &in, &o, 12, 11, 14, 12, 13, 12);
    step(&s, &in, &o, 13, 12, 15, 13, 14, 13);
    step(&s, &in, &o, 14, 13, 16, 14, 15, 14);
    step(&s, &in, &o, 15, 14, 15, 13, 14, 15);
    step(&s, &in, &o, 16, 15, 6, 4, 5, 14);
    expect_i(o.flipped, 1, "close through start");
    expect_i(o.trend, 0, "trend cleared");
    step(&s, &in, &o, 15, 16, 5, 3, 4, 5);
    step(&s, &in, &o, 14, 15, 4, 2, 3, 4);
    step(&s, &in, &o, 13, 14, 3, 1.5, 2, 3);
    expect_i(o.trend, -1, "spent up structure allows downtrend");
}

static void test_session_reset_clears(void) {
    tr_fxadj_t s;
    tr_fxadj_in_t in;
    tr_fxadj_out_t o;
    tr_fxadj_init(&s);
    base(&in);
    step(&s, &in, &o, 10, 0, 12, 10, 11, 0);
    step(&s, &in, &o, 11, 10, 13, 11, 12, 11);
    step(&s, &in, &o, 12, 11, 14, 12, 13, 12);
    step(&s, &in, &o, 13, 12, 15, 13, 14, 13);
    expect_i(o.trend, 1, "before reset");
    in.session_reset = 1;
    step(&s, &in, &o, 14, 13, 16, 14, 15, 14);
    expect_i(o.trend, 0, "reset trend");
    expect_i(o.valid, 1, "flat still valid");
    expect_i(o.sig_dir, 0, "reset signal");
}

/* V1은 종가가 시작가 위에 있어도 조정 저점이 시작가 아래면 전환한다. */
static void test_v1_turns_on_extreme_not_close(void) {
    tr_fxadj_t s6, s1;
    tr_fxadj_in_t in;
    tr_fxadj_out_t o6, o1;
    tr_fxadj_init(&s6);
    tr_fxadj_init(&s1);
    base(&in);
    step(&s6, &in, &o6, 10, 0, 30, 10, 25, 0);
    step(&s6, &in, &o6, 11, 10, 30, 10, 25, 25);
    step(&s6, &in, &o6, 12, 11, 30, 10, 25, 25);
    step(&s6, &in, &o6, 13, 12, 30, 10, 25, 25);
    step(&s6, &in, &o6, 20, 13, 16, 12, 15, 25);
    step(&s6, &in, &o6, 21, 20, 16, 9, 15, 15);
    expect_i(o6.flipped, 0, "v6 close stays above start");
    expect_i(o6.in_adj, 1, "v6 still adjusting");
    in.rev = 1;
    in.start_b_only = 0;
    in.struct_boost = 0;
    step(&s1, &in, &o1, 10, 0, 30, 10, 25, 0);
    step(&s1, &in, &o1, 11, 10, 30, 10, 25, 25);
    step(&s1, &in, &o1, 12, 11, 30, 10, 25, 25);
    step(&s1, &in, &o1, 13, 12, 30, 10, 25, 25);
    step(&s1, &in, &o1, 20, 13, 16, 12, 15, 25);
    step(&s1, &in, &o1, 21, 20, 16, 9, 15, 15);
    expect_i(o1.flipped, 1, "v1 extreme breaks start");
    expect_i(o1.trend, 0, "v1 trend cleared");
}

/* V5는 종가가 상승최저 이하이면 이탈구조를 끄고, V4는 그 종가로 구조 전환을 본다. */
static void test_v5_invalidates_structure_v4_does_not(void) {
    tr_fxadj_t s4, s5;
    tr_fxadj_in_t in;
    tr_fxadj_out_t o4, o5;
    tr_fxadj_init(&s4);
    tr_fxadj_init(&s5);
    base(&in);
    in.rev = 4;
    in.up_hi = 40;
    in.up_lo = 20;
    in.up_382 = 30;
    in.dn_hi = 22;
    in.dn_lo = 5;
    in.dn_618 = 25;
    step(&s4, &in, &o4, 10, 0, 30, 10, 25, 0);
    step(&s4, &in, &o4, 11, 10, 30, 10, 25, 25);
    step(&s4, &in, &o4, 12, 11, 30, 10, 25, 25);
    step(&s4, &in, &o4, 13, 12, 30, 10, 25, 25);
    step(&s4, &in, &o4, 40, 13, 28, 12, 25, 25);
    step(&s4, &in, &o4, 41, 40, 24, 12, 18, 25);
    expect_i(o4.flipped, 1, "v4 close under structure high");
    in.rev = 5;
    step(&s5, &in, &o5, 10, 0, 30, 10, 25, 0);
    step(&s5, &in, &o5, 11, 10, 30, 10, 25, 25);
    step(&s5, &in, &o5, 12, 11, 30, 10, 25, 25);
    step(&s5, &in, &o5, 13, 12, 30, 10, 25, 25);
    step(&s5, &in, &o5, 40, 13, 28, 12, 25, 25);
    step(&s5, &in, &o5, 41, 40, 24, 12, 18, 25);
    expect_i(o5.flipped, 0, "v5 close through up low drops structure");
    expect_i(o5.in_adj, 1, "v5 stays in adjustment");
}

int main(void) {
    test_trend_then_kind1_and_replay();
    test_signal_limit_hides_output_only();
    test_kind2();
    test_grade_uses_max_mid();
    test_flip_spends_up_structure();
    test_session_reset_clears();
    test_v1_turns_on_extreme_not_close();
    test_v5_invalidates_structure_v4_does_not();
    if (fails) {
        return 1;
    }
    printf("ok\n");
    return 0;
}
