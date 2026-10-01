#include "core/functions/fx_session_key_v1.h"

/* 원본 대응 (WSF_FXSessionKeyV1.txt:4-28):
 *   일 = 날짜입력 % 100;  월 = ((날짜입력-일)/100) % 100;  연도 = (…)/10000
 *   시각 >= 153000 → 구간 1 (날짜 유지)
 *   시각 <  70000  → 일을 하나 당기고(월·연도 롤백 포함, 윤년 처리) 구간 1
 *   반환 = 기준날짜*2 + 구간
 * 원본의 윤년 규칙(4·100·400년)과 4/6/9/11월 30일 규칙을 그대로 옮긴다. */
int64_t tr_fx_session_key_v1(int64_t yyyymmdd, int64_t hhmmss) {
    int64_t day = yyyymmdd % 100;
    int64_t month = ((yyyymmdd - day) / 100) % 100;
    int64_t year = (yyyymmdd - month * 100 - day) / 10000;
    int64_t base = yyyymmdd; /* 기준날짜 */
    int64_t segment = 0;
    if (hhmmss >= 153000) {
        segment = 1;
    }
    if (hhmmss < 70000) {
        day -= 1;
        if (day == 0) {
            month -= 1;
            if (month == 0) {
                month = 12;
                year -= 1;
            }
            day = 31;
            if (month == 4 || month == 6 || month == 9 || month == 11) {
                day = 30;
            }
            if (month == 2) {
                day = 28;
                if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) {
                    day = 29;
                }
            }
        }
        base = year * 10000 + month * 100 + day;
        segment = 1;
    }
    return base * 2 + segment;
}
