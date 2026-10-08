#include "test_util.h"

#include <string.h>

#include "adapters/nh/nh_master.h"

static const char TEXT[] =
    "O_SPW2RV26-C7780.0;O_SPW2RV26-C7780.0;OCBO;CBOE S&P 500 Weekly Option C7780.0;CBOE S&P 500 위클리 옵션 C7780.0;15;O;30;3;0;0.05;1;O_SPW;202610;1;1;;2주 목요일;20261008\n"
    "O_NDXV26-C100.0;O_NDXV26-C100.0;OCBO;Nasdaq-100;나스닥100;15;O;30;3;0;0.05;1;O_NDX;202610;1;1;;11월 1주 목요일;20261016\n"
    "VXF27;VXF27;FCBO;Volatility Index (202701);S&P변동성 지수 (202701);15;F;30;5;0;0.05;1;0;0;VX;202701;;19;50;\n"
    "6AZ26;6AZ26;FCME;Australian Dollar;호주달러;01;F;10;6;0;0.00005;1;1;1;6A;202612;;0.7;5;\n"
    "ESZ26;ESZ26;FCME;E-mini S&P 500;미니 S&P 500;01;F;30;2;0;0.25;1;1;1;ES;202612;;7800;25;\n"
    "YMZ26;YMZ26;FCME;Mini Dow;미니 다우;01;F;30;0;0;1;1;1;1;YM;202612;;48000;100;\n"
    "4;F;1;KOSPI;20261210;day;;KSPFBASE;A016C000;A016C000;코스피200 F 202612;KOSPI 200 F 202612;1;1;0;1;1;1;1;Y;1;1;A016C000;KOSPI612;03;01\n"
    "4;O;3;KOSPI;20261008;day;;KSPOBASE;B016A745;B016A745;코스피200 C 202610   745.0;KOSPI 200 C 202610   745.0;1;1;745;1;1;1;1;Y;1;1;B016A745;KOSPI610;03;01\n"
    "6;F;1;S11;20261008;day;;STKFBASE;A116A000;A116A000;삼성전자   F 202610 (  10);SEC F 202610;1;1;0;1;1;1;1;Y;1;1;A116A000;SSELEC610;01;11\n"
    "5;O;3;S11;20261008;day;;STKOBASE;B116A100;B116A100;삼성전자 C 202610   70000;SEC C 202610 70000;1;1;70000;1;1;1;1;Y;1;1;B116A100;SEC;01;11\n";

static void test_tabs(void) {
    nh_master_t *m = nh_master_parse(TEXT, strlen(TEXT));
    TR_CHECK(nh_master_count(m) == 9);
    nh_inst_hit_t hits[8];
    size_t n = nh_master_search(m, "", 2, 0, hits, 8);
    TR_CHECK(n == 1);
    TR_CHECK(strcmp(hits[0].shcode, "VXF27") == 0);
    TR_CHECK(hits[0].fut == 2);
    n = nh_master_search(m, "", 4, 0, hits, 8);
    TR_CHECK(n == 2);
    TR_CHECK(strcmp(hits[0].shcode, "6AZ26") == 0);
    TR_CHECK(strcmp(hits[1].shcode, "ESZ26") == 0);
    TR_CHECK(hits[0].fut == 4);
    n = nh_master_search(m, "YM", 4, 0, hits, 8);
    TR_CHECK(n == 0);
    n = nh_master_search(m, "", 1, 0, hits, 8);
    TR_CHECK(n == 1);
    TR_CHECK(strcmp(hits[0].shcode, "A016C000") == 0);
    TR_CHECK(hits[0].fut == 1);
    n = nh_master_search(m, "", 3, "O_NDX 20261016", hits, 8);
    TR_CHECK(n == 1);
    TR_CHECK(strcmp(hits[0].shcode, "O_NDXV26-C100.0") == 0);
    TR_CHECK(hits[0].fut == 3);
    n = nh_master_search(m, "", 3, "코스피200 20261008", hits, 8);
    TR_CHECK(n == 1);
    TR_CHECK(hits[0].cp == 'C');
    TR_CHECK(hits[0].strike == 745.0);
    n = nh_master_search(m, "삼성", 0, 0, hits, 8);
    TR_CHECK(n == 2);
    TR_CHECK(hits[0].fut == 0);
    TR_CHECK(hits[1].fut == 0);
    n = nh_master_search(m, "삼성", 3, 0, hits, 8);
    TR_CHECK(n == 0);
    n = nh_master_search(m, "삼성", 1, 0, hits, 8);
    TR_CHECK(n == 0);
    char keys[8][40];
    size_t ne = nh_master_expiries(m, &keys[0][0], 40, 8);
    TR_CHECK(ne == 3);
    int saw_fut = 0;
    for (size_t i = 0; i < ne; i++) {
        if (strcmp(keys[i], "VXF27") == 0) {
            saw_fut = 1;
        }
    }
    TR_CHECK(!saw_fut);
    nh_master_free(m);
}

int main(void) {
    test_tabs();
    TR_TEST_SUMMARY();
}
