// WSF_FXSessionKeyV1 포팅 단위 테스트 — 원본 규칙의 경계를 손계산으로 고정한다.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "core/functions/fx_session_key_v1.h"

static int failures = 0;

static void check(int64_t date, int64_t time_s, int64_t want) {
    int64_t got = tr_fx_session_key_v1(date, time_s);
    if (got != want) {
        printf("FAIL key(%lld, %lld) = %lld, want %lld\n", (long long)date, (long long)time_s,
               (long long)got, (long long)want);
        failures++;
    }
}

int main(void) {
    // 낮 구간(07:00~15:29): 당일 날짜*2+0
    check(20261001, 70000, 20261001 * 2);
    check(20261001, 120000, 20261001 * 2);
    check(20261001, 152959, 20261001 * 2);
    // 저녁 구간(>=15:30): 당일 날짜*2+1 (날짜 유지)
    check(20261001, 153000, 20261001 * 2 + 1);
    check(20261001, 235959, 20261001 * 2 + 1);
    // 자정 넘김(<07:00): 전날*2+1
    check(20261001, 65959, 20260930 * 2 + 1);
    check(20261002, 0, 20261001 * 2 + 1);
    // 월 롤백: 3월→2월 (평년 28일 / 윤년 29일 / 100년·400년 규칙)
    check(20260301, 30000, 20260228 * 2 + 1); // 2026 평년
    check(20240301, 30000, 20240229 * 2 + 1); // 2024 윤년
    check(20000301, 30000, 20000229 * 2 + 1); // 2000: 400으로 나뉨 → 윤년
    check(21000301, 30000, 21000228 * 2 + 1); // 2100: 100으로 나뉘나 400은 아님 → 평년
    // 월 롤백: 31일 달(1·5·7·8·10·12월 진입)과 30일 달(4·6·9·11월 진입)
    check(20260501, 30000, 20260430 * 2 + 1); // 4월 → 30일
    check(20260601, 30000, 20260531 * 2 + 1); // 5월 → 31일
    check(20261101, 30000, 20261031 * 2 + 1); // 10월 → 31일
    check(20261201, 30000, 20261130 * 2 + 1); // 11월 → 30일
    // 연도 롤백: 1월 1일 → 전년 12월 31일
    check(20270101, 30000, 20261231 * 2 + 1);
    // 구간 경계: 07:00:00은 넘김이 아니다 (시작 포함), 15:30:00은 구간 1 포함
    check(20261001, 70001, 20261001 * 2);
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
