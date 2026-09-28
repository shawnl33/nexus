#ifndef TR_TEST_UTIL_H
#define TR_TEST_UTIL_H

/* 최소 테스트 유틸리티. 실패 시 파일·줄·표현식을 출력하고 마지막에 종료 코드로 보고한다. */

#include <stdio.h>

static int g_tr_failures = 0;

#define TR_CHECK(cond)                                                        \
    do {                                                                      \
        if (!(cond)) {                                                        \
            g_tr_failures++;                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

#define TR_TEST_SUMMARY()                                                     \
    do {                                                                      \
        if (g_tr_failures > 0) {                                              \
            fprintf(stderr, "%d failure(s)\n", g_tr_failures);                \
            return 1;                                                         \
        }                                                                     \
        return 0;                                                             \
    } while (0)

#endif
