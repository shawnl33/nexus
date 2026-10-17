/* 위클리 합산수익률·프라이스링크. 주문 시뮬과 따로, 첫 만남 선만 확인한다. */

#include "test_util.h"

#include <stdio.h>
#include <string.h>

#include "core/indicators/weekly_plot_v1.h"

#define TUE 20241001
#define WED 20241002
#define FRI 20241004
#define MON 20240930

static int near(double a, double b) {
    double d = a - b;
    if (d < 0.0) {
        d = -d;
    }
    return d < 1e-6;
}

static void fresh(tr_wplot_t *st, int side) {
    tr_wplot_config_t cfg;
    tr_wplot_default_config(&cfg);
    TR_CHECK(tr_wplot_init(st, side, &cfg) == 0);
}

/* 화요일 09:00, 겹침 중간값 9.5가 되는 양매도 기본 봉. */
static tr_wplot_bar_t meet_bar(int64_t date, int64_t t) {
    tr_wplot_bar_t b;
    memset(&b, 0, sizeof(b));
    b.bdate = date;
    b.sdate = date;
    b.stime = t;
    b.compress = 2;
    b.interval = 1;
    b.high = 10.0;
    b.low = 8.0;
    b.close = 9.0;
    b.d3_date = date;
    b.d3_time = t;
    b.d3_compress = 2;
    b.d3_interval = 1;
    b.d3_high = 11.0;
    b.d3_low = 9.0;
    b.d3_close = 10.0;
    b.d2_date = date;
    b.d2_time = t;
    b.d2_compress = 2;
    b.d2_interval = 1;
    b.d2_day_index = 20;
    b.calc_ready = 1;
    b.three_ready = 1;
    b.three_peak = 1.0;
    b.three_ratio = 10.0;
    b.break_ready = 1;
    b.two_max = 1.0;
    b.price_ratio = 50.0;
    return b;
}

static void test_reject(void) {
    tr_wplot_t st;
    tr_wplot_config_t cfg;
    tr_wplot_bar_t b = meet_bar(TUE, 90000);
    tr_wplot_out_t o;
    tr_wplot_default_config(&cfg);
    TR_CHECK(tr_wplot_init(0, TR_WPLOT_SHORT, &cfg) == -1);
    TR_CHECK(tr_wplot_init(&st, 0, &cfg) == -1);
    TR_CHECK(tr_wplot_init(&st, TR_WPLOT_SHORT, 0) == -1);
    TR_CHECK(tr_wplot_eval(0, &b, &o) == -1);
    fresh(&st, TR_WPLOT_SHORT);
    TR_CHECK(tr_wplot_eval(&st, 0, &o) == -1);
    TR_CHECK(tr_wplot_eval(&st, &b, 0) == -1);
}

static void test_short_midpoint_and_return(void) {
    tr_wplot_t st;
    tr_wplot_bar_t b = meet_bar(TUE, 90000);
    tr_wplot_out_t o;
    const double expect = ((1.0 - 8.0 / 9.0) + (1.0 - 12.0 / 10.0)) * 50.0;
    fresh(&st, TR_WPLOT_SHORT);
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 1);
    TR_CHECK(near(o.link, 9.5));
    TR_CHECK(o.ret_on == 1);
    TR_CHECK(near(o.ret, 0.0));

    b.close = 8.0;
    b.d3_close = 12.0;
    b.stime = 90100;
    b.d3_time = 90100;
    b.d2_time = 90100;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 1);
    TR_CHECK(near(o.link, 9.5));
    TR_CHECK(o.ret_on == 1);
    TR_CHECK(near(o.ret, expect));
}

static void test_short_gates(void) {
    tr_wplot_t st;
    tr_wplot_out_t o;
    tr_wplot_bar_t b;

    fresh(&st, TR_WPLOT_SHORT);
    b = meet_bar(MON, 90000);
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 0);
    TR_CHECK(o.ret_on == 0);

    fresh(&st, TR_WPLOT_SHORT);
    b = meet_bar(TUE, 85900);
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 0);

    fresh(&st, TR_WPLOT_SHORT);
    b = meet_bar(TUE, 90000);
    b.d2_compress = 0;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 0);

    fresh(&st, TR_WPLOT_SHORT);
    st.cfg.trade_off = 1;
    b = meet_bar(TUE, 90000);
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 0);
}

static void test_short_keeps_first_then_new_day(void) {
    tr_wplot_t st;
    tr_wplot_out_t o;
    tr_wplot_bar_t b = meet_bar(TUE, 90000);
    fresh(&st, TR_WPLOT_SHORT);
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);

    b.stime = 90100;
    b.d3_time = 90100;
    b.d2_time = 90100;
    b.high = 20.0;
    b.low = 1.0;
    b.close = 8.0;
    b.d3_high = 30.0;
    b.d3_low = 2.0;
    b.d3_close = 12.0;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(near(o.link, 9.5));
    TR_CHECK(near(o.ret, ((1.0 - 8.0 / 9.0) + (1.0 - 12.0 / 10.0)) * 50.0));

    b.d3_compress = 0;
    b.d3_close = 0.0;
    b.stime = 90200;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 1);
    TR_CHECK(near(o.link, 9.5));
    TR_CHECK(o.ret_on == 0);

    b = meet_bar(WED, 90000);
    b.high = 6.0;
    b.low = 4.0;
    b.close = 5.0;
    b.d3_high = 7.0;
    b.d3_low = 5.0;
    b.d3_close = 6.0;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 1);
    TR_CHECK(near(o.link, 5.5));
    TR_CHECK(near(o.ret, 0.0));
}

static void test_short_mode2(void) {
    tr_wplot_t st;
    tr_wplot_out_t o;
    tr_wplot_bar_t b = meet_bar(TUE, 90000);
    fresh(&st, TR_WPLOT_SHORT);
    st.cfg.meet_mode = 2;
    st.cfg.tolerance = 3.0;
    b.d3_high = 20.0;
    b.d3_low = 18.0;
    b.d3_close = 12.0;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 1);
    TR_CHECK(near(o.link, 10.5));
}

static void test_long_three_gates(void) {
    tr_wplot_t st;
    tr_wplot_out_t o;
    tr_wplot_bar_t b;
    const double up = ((8.0 / 9.0 - 1.0) + (12.0 / 10.0 - 1.0)) * 50.0;

    fresh(&st, TR_WPLOT_LONG);
    b = meet_bar(TUE, 90000);
    b.d2_day_index = 14;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 0);

    b.d2_day_index = 15;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(near(o.link, 9.5));
    TR_CHECK(near(o.ret, 0.0));
    b.stime = 90100;
    b.d3_time = 90100;
    b.d2_time = 90100;
    b.close = 8.0;
    b.d3_close = 12.0;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(near(o.ret, up));

    fresh(&st, TR_WPLOT_LONG);
    b = meet_bar(TUE, 90000);
    b.d2_day_index = 15;
    b.three_ratio = 70.0;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 0);
    b.three_ratio = 69.0;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 1);

    fresh(&st, TR_WPLOT_LONG);
    b = meet_bar(TUE, 120000);
    b.d2_day_index = 195;
    b.three_ratio = 50.0;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 0);
    b.three_ratio = 49.0;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 1);

    fresh(&st, TR_WPLOT_LONG);
    b = meet_bar(TUE, 90000);
    b.d2_day_index = 15;
    b.price_ratio = 100.0;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 1);
}

static void test_long_friday_morning(void) {
    tr_wplot_t st;
    tr_wplot_out_t o;
    tr_wplot_bar_t b;

    fresh(&st, TR_WPLOT_LONG);
    b = meet_bar(FRI, 90000);
    b.d2_day_index = 15;
    b.price_ratio = 100.0;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 0);

    b.d2_time = 85900;
    b.price_ratio = 99.0;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 0);

    b.d2_time = 90000;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 1);

    fresh(&st, TR_WPLOT_LONG);
    b = meet_bar(FRI, 120000);
    b.d2_day_index = 195;
    b.three_ratio = 40.0;
    b.price_ratio = 100.0;
    TR_CHECK(tr_wplot_eval(&st, &b, &o) == 0);
    TR_CHECK(o.link_on == 1);
}

static tr_wpair_pxbar_t px(int64_t sec, int64_t ymd, int64_t hms, int32_t day, double h, double l,
                           double c) {
    tr_wpair_pxbar_t b;
    memset(&b, 0, sizeof(b));
    b.open_us = sec * 1000000;
    b.ymd = ymd;
    b.hhmmss = hms;
    b.day_index = day;
    b.high = h;
    b.low = l;
    b.close = c;
    b.volume = 1.0;
    return b;
}

static void test_replay_short_and_long(void) {
    tr_wplot_t st;
    tr_wplot_pt_t link[8];
    tr_wplot_pt_t ret[8];
    size_t nl = 0;
    size_t nr = 0;
    tr_wpair_pxbar_t self[6];
    tr_wpair_pxbar_t opp[5];
    tr_wpair_pxbar_t fut[6];
    const int64_t t0 = 1700000000;
    fresh(&st, TR_WPLOT_SHORT);
    self[0] = px(t0, MON, 90000, 20, 10, 8, 9);
    self[1] = px(t0 + 60, TUE, 85900, 14, 10, 8, 9);
    self[2] = px(t0 + 120, TUE, 90000, 15, 10, 8, 9);
    self[3] = px(t0 + 180, TUE, 90100, 16, 10, 8, 8);
    self[4] = px(t0 + 240, TUE, 90200, 17, 10, 8, 8);
    self[5] = px(t0 + 86400, WED, 90000, 20, 6, 4, 5);
    opp[0] = px(t0, MON, 90000, 20, 11, 9, 10);
    opp[1] = px(t0 + 60, TUE, 85900, 14, 11, 9, 10);
    opp[2] = px(t0 + 120, TUE, 90000, 15, 11, 9, 10);
    opp[3] = px(t0 + 180, TUE, 90100, 16, 14, 11, 12);
    opp[4] = px(t0 + 86400, WED, 90000, 20, 7, 5, 6);
    for (int i = 0; i < 6; i++) {
        fut[i] = self[i];
        fut[i].high = 400.0;
        fut[i].low = 399.0;
        fut[i].close = 400.0;
    }
    TR_CHECK(tr_wplot_replay(&st, self, 6, opp, 5, fut, 6, 0, link, 8, &nl, ret, 8, &nr) == 0);
    TR_CHECK(nl == 4);
    TR_CHECK(nr == 3);
    TR_CHECK(link[0].time_sec == t0 + 120 && near(link[0].value, 9.5));
    TR_CHECK(link[3].time_sec == t0 + 86400 && near(link[3].value, 5.5));
    TR_CHECK(near(ret[0].value, 0.0));
    TR_CHECK(near(ret[1].value, ((1.0 - 8.0 / 9.0) + (1.0 - 12.0 / 10.0)) * 50.0));
    TR_CHECK(ret[1].time_sec == t0 + 180);
    TR_CHECK(ret[2].time_sec == t0 + 86400);

    fresh(&st, TR_WPLOT_LONG);
    nl = nr = 99;
    TR_CHECK(tr_wplot_replay(&st, &self[2], 1, &opp[2], 1, &fut[2], 1, 0, link, 8, &nl, ret, 8,
                             &nr) == 0);
    TR_CHECK(nl == 0);
    TR_CHECK(nr == 0);
}

static void test_pack(void) {
    tr_wplot_pt_t ll[3];
    tr_wplot_pt_t lr[3];
    tr_wplot_pt_t gap[3];
    tr_wplot_pt_t many[30];
    char buf[256];
    char big[100];
    char slice[100];
    ll[0] = (tr_wplot_pt_t){1000, 9.5};
    ll[1] = (tr_wplot_pt_t){1060, 9.5};
    ll[2] = (tr_wplot_pt_t){1120, 9.5};
    lr[0] = (tr_wplot_pt_t){1000, 0.0};
    lr[1] = (tr_wplot_pt_t){1060, 4.5};
    lr[2] = (tr_wplot_pt_t){1120, -1.0};
    TR_CHECK(tr_wplot_pack(buf, sizeof(buf), 1000, 1120, ll, 3, lr, 3, 0, 0, 0, 0) == 0);
    TR_CHECK(strcmp(buf, "{\"ll\":[[1000,1120,9.5]],\"lr\":[[1000,0,4.5,-1]],\"sl\":[],\"sr\":[],"
                         "\"t0\":1000,\"t1\":1120}") == 0);

    gap[0] = (tr_wplot_pt_t){1000, 1.0};
    gap[1] = (tr_wplot_pt_t){1060, 2.0};
    gap[2] = (tr_wplot_pt_t){1180, 3.0};
    TR_CHECK(tr_wplot_pack(buf, sizeof(buf), 1000, 1180, 0, 0, gap, 3, 0, 0, 0, 0) == 0);
    TR_CHECK(strstr(buf, "\"lr\":[[1000,1,2],[1180,3]]") != 0);

    for (int i = 0; i < 30; i++) {
        many[i].time_sec = 1700000000 + (int64_t)i * 60;
        many[i].value = (double)i;
    }
    TR_CHECK(tr_wplot_pack(big, sizeof(big), many[0].time_sec, many[29].time_sec, 0, 0, many, 30, 0,
                           0, 0, 0) == 0);
    TR_CHECK(strlen(big) < sizeof(big));
    TR_CHECK(big[0] == '{');
    TR_CHECK(big[strlen(big) - 1] == '}');
    {
        const char *lrj = strstr(big, "\"lr\":");
        const char *sle = strstr(big, ",\"sl\":");
        size_t nslice;
        TR_CHECK(lrj != 0 && sle != 0 && sle > lrj);
        nslice = (size_t)(sle - lrj);
        TR_CHECK(nslice < sizeof(slice));
        memcpy(slice, lrj, nslice);
        slice[nslice] = '\0';
        TR_CHECK(strstr(slice, "1700000000") == 0);
        TR_CHECK(strstr(slice, "29") != 0);
    }
    TR_CHECK(tr_wplot_pack(buf, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == -1);
}

int main(void) {
    test_reject();
    test_short_midpoint_and_return();
    test_short_gates();
    test_short_keeps_first_then_new_day();
    test_short_mode2();
    test_long_three_gates();
    test_long_friday_morning();
    test_replay_short_and_long();
    test_pack();
    TR_TEST_SUMMARY();
}
