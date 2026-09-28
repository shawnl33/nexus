#ifndef TR_LS_MASTER_H
#define TR_LS_MASTER_H

/* LS 종목 레지스트리 (계획서 §15, docs/ls_api_mapping.md §3).
 *
 * - t8436(주식 종목조회, 코스피/코스닥)과 t8467(코스피200선물 마스터)로 종목 메타데이터를 구성한다.
 * - 종목 유형(주식/선물)을 코드 길이 추정이 아니라 마스터 데이터로 판별한다.
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
    int market;        /* 1=코스피, 2=코스닥, 3=코스피200선물 */
    bool is_futures;
} ls_instrument_info_t;

#define LS_MARKET_KOSPI 1
#define LS_MARKET_KOSDAQ 2
#define LS_MARKET_KP200_FUT 3

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

/* 파서 (테스트 가능하도록 분리). 반환값은 기록된 종목 수, 실패 시 -1. */
int ls_master_parse_stock(const char *body, size_t len, ls_instrument_info_t *out, size_t cap);
int ls_master_parse_fut(const char *body, size_t len, ls_instrument_info_t *out, size_t cap);

#endif
