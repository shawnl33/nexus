#ifndef TR_LS_MASTER_H
#define TR_LS_MASTER_H

/* LS 종목 레지스트리 (계획서 §15, docs/ls_api_mapping.md §3).
 *
 * - t8436(주식 종목조회, 코스피/코스닥)과 t8467(코스피200선물 마스터)로 종목 메타데이터를 구성한다.
 * - 해외선물은 o3101(해외선물마스터조회 — 이 계정은 HKEX/LME만 온다)을 있으면 로딩하고,
 *   CME 계열은 내장 정적 표(ls_ovsfut)를 레지스트리에 등록한다 (2026-10-01 실측 참조).
 * - 종목 유형(주식/선물/해외선물)을 코드 길이 추정이 아니라 마스터 데이터로 판별한다.
 * - 지원 데이터가 없는 종목은 호가 등 구성요소 미지원으로 표시한다 (0으로 채우지 않음).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adapters/ls/ls_auth.h"

typedef struct {
    char shcode[12];
    char name[64];
    char expcode[20];
    int market;        /* 1=코스피, 2=코스닥, 3=코스피200선물, 4=해외선물 */
    bool is_futures;
    double tick_raw;   /* 1틱의 raw 크기 (실제 × 100). 0이면 자동(선물 5, 주식 100) — 해외선물만 채운다 */
} ls_instrument_info_t;

#define LS_MARKET_KOSPI 1
#define LS_MARKET_KOSDAQ 2
#define LS_MARKET_KP200_FUT 3
#define LS_MARKET_OVS_FUT 4

/* 레지스트리 본체. 직접 필드 접근보다 아래 접근자(ls_master_find/at/count) 사용을 권장한다. */
typedef struct tr_ls_master {
    ls_instrument_info_t *items;
    size_t count;
} tr_ls_master_t;

/* 두 마스터를 조회해 레지스트리를 구성한다. 실패 시 NULL + errbuf. */
tr_ls_master_t *ls_master_fetch(ls_auth_t *auth, char *errbuf, size_t errlen);
void ls_master_free(tr_ls_master_t *m);

const ls_instrument_info_t *ls_master_find(tr_ls_master_t *m, const char *shcode);
size_t ls_master_count(tr_ls_master_t *m);
const ls_instrument_info_t *ls_master_at(tr_ls_master_t *m, size_t index);

/* 종목 검색: shcode 접두사 또는 종목명 부분 문자열(대소문자 무시, ASCII 기준)에
 * 일치하는 종목을 out에 최대 cap개 기록하고 기록된 수를 반환한다.
 * q가 NULL이거나 빈 문자열이면 전체가 일치한 것으로 본다. */
size_t ls_master_search(const tr_ls_master_t *m, const char *q,
                        const ls_instrument_info_t **out, size_t cap);

/* 파서 (테스트 가능하도록 분리). 반환값은 기록된 종목 수, 실패 시 -1. */
int ls_master_parse_stock(const char *body, size_t len, ls_instrument_info_t *out, size_t cap);
int ls_master_parse_fut(const char *body, size_t len, ls_instrument_info_t *out, size_t cap);

#endif
