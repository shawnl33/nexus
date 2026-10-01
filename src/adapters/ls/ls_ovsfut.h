#ifndef TR_LS_OVSFUT_H
#define TR_LS_OVSFUT_H

/* 해외선물 종목 해결 (계획서 §15, docs/ls_api_mapping.md §3/§4).
 *
 * - 이 계정의 REST는 CME 종목을 돌려주지 않는다 (2026-10-01 실측: o3101/o3103에 ESZ26을
 *   물으면 rsp_cd 00000 + "해당자료가 없습니다." — HKEX·LME 종목만 온다).
 *   그래서 CME 계열은 내장 정적 표로 해결하고, o3101(해외선물마스터조회)은 있으면 로딩해
 *   정적 표에 없는 종목(HKEX/LME)의 이름·틱을 보강한다.
 * - 정적 표 행 하나가 종목 하나다: 심볼 접두(ES, NQ, ...) → {현재 월물, 이름, 틱 크기(raw)}.
 *   판별은 접두 매치라 월물이 바뀌어도(ESH27 등) 동작한다. 월물 롤링 시 contract 열 한 자만 고친다.
 * - 세션은 CME 기준 한국시각 07:00 개장 → 익일 06:00 폐장(평일 개장) 하나로 통일한다.
 *   미국 서머타임 전환기에는 1시간 어긋난다 — v1 한계로 기록 (틱 유실은 아니고
 *   세션 경계 봉 처리에만 영향).
 * - raw 스케일은 기존 선물과 같은 ×100 (ES 7734.75 → 773475, 틱 0.25 → 25).
 *   소수 3자리 이상 상품(예: SI 0.005틱, CUS 0.0001틱)은 ×100 반올림이 가격을 절단하므로
 *   v1에서 배제한다 — 정적 표에 넣지 않고, o3101 등록 경로에서도 `DotGb > 2`인 종목은
 *   걸러낸다 (ls_master_fetch, 기동 시 "정밀도 배제 N종" 1줄 요약).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/market/session.h"

typedef struct {
    char prefix[8];    /* 심볼 접두 (ES, NQ, ...) */
    char contract[16]; /* 현재 월물 코드 (검색 결과 표시용) */
    char name[48];     /* 종목명 */
    double tick_raw;   /* 1틱의 raw 크기 (실제 가격 × 100) */
} ls_ovsfut_entry_t;

/* o3101(해외선물마스터조회) 한 행 — 이름·거래소·틱·소수 자리만 쓴다 */
typedef struct {
    char symbol[12];   /* Symbol (예: CUSV26) */
    char name[52];     /* SymbolNm */
    char exch_cd[8];   /* ExchCd (HKEX, LME, ...) */
    double tick_raw;   /* UntPrc × 100 (최소가격변동의 raw 크기) */
    int dot_gb;        /* DotGb (가격 소수 자리, Number). 필드가 없으면 -1 */
} ls_ovsfut_master_row_t;

/* 정적 표 조회: shcode의 접두(또는 월물 코드 정확 일치)가 표에 있으면 그 행을 돌려준다.
 * 접두가 여럿 겹치면 가장 긴 것을 고른다. 없으면 NULL. */
const ls_ovsfut_entry_t *ls_ovsfut_find(const char *shcode);

/* 해외선물 공통 세션 (CME 기준 KST 07:00 → 익일 06:00, 평일 개장 — 헤더 주석 참조) */
tr_session_policy_t ls_ovsfut_session(void);

/* 정적 표 전체 순회용 (레지스트리 등록·테스트) */
size_t ls_ovsfut_count(void);
const ls_ovsfut_entry_t *ls_ovsfut_at(size_t index);

/* 정적 표 검색: 접두/월물 코드 접두사 또는 이름의 단어 경계 부분 문자열(대소문자 무시,
 * ASCII 기준). 이름 매치는 단어 시작(문자열 처음 또는 비영숫자 직후)에서만 인정한다 —
 * "es"가 "Jones"에 걸리는 오탐 방지. q가 NULL이거나 빈 문자열이면 전체 일치.
 * 참고: 프로덕션 종목 검색은 정적 표가 등록된 레지스트리(ls_master_search)가 담당하고,
 * 이 함수는 표 자체의 단위 테스트·보조 용도다. */
size_t ls_ovsfut_search(const char *q, const ls_ovsfut_entry_t **out, size_t cap);

/* o3101 응답 파서 (테스트 가능하도록 분리). 반환값은 기록된 행 수, 실패 시 -1.
 * Symbol이 빈 행은 건너뛴다. */
int ls_ovsfut_parse_master(const char *body, size_t len,
                           ls_ovsfut_master_row_t *out, size_t cap);

/* ×100 raw 스케일로 가격을 정확히 표현할 수 있는 상품인지 (DotGb 기준).
 * 소수 3자리 이상(DotGb > 2)이면 가격 ×100 반올림이 값을 절단하므로 레지스트리 등록에서
 * 배제한다 (예: CUS 0.0001틱 "6.7124" → 671, 24틱 오차). DotGb를 알 수 없으면(-1)
 * 정밀도를 증명할 수 없어 보수적으로 배제한다. */
bool ls_ovsfut_fits_raw100(int dot_gb);

#endif
