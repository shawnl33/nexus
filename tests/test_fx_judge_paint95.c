/* 1분통합판정강조 V95. 그룹 색과 무거래30 진입. */

#include "test_util.h"

#include "core/paintbar/fx_judge_paint95.h"

#include <string.h>

static void feed(tr_fxp95_t *s, int date, int hm, double o, double h, double l, double c, double v, int judge) {
    tr_fxp95_in_t in;
    memset(&in, 0, sizeof(in));
    in.date = date;
    in.time = hm;
    in.is_new_bar = 1;
    in.open = o;
    in.high = h;
    in.low = l;
    in.close = c;
    in.volume = v;
    in.judge = judge;
    in.fut = judge > 0 ? 1 : (judge < 0 ? -1 : 0);
    in.di = in.fut;
    in.state5 = in.fut;
    tr_fxp95_bar(s, &in);
}

static int hm_add(int hm, int minutes) {
    int h = hm / 10000;
    int m = (hm / 100) % 100;
    int t = h * 60 + m + minutes;
    return (t / 60) * 10000 + (t % 60) * 100;
}

int main(void) {
    tr_fxp95_t s;
    int day, i;
    memset(&s, 0, sizeof(s));
    TR_CHECK(tr_fxp95_color(4, 0, 1) == (140u << 16));
    TR_CHECK(tr_fxp95_color(3, 0, 1) == ((220u << 16) | 140u));
    TR_CHECK(tr_fxp95_color(-2, 0, 1) == 255u);
    TR_CHECK(tr_fxp95_color(4, 1, 1) == ((200u << 16) | (200u << 8) | 200u));
    TR_CHECK(tr_fxp95_color(0, 0, 0xff0000) == 0xff0000u);

    for (day = 0; day < 5; day++) {
        int date = 20260615 + day;
        feed(&s, date, 220000, 100, 101, 99, 100, 10, 100);
        for (i = 0; i <= 30; i++) {
            feed(&s, date, hm_add(223000, i), 100, 101, 99, 100, 10, 100);
        }
    }
    feed(&s, 20260620, 220000, 100, 101, 99, 100, 10, 100);
    for (i = 0; i < 30; i++) {
        feed(&s, 20260620, hm_add(223000, i), 100, 110, 90, 95, 40, 100);
    }
    feed(&s, 20260620, hm_add(223000, 30), 100, 120, 80, 90, 40, 100);
    TR_CHECK(s.group == 3);
    TR_CHECK(s.rgb == ((220u << 16) | 140u));
    TR_TEST_SUMMARY();
}
