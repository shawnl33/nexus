/* .env 백업 로더 테스트: 파싱 규칙과 환경변수 우선 규칙 (네트워크 불필요) */

#include "test_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adapters/ls/ls_auth.h"

#define FIXTURE "test_ls_auth_dotenv.tmp"

static void write_fixture(const char *content) {
    FILE *f = fopen(FIXTURE, "w");
    TR_CHECK(f != 0);
    fputs(content, f);
    fclose(f);
}

static void test_parse_variants(void) {
    write_fixture(
        "# 주석\n"
        "\n"
        "LS_APP_KEY = \"quoted-key-123\"\n"
        "export LS_SECRET_KEY=plain-secret-456\r\n"
        "UNRELATED_KEY=zzz\n"
        "NOEQUALS\n");
    int n = ls_auth_load_dotenv(FIXTURE);
    TR_CHECK(n == 2);
    /* 환경변수가 없다는 전제에서 .env 값으로 인식된다 */
    if (getenv("LS_APP_KEY") == 0 && getenv("LS_SECRET_KEY") == 0) {
        TR_CHECK(ls_auth_keys_present());
    }
    remove(FIXTURE);
}

static void test_missing_file(void) {
    TR_CHECK(ls_auth_load_dotenv("definitely-not-here.env") == 0);
}

int main(void) {
    test_parse_variants();
    test_missing_file();
    TR_TEST_SUMMARY();
}
