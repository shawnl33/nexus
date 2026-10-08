#include "adapters/nh/nh_master.h"

#include "adapters/nh/nh_chart.h"

#include <curl/curl.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define NH_MASTER_BASE "https://apidev.futures.co.kr/guide/master/"

typedef struct {
    char code[40];
    char name[80];
    char enam[80];
    char expiry[40];
    char cp;
    double strike;
    int kind; /* 1 국내선물, 2 해외선물, 3 옵션 */
} nh_row_t;

struct nh_master {
    nh_row_t *rows;
    size_t n;
    size_t cap;
};

static int code_kind_cmp(const void *a, const void *b);

void nh_master_free(nh_master_t *m) {
    if (m == 0) {
        return;
    }
    free(m->rows);
    free(m);
}

size_t nh_master_count(const nh_master_t *m) {
    return m != 0 ? m->n : 0;
}

static void copy_field(char *dst, size_t n, const char *src, size_t len) {
    if (n == 0) {
        return;
    }
    if (len >= n) {
        len = n - 1;
    }
    memcpy(dst, src, len);
    dst[len] = 0;
}

static int split_fields(const char *line, size_t len, const char **f, size_t *fl, int maxf) {
    int nf = 0;
    const char *p = line;
    const char *end = line + len;
    while (p <= end && nf < maxf) {
        const char *semi = memchr(p, ';', (size_t)(end - p));
        f[nf] = p;
        fl[nf] = semi != 0 ? (size_t)(semi - p) : (size_t)(end - p);
        nf++;
        if (semi == 0) {
            break;
        }
        p = semi + 1;
    }
    return nf;
}

static void strike_from_tail(const char *code, char *cp, double *strike) {
    *cp = 0;
    *strike = 0;
    const char *dash = strrchr(code, '-');
    if (dash != 0 && (dash[1] == 'C' || dash[1] == 'P')) {
        *cp = dash[1];
        *strike = atof(dash + 2);
    }
}

static bool push_row(nh_master_t *m, const nh_row_t *src) {
    if (src->code[0] == 0) {
        return false;
    }
    if (m->n == m->cap) {
        size_t cap = m->cap == 0 ? 256 : m->cap * 2;
        nh_row_t *rows = (nh_row_t *)realloc(m->rows, cap * sizeof(nh_row_t));
        if (rows == 0) {
            return false;
        }
        m->rows = rows;
        m->cap = cap;
    }
    m->rows[m->n++] = *src;
    return true;
}

static bool field_eq(const char *s, size_t n, const char *lit) {
    size_t ln = strlen(lit);
    return n == ln && memcmp(s, lit, ln) == 0;
}

static bool is_cboe_exch(const char *exch_name, size_t en, const char *exch, size_t xn) {
    return field_eq(exch_name, en, "OCBO") || field_eq(exch_name, en, "FCBO") ||
           field_eq(exch, xn, "15");
}

/* 해외 19열(OPRA) 또는 20열(CBOE 선물). 옵션은 OPRA, 선물은 거래소 15/FCBO 만. */
static void add_overseas(nh_master_t *m, const char **f, const size_t *fl, int nf) {
    if (nf < 7) {
        return;
    }
    char type = fl[6] > 0 ? f[6][0] : 0;
    nh_row_t row;
    memset(&row, 0, sizeof(row));
    if (type == 'O' && nf >= 19) {
        row.kind = 3;
        copy_field(row.code, sizeof(row.code), f[0], fl[0]);
        copy_field(row.enam, sizeof(row.enam), f[3], fl[3]);
        copy_field(row.name, sizeof(row.name), f[4], fl[4]);
        char comd[16];
        char last[16];
        char week[24];
        copy_field(comd, sizeof(comd), f[12], fl[12]);
        copy_field(week, sizeof(week), f[17], fl[17]);
        copy_field(last, sizeof(last), f[18], fl[18]);
        snprintf(row.expiry, sizeof(row.expiry), "%s %s", comd, last);
        if (week[0] != 0) {
            char base[80];
            snprintf(base, sizeof(base), "%s", row.name);
            snprintf(row.name, sizeof(row.name), "%s %s", base, week);
        }
        strike_from_tail(row.code, &row.cp, &row.strike);
        push_row(m, &row);
        return;
    }
    if ((type == 'F') && nf >= 16 && is_cboe_exch(f[2], fl[2], f[5], fl[5])) {
        row.kind = 2;
        copy_field(row.code, sizeof(row.code), f[0], fl[0]);
        copy_field(row.enam, sizeof(row.enam), f[3], fl[3]);
        copy_field(row.name, sizeof(row.name), f[4], fl[4]);
        push_row(m, &row);
        return;
    }
    if (type == 'F' && nf >= 7 && field_eq(f[2], fl[2], "FCME")) {
        char code[40];
        copy_field(code, sizeof(code), f[0], fl[0]);
        if (nh_exch_for_symbol(code) == 0 || strcmp(nh_exch_for_symbol(code), "FCME") != 0) {
            return;
        }
        row.kind = 4;
        snprintf(row.code, sizeof(row.code), "%s", code);
        copy_field(row.enam, sizeof(row.enam), f[3], fl[3]);
        copy_field(row.name, sizeof(row.name), f[4], fl[4]);
        push_row(m, &row);
    }
}

/* 국내 마스터. 1열 F는 선물, C는 스프레드, O는 옵션.
 * 주식 파생(STK)은 선물·스프레드·개별종목 옵션 모두 주식 탭(kind 0).
 * 옵션 탭은 지수 옵션과 해외 옵션만 둔다. */
static void add_domestic(nh_master_t *m, const char **f, const size_t *fl, int nf) {
    if (nf < 11) {
        return;
    }
    char type = fl[1] > 0 ? f[1][0] : 0;
    if (type != 'F' && type != 'O' && type != 'C') {
        return;
    }
    int stock = nf > 7 && fl[7] >= 3 && memcmp(f[7], "STK", 3) == 0;
    nh_row_t row;
    memset(&row, 0, sizeof(row));
    if (stock) {
        row.kind = 0;
    } else if (type == 'O') {
        row.kind = 3;
    } else {
        row.kind = 1;
    }
    copy_field(row.code, sizeof(row.code), f[8], fl[8]);
    copy_field(row.name, sizeof(row.name), f[10], fl[10]);
    if (type == 'O') {
        char date[16];
        copy_field(date, sizeof(date), f[4], fl[4]);
        char product[40];
        snprintf(product, sizeof(product), "%s", row.name);
        char *cut = strstr(product, " C ");
        if (cut == 0) {
            cut = strstr(product, " P ");
        }
        if (cut != 0) {
            *cut = 0;
        }
        snprintf(row.expiry, sizeof(row.expiry), "%s %s", product, date);
        if (strstr(row.name, " C ") != 0) {
            row.cp = 'C';
        } else if (strstr(row.name, " P ") != 0) {
            row.cp = 'P';
        }
        const char *num = strrchr(row.name, ' ');
        if (num != 0) {
            row.strike = atof(num + 1);
        }
    }
    push_row(m, &row);
}

static void add_line(nh_master_t *m, const char *line, size_t len) {
    const char *f[28];
    size_t fl[28];
    int nf = split_fields(line, len, f, fl, 28);
    if (nf >= 11 && fl[1] == 1 && (f[1][0] == 'F' || f[1][0] == 'O' || f[1][0] == 'C')) {
        add_domestic(m, f, fl, nf);
        return;
    }
    if (nf >= 19 && fl[6] > 0 && (f[6][0] == 'F' || f[6][0] == 'O' || f[6][0] == 'S')) {
        add_overseas(m, f, fl, nf);
    }
}

nh_master_t *nh_master_parse(const char *text, size_t len) {
    nh_master_t *m = (nh_master_t *)calloc(1, sizeof(*m));
    if (m == 0 || text == 0) {
        free(m);
        return 0;
    }
    const char *p = text;
    const char *end = text + len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *line_end = nl != 0 ? nl : end;
        size_t n = (size_t)(line_end - p);
        if (n > 0 && p[n - 1] == '\r') {
            n--;
        }
        if (n > 0) {
            add_line(m, p, n);
        }
        if (nl == 0) {
            break;
        }
        p = nl + 1;
    }
    if (m->n > 1) {
        qsort(m->rows, m->n, sizeof(m->rows[0]), code_kind_cmp);
        size_t w = 1;
        for (size_t i = 1; i < m->n; i++) {
            if (strcmp(m->rows[i].code, m->rows[w - 1].code) == 0 &&
                m->rows[i].kind == m->rows[w - 1].kind) {
                continue;
            }
            m->rows[w++] = m->rows[i];
        }
        m->n = w;
    }
    return m;
}

static int code_kind_cmp(const void *a, const void *b) {
    const nh_row_t *ra = (const nh_row_t *)a;
    const nh_row_t *rb = (const nh_row_t *)b;
    int d = strcmp(ra->code, rb->code);
    if (d != 0) {
        return d;
    }
    return ra->kind - rb->kind;
}

static int icmp_has(const char *hay, const char *needle) {
    if (needle == 0 || needle[0] == 0) {
        return 1;
    }
    if (hay == 0) {
        return 0;
    }
    size_t nlen = strlen(needle);
    for (const char *s = hay; *s != 0; s++) {
        size_t i = 0;
        while (i < nlen && s[i] != 0) {
            unsigned char a = (unsigned char)s[i];
            unsigned char b = (unsigned char)needle[i];
            if (a >= 'a' && a <= 'z') {
                a = (unsigned char)(a - 'a' + 'A');
            }
            if (b >= 'a' && b <= 'z') {
                b = (unsigned char)(b - 'a' + 'A');
            }
            if (a != b) {
                break;
            }
            i++;
        }
        if (i == nlen) {
            return 1;
        }
    }
    return 0;
}

static int row_cmp(const void *a, const void *b) {
    const nh_row_t *ra = *(const nh_row_t *const *)a;
    const nh_row_t *rb = *(const nh_row_t *const *)b;
    int d = strcmp(ra->expiry, rb->expiry);
    if (d != 0) {
        return d;
    }
    if (ra->strike < rb->strike) {
        return -1;
    }
    if (ra->strike > rb->strike) {
        return 1;
    }
    if (ra->cp != rb->cp) {
        return ra->cp < rb->cp ? -1 : 1;
    }
    return strcmp(ra->code, rb->code);
}

size_t nh_master_search(const nh_master_t *m, const char *q, int kind, const char *expiry,
                        nh_inst_hit_t *out, size_t cap) {
    if (m == 0 || out == 0 || cap == 0) {
        return 0;
    }
    const char *query = q != 0 ? q : "";
    const nh_row_t **hits = (const nh_row_t **)malloc(m->n * sizeof(hits[0]));
    if (hits == 0 && m->n > 0) {
        return 0;
    }
    size_t n = 0;
    for (size_t i = 0; i < m->n; i++) {
        const nh_row_t *row = &m->rows[i];
        if (kind >= 0 && row->kind != kind) {
            continue;
        }
        if (expiry != 0 && expiry[0] != 0 && strcmp(row->expiry, expiry) != 0) {
            continue;
        }
        int ok = query[0] == 0;
        if (!ok && (icmp_has(row->code, query) || icmp_has(row->name, query) ||
                    icmp_has(row->enam, query) || icmp_has(row->expiry, query))) {
            ok = 1;
        }
        if (ok) {
            hits[n++] = row;
        }
    }
    if (n > 1) {
        qsort(hits, n, sizeof(hits[0]), row_cmp);
    }
    size_t written = n < cap ? n : cap;
    for (size_t i = 0; i < written; i++) {
        nh_inst_hit_t *hit = &out[i];
        memset(hit, 0, sizeof(*hit));
        snprintf(hit->shcode, sizeof(hit->shcode), "%s", hits[i]->code);
        snprintf(hit->name, sizeof(hit->name), "%s", hits[i]->name);
        hit->cp = hits[i]->cp;
        hit->strike = hits[i]->strike;
        hit->fut = hits[i]->kind;
    }
    free(hits);
    return written;
}

size_t nh_master_expiries(const nh_master_t *m, char *out, size_t stride, size_t cap) {
    if (m == 0 || out == 0 || stride < 2 || cap == 0) {
        return 0;
    }
    size_t n = 0;
    for (size_t i = 0; i < m->n; i++) {
        if (m->rows[i].kind != 3 || m->rows[i].expiry[0] == 0) {
            continue;
        }
        int seen = 0;
        for (size_t j = 0; j < n; j++) {
            if (strcmp(out + j * stride, m->rows[i].expiry) == 0) {
                seen = 1;
                break;
            }
        }
        if (seen) {
            continue;
        }
        if (n == cap) {
            break;
        }
        snprintf(out + n * stride, stride, "%s", m->rows[i].expiry);
        n++;
    }
    for (size_t i = 1; i < n; i++) {
        char tmp[80];
        snprintf(tmp, sizeof(tmp), "%s", out + i * stride);
        size_t j = i;
        while (j > 0) {
            const char *prev = out + (j - 1) * stride;
            const char *da = strrchr(prev, ' ');
            const char *db = strrchr(tmp, ' ');
            da = da != 0 ? da + 1 : prev;
            db = db != 0 ? db + 1 : tmp;
            int ord = strcmp(da, db);
            if (ord == 0) {
                ord = strcmp(prev, tmp);
            }
            if (ord <= 0) {
                break;
            }
            snprintf(out + j * stride, stride, "%s", out + (j - 1) * stride);
            j--;
        }
        snprintf(out + j * stride, stride, "%s", tmp);
    }
    return n;
}

static int download_file(const char *url, const char *path) {
    CURL *curl = curl_easy_init();
    if (curl == 0) {
        return -1;
    }
    FILE *f = fopen(path, "wb");
    if (f == 0) {
        curl_easy_cleanup(curl);
        return -1;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    CURLcode cc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    fclose(f);
    if (cc != CURLE_OK || status != 200) {
        remove(path);
        return -1;
    }
    return 0;
}

static int gunzip_file(const char *path, char **out, size_t *out_len) {
    gzFile gz = gzopen(path, "rb");
    if (gz == 0) {
        return -1;
    }
    size_t cap = 1 << 20;
    size_t n = 0;
    char *buf = (char *)malloc(cap);
    if (buf == 0) {
        gzclose(gz);
        return -1;
    }
    for (;;) {
        if (n + 65536 > cap) {
            char *grown = (char *)realloc(buf, cap * 2);
            if (grown == 0) {
                free(buf);
                gzclose(gz);
                return -1;
            }
            buf = grown;
            cap *= 2;
        }
        int got = gzread(gz, buf + n, 65536);
        if (got < 0) {
            free(buf);
            gzclose(gz);
            return -1;
        }
        if (got == 0) {
            break;
        }
        n += (size_t)got;
    }
    gzclose(gz);
    *out = buf;
    *out_len = n;
    return 0;
}

static const char *MASTER_FILES[] = {
    "opra_opt.mst.gz", "etc_fut.mst.gz", "cme_fut.mst.gz", "fut_opt.mst.gz", "stk_fut_opt.mst.gz",
};

nh_master_t *nh_master_open_cached(const char *cache_dir, char *err, size_t err_cap) {
    if (cache_dir == 0) {
        return 0;
    }
    nh_master_t *all = (nh_master_t *)calloc(1, sizeof(*all));
    if (all == 0) {
        return 0;
    }
    int loaded = 0;
    for (size_t i = 0; i < sizeof(MASTER_FILES) / sizeof(MASTER_FILES[0]); i++) {
        char path[512];
        char url[256];
        snprintf(path, sizeof(path), "%s/%s", cache_dir, MASTER_FILES[i]);
        snprintf(url, sizeof(url), "%s%s", NH_MASTER_BASE, MASTER_FILES[i]);
        FILE *existing = fopen(path, "rb");
        if (existing == 0) {
            if (download_file(url, path) != 0) {
                if (err != 0 && err_cap > 0) {
                    snprintf(err, err_cap, "master download failed: %s", MASTER_FILES[i]);
                }
                continue;
            }
        } else {
            fclose(existing);
        }
        char *text = 0;
        size_t len = 0;
        if (gunzip_file(path, &text, &len) != 0) {
            continue;
        }
        nh_master_t *part = nh_master_parse(text, len);
        free(text);
        if (part == 0) {
            continue;
        }
        for (size_t r = 0; r < part->n; r++) {
            push_row(all, &part->rows[r]);
        }
        nh_master_free(part);
        loaded++;
    }
    if (loaded == 0) {
        nh_master_free(all);
        return 0;
    }
    return all;
}
