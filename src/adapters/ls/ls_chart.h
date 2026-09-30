#ifndef TR_LS_CHART_H
#define TR_LS_CHART_H

/* LS 과거 분봉·일봉 차트 (계획서 §15, docs/ls_api_mapping.md §3).
 *
 * - t8412(주식 N분) / t8465(선물 N분)를 조회해 tr_candle_t 배열로 정규화한다.
 * - 페이지/연속 조회(cts_date/cts_time)를 지원하고, 중복·누락·정렬을 확인한다.
 * - 가격 스케일: 주식은 정수(Number, scale 1), 선물은 소수 문자열(0.01틱, scale 100).
 * - 차트 TR은 1 TPS — 연속 조회 시 최소 간격을 강제한다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adapters/ls/ls_auth.h"
#include "core/market/candle.h"

typedef enum {
    LS_CHART_STOCK_MIN = 0, /* t8412 */
    LS_CHART_FUT_MIN = 1,   /* t8465 */
    LS_CHART_STOCK_DAY = 2, /* t8410 */
    LS_CHART_FUT_DAY = 3    /* t8466 */
} ls_chart_kind_t;

typedef struct {
    size_t count;          /* out에 기록된 봉 수 */
    bool has_more;         /* 연속 조회 가능 여부 */
    char cts_date[9];      /* 다음 페이지 키 */
    char cts_time[11];
    int64_t session_open_min;  /* OutBlock s_time (분) */
    int64_t session_close_min; /* OutBlock e_time (분) */
} ls_chart_page_t;

/* 한 페이지 조회. out에는 open_time 오름차순으로 기록된다.
 * 반환: LS_HTTP_OK 외에 LS_CHART_EMPTY(데이터 없음, 오류 아님) 가능.
 * 재시도 가능한 실패(ls_chart_retryable)는 최대 2회 재시도한다 (1 TPS 스로틀 유지). */
#define LS_CHART_EMPTY 100

/* 재시도 가능한 실패인지 분류한다 (조회 함수들의 재시도 정책이 여기서 고정된다).
 * 전송 오류(타임아웃)·HTTP 상태 오류(5xx 등)는 서버가 답하지 못한 것이므로 재시도한다.
 * PARSE/API 오류는 서버가 답한 것이므로 재시도해도 같고, EMPTY·OK는 실패가 아니다.
 * (2026-09-30 실측: 차트 API가 간헐적으로 10초 타임아웃/500을 내 백필 페이지가 유실됐다) */
bool ls_chart_retryable(int rc);

int ls_chart_fetch_minute(ls_auth_t *auth, ls_chart_kind_t kind, const char *shcode,
                          int32_t ncnt, int32_t qrycnt, const char *edate, const char *etime,
                          const char *cts_date, const char *cts_time,
                          uint64_t instrument_id, uint64_t source_id,
                          tr_candle_t *out, size_t out_cap, ls_chart_page_t *page,
                          char *errbuf, size_t errlen);

/* 응답 JSON 본문 파싱 (테스트 가능하도록 분리). */
int ls_chart_parse_page(const char *body, size_t body_len, ls_chart_kind_t kind,
                        uint64_t instrument_id, uint64_t source_id, uint32_t timeframe_sec,
                        tr_candle_t *out, size_t out_cap, ls_chart_page_t *page,
                        char *errbuf, size_t errlen);

/* 가장 최근 KRX야간파생 세션의 기준일(개장일, 평일, days since epoch, KST 기준). */
int64_t ls_fut_night_session_day(tr_time_us_t now_us);

/* t8461(KRX야간파생 틱분별, 분봉) 응답 파서 (테스트 가능하도록 분리).
 * 행은 최신→과거 내림차순이고 날짜 필드가 없으므로(chetime HHMMSS만),
 * newest_session_day(최신 행이 속한 세션의 기준일)에서 뒤로 걸며
 * 저녁(18:00~)→아침(~05:00) 전이마다 기준일을 **직전 거래일**로 옮겨 날짜를 부여한다.
 * 거래일 목록(trading_days, 오름차순)은 t8465 주간 봉에서 추출한다 — 추석 같은
 * 연휴가 끼면 평일 추정이 어긋나므로 반드시 실제 거래일을 쓴다 (2026-09-28 실측 사건).
 * out에는 open_time 오름차순으로 기록된다. 반환값은 기록된 봉 수, 실패 시 -1. */
int ls_chart_parse_fut_night(const char *body, size_t body_len,
                             const int64_t *trading_days, size_t n_trading_days,
                             int64_t newest_session_day,
                             uint64_t instrument_id, uint64_t source_id,
                             tr_candle_t *out, size_t out_cap, char *errbuf, size_t errlen);

/* t8461 분봉 조회 (1 TPS 스로틀 적용). cnt는 1~999 (서버 상한, 연속 조회 없음).
 * t8465와 달리 야간 세션(18:00~익일 05:00) 봉을 준다.
 * trading_days는 날짜 부여에 쓰는 실제 거래일 목록(오름차순, t8465 주간 봉에서 추출). */
int ls_chart_fetch_fut_night(ls_auth_t *auth, const char *focode, int32_t cnt,
                             const int64_t *trading_days, size_t n_trading_days,
                             uint64_t instrument_id, uint64_t source_id,
                             tr_candle_t *out, size_t out_cap, size_t *out_count,
                             char *errbuf, size_t errlen);

/* ---------- 일봉 (t8410 주식 / t8466 선물) ----------
 * ⑤ 매매 상태 체인(dtl1/gap1) 워밍업 프라이밍용. 분봉 TR과 같은 엔드포인트·같은
 * InBlock 패턴이고 TR 코드와 주기 구분(gubun="2")만 다르다 (공식 tr-guides 명세).
 * t8466은 2026-09-29, t8410은 2026-09-30 실호출로 검증했다 (응답 행 오름차순,
 * 당일 진행 중 일봉 포함, comp_yn 필수 — 생략 시 압축여부 오류). */

typedef struct {
    int64_t day;   /* 거래일 (date 필드, days since epoch, KST 날짜) */
    int64_t high;  /* ×100 스케일 raw (분봉과 동일 규칙) */
    int64_t low;
} ls_daily_bar_t;

/* 일봉 응답 파서 (테스트 가능하도록 분리). 두 kind 모두 OutBlock1이 오름차순(과거→최신)임을
 * 실측으로 확인했다 (t8466 2026-09-29, t8410 2026-09-30). 그래도 방어적으로
 * 거래일 오름차순으로 정렬해 out에 기록한다. 중복 거래일은 LS_HTTP_PARSE_ERR.
 * 반환: LS_HTTP_OK / LS_CHART_EMPTY(데이터 없음, 오류 아님) / 오류 코드. */
int ls_chart_parse_daily(const char *body, size_t body_len, ls_chart_kind_t kind,
                         ls_daily_bar_t *out, size_t out_cap, size_t *out_count,
                         char *errbuf, size_t errlen);

/* 일봉 조회 (1 TPS 스로틀 적용). edate 기준 최신 qrycnt개 (비압축 최대 500).
 * edate는 "99999999"=당일 기준. 반환 코드는 parse_daily와 같다. */
int ls_chart_fetch_daily(ls_auth_t *auth, ls_chart_kind_t kind, const char *shcode,
                         int32_t qrycnt, const char *edate,
                         ls_daily_bar_t *out, size_t out_cap, size_t *out_count,
                         char *errbuf, size_t errlen);

#endif
