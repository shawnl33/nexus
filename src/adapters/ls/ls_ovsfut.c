#include "adapters/ls/ls_ovsfut.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yyjson.h"

/* 해외선물 정적 표 — 종목 추가·월물 롤링은 한 줄만 고친다 (헤더 주석 참조).
 * tick_raw는 1틱 × 100 (ES 0.25 → 25, YM 1.0 → 100, CL 0.01 → 1). */
static const ls_ovsfut_entry_t OVSFUT_TABLE[] = {
    {"ES",  "ESZ26",  "E-mini S&P 500",       25.0},
    {"NQ",  "NQZ26",  "E-mini Nasdaq 100",    25.0},
    {"YM",  "YMZ26",  "Mini Dow Jones",      100.0},
    {"RTY", "RTYZ26", "E-mini Russell 2000",  10.0},
    {"CL",  "CLX26",  "WTI Crude Oil",         1.0},
    {"GC",  "GCZ26",  "Gold",                 10.0},
};

size_t ls_ovsfut_count(void) {
    return sizeof(OVSFUT_TABLE) / sizeof(OVSFUT_TABLE[0]);
}

const ls_ovsfut_entry_t *ls_ovsfut_at(size_t index) {
    return index < ls_ovsfut_count() ? &OVSFUT_TABLE[index] : 0;
}

const ls_ovsfut_entry_t *ls_ovsfut_find(const char *shcode) {
    if (shcode == 0 || shcode[0] == '\0') {
        return 0;
    }
    const ls_ovsfut_entry_t *best = 0;
    size_t best_len = 0;
    for (size_t i = 0; i < ls_ovsfut_count(); i++) {
        const ls_ovsfut_entry_t *e = &OVSFUT_TABLE[i];
        size_t plen = strlen(e->prefix);
        /* 월물 코드 정확 일치 또는 접두 매치 (접두 뒤는 월물 코드: 월 코드 1자 + 연도) */
        if (strcmp(shcode, e->contract) == 0) {
            return e;
        }
        if (strncmp(shcode, e->prefix, plen) == 0 && plen > best_len && shcode[plen] != '\0') {
            best = e;
            best_len = plen;
        }
    }
    return best;
}

tr_session_policy_t ls_ovsfut_session(void) {
    /* CME: 한국시각 07:00 개장 → 익일 06:00 폐장 (close < open 이면 익일 폐장 야간 넘김).
     * 개장 요일은 평일 — 월 07:00에 열린 세션이 토 06:00에 닫힌다 (일요일 KST 개장 없음).
     * DST 전환기 1시간 오차는 v1 한계 (헤더 주석 참조) */
    tr_session_policy_t s = {540, 7 * 60, 6 * 60, TR_SESSION_WEEKDAYS};
    return s;
}

/* ASCII 대소문자 무시 접두 비교. 코드는 대문자로 저장된다. */
static bool prefix_ci(const char *s, const char *q, size_t qlen) {
    for (size_t i = 0; i < qlen; i++) {
        if (s[i] == 0 ||
            tolower((unsigned char)s[i]) != tolower((unsigned char)q[i])) {
            return false;
        }
    }
    return true;
}

/* 이름의 단어 경계 부분 문자열 검색 (대소문자 무시, ASCII 기준): 매치 시작이 문자열
 * 처음이거나 비영숫자 직후여야 한다 ("es"가 "Jones"의 끝 두 글자에 걸리는 오탐 방지) */
static bool name_contains_ci(const char *hay, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) {
        return true;
    }
    for (const char *h = hay; *h != 0; h++) {
        size_t i = 0;
        while (i < nlen && h[i] != 0 &&
               tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen && (h == hay || !isalnum((unsigned char)h[-1]))) {
            return true;
        }
        if (h[i] == 0) {
            break;
        }
    }
    return false;
}

size_t ls_ovsfut_search(const char *q, const ls_ovsfut_entry_t **out, size_t cap) {
    if (out == 0 || cap == 0) {
        return 0;
    }
    size_t n = 0;
    size_t qlen = q != 0 ? strlen(q) : 0;
    for (size_t i = 0; i < ls_ovsfut_count() && n < cap; i++) {
        const ls_ovsfut_entry_t *e = &OVSFUT_TABLE[i];
        bool match = qlen == 0 || prefix_ci(e->prefix, q, qlen) ||
                     prefix_ci(e->contract, q, qlen) || name_contains_ci(e->name, q);
        if (match) {
            out[n++] = e;
        }
    }
    return n;
}

int ls_ovsfut_parse_master(const char *body, size_t len,
                           ls_ovsfut_master_row_t *out, size_t cap) {
    if (body == 0 || out == 0) {
        return -1;
    }
    yyjson_doc *doc = yyjson_read((char *)body, len, 0);
    if (doc == 0) {
        return -1;
    }
    yyjson_val *arr = yyjson_obj_get(yyjson_doc_get_root(doc), "o3101OutBlock");
    if (!yyjson_is_arr(arr)) {
        yyjson_doc_free(doc);
        return 0; /* 성공이지만 데이터 없음 (이 계정의 CME처럼) — 오류 아님 */
    }
    int n = 0;
    size_t idx, max;
    yyjson_val *row;
    yyjson_arr_foreach(arr, idx, max, row) {
        if ((size_t)n >= cap) {
            break;
        }
        const char *symbol = yyjson_get_str(yyjson_obj_get(row, "Symbol"));
        const char *name = yyjson_get_str(yyjson_obj_get(row, "SymbolNm"));
        const char *exch = yyjson_get_str(yyjson_obj_get(row, "ExchCd"));
        const char *untprc = yyjson_get_str(yyjson_obj_get(row, "UntPrc"));
        if (symbol == 0 || symbol[0] == '\0') {
            continue;
        }
        ls_ovsfut_master_row_t *r = &out[n];
        memset(r, 0, sizeof(*r));
        snprintf(r->symbol, sizeof(r->symbol), "%.11s", symbol);
        snprintf(r->name, sizeof(r->name), "%.51s", name != 0 ? name : "");
        snprintf(r->exch_cd, sizeof(r->exch_cd), "%.7s", exch != 0 ? exch : "");
        /* UntPrc는 실제 가격 단위의 최소가격변동 (예: "0.000100000") → raw ×100 */
        r->tick_raw = untprc != 0 ? atof(untprc) * 100.0 : 0.0;
        /* DotGb는 가격 소수 자리 (Number, 2026-10-01 실측: CUS=4, HSI=0) — ×100 raw
         * 정밀도 판별(ls_ovsfut_fits_raw100)에 쓴다. 없으면 -1 (보수적 배제 대상) */
        yyjson_val *dg = yyjson_obj_get(row, "DotGb");
        r->dot_gb = yyjson_is_num(dg) ? (int)yyjson_get_num(dg)
                  : yyjson_is_str(dg) ? atoi(yyjson_get_str(dg))
                                      : -1;
        n++;
    }
    yyjson_doc_free(doc);
    return n;
}

bool ls_ovsfut_fits_raw100(int dot_gb) {
    /* 소수 ≤2자리면 가격 ×100이 항상 정수라 raw 절단이 없다.
     * 3자리 이상 또는 알 수 없음(-1)은 등록 배제 (헤더 주석의 배제 원칙) */
    return dot_gb >= 0 && dot_gb <= 2;
}
