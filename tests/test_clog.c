#include "test_util.h"

#include <string.h>

#include "runtime/clog.h"

static void test_line(void) {
    char buf[128];
    int n = tr_clog_format(buf, sizeof(buf), 9, 5, 3, "시세", "끊김 1002 Protocol error");
    TR_CHECK(n == (int)strlen(buf));
    TR_CHECK(strcmp(buf, "09:05:03  시세  끊김 1002 Protocol error\n") == 0);
}

static void test_pads_midnight(void) {
    char buf[64];
    tr_clog_format(buf, sizeof(buf), 0, 0, 0, "엔진", "준비됨");
    TR_CHECK(strcmp(buf, "00:00:00  엔진  준비됨\n") == 0);
}

int main(void) {
    test_line();
    test_pads_midnight();
    TR_TEST_SUMMARY();
}
