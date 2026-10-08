#ifndef NH_MASTER_H
#define NH_MASTER_H

/* NH 종목 마스터. 선물과 옵션을 종류로 나눈다.
 * kind: 1 국내선물, 2 CBOE 선물, 3 옵션, 4 분봉이 되는 CME 선물. 텍스트 검색(kind < 0)은 전부. */

#include <stddef.h>

typedef struct nh_master nh_master_t;

typedef struct {
    char shcode[40];
    char name[96];
    char cp; /* 'C', 'P', 또는 0 */
    double strike;
    int fut; /* 1 국내선물, 2 해외선물, 3 옵션 */
} nh_inst_hit_t;

void nh_master_free(nh_master_t *m);
size_t nh_master_count(const nh_master_t *m);

/* 세미콜론 텍스트. 해외(19·20열)와 국내(26열)를 한 버퍼에서 구분한다. */
nh_master_t *nh_master_parse(const char *text, size_t len);

/* cache_dir 아래 마스터 gzip. 없으면 포털에서 받는다. */
nh_master_t *nh_master_open_cached(const char *cache_dir, char *err, size_t err_cap);

/* kind < 0 이면 전부. expiry는 옵션 탭 키. 비면 무시. */
size_t nh_master_search(const nh_master_t *m, const char *q, int kind, const char *expiry,
                        nh_inst_hit_t *out, size_t cap);

/* 옵션 탭 칩. 상품과 만기를 붙여 다른 상품이 한 체인에 섞이지 않게 한다. */
size_t nh_master_expiries(const nh_master_t *m, char *out, size_t stride, size_t cap);

#endif
