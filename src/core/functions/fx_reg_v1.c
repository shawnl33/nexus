#include "core/functions/fx_reg_v1.h"

#include <math.h>
#include <string.h>

#define TR_FXREG_MIN_SAMPLES 5 /* 원본 최소회귀봉수 (21줄) */

/* 차트 주기에 따른 자동 회귀기간 (원본 30~49줄). 국내 WSF_Mtf_LinRegV3의
 * select_n(linreg_v3.c)과 동일한 표다 — 두 원본이 같은 표를 공유한다. */
static uint32_t select_n(tr_compress_t compress, uint32_t interval) {
    switch (compress) {
    case TR_COMPRESS_TICK:
    case TR_COMPRESS_SEC:
        return 30;
    case TR_COMPRESS_MIN:
        if (interval <= 1) {
            return 30;
        }
        if (interval <= 5) {
            return 18;
        }
        if (interval <= 15) {
            return 10;
        }
        if (interval <= 30) {
            return 6;
        }
        return 12;
    case TR_COMPRESS_DAY:
        return 20;
    case TR_COMPRESS_WEEK:
        return 13;
    case TR_COMPRESS_MONTH:
        return 12;
    }
    return 20;
}

bool tr_fxreg_init(tr_fxreg_t *s, tr_compress_t compress, uint32_t bar_interval) {
    if (s == 0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->compress = compress;
    s->bar_interval = bar_interval;
    s->n = select_n(compress, bar_interval);
    /* memset 다음에 시계열 저장소를 연결한다 (순서 고정). 유효 용량은 회귀기간 n개 */
    ylv_init(&s->prices, s->prices_buf, s->n);
    return true;
}

void tr_fxreg_eval(tr_fxreg_t *s, const tr_fxreg_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    /* 새 봉 판정 (원본 56~66줄: sDate/sTime 변화 — 포팅은 bar open_time으로) */
    bool is_new_bar = !s->has_bar || in->bar_open != s->last_bar_open;

    if (is_new_bar) {
        /* 세션초기화==1인 새 봉에서 전일 자료 제거 (원본 71~80줄) */
        if (in->session_reset) {
            ylv_clear(&s->prices);
        }
        ylv_push(&s->prices, in->price); /* 원본 82~88줄: 시프트 + 유효개수 상한 n (링 용량이 보장) */
        s->last_bar_open = in->bar_open;
        s->has_bar = true;
    } else {
        /* 진행 중인 현재 봉은 배열 이동 없이 최신 가격만 갱신 (원본 91~92줄) */
        ylv_set_current(&s->prices, in->price);
    }

    /* 장 첫 봉에는 안전한 기본값 반환 (원본 94~100줄) */
    s->line = in->price;
    s->slope = 0.0;
    s->line_sign = 0;
    s->reg_valid = false;
    s->r2 = 0.0;
    s->residual = 0.0;

    /* 장 시작 후 표본을 늘리되 최소 5봉부터 방향을 인정 (원본 102~105줄).
     * 계산개수 = Min(유효개수, n) — 링 용량이 n이라 ylv_count와 동일 */
    size_t calc = ylv_count(&s->prices);
    if (calc >= TR_FXREG_MIN_SAMPLES) {
        /* 원본 107~153줄: OLS + R² + 잔차. 절편 B는 원본 산식 순서(129줄)를 그대로 둔다 */
        double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0, sumY2 = 0.0;
        for (size_t i = 0; i < calc; i++) {
            double p = 0.0;
            ylv_at(&s->prices, calc - 1 - i, &p); /* 유효 구간의 끝이 가장 오래된 값 (원본 113줄) */
            sumX += (double)i;
            sumY += p;
            sumXY += (double)i * p;
            sumX2 += (double)i * (double)i;
            sumY2 += p * p;
        }

        double dn = (double)calc;
        double denom = dn * sumX2 - sumX * sumX;
        if (denom != 0.0) { /* 연속 정수 x라 calc>=2에서 도달 불가 — 원본 가드(126줄) 유지 */
            double lrs = (dn * sumXY - sumX * sumY) / denom;
            double b = (sumY * sumX2 - sumX * sumXY) / denom;

            /* 최신 봉은 x=계산개수-1 (원본 131~132줄) */
            s->line = lrs * (double)(calc - 1) + b;
            s->slope = lrs;
            s->line_sign = (lrs > 0.0) - (lrs < 0.0); /* 원본 136~137줄 IFF 중첩 대응 */
            s->reg_valid = true;

            /* 결정계수 R² (원본 140~146줄) */
            double r2_num = (dn * sumXY - sumX * sumY) * (dn * sumXY - sumX * sumY);
            double r2_den = (dn * sumX2 - sumX * sumX) * (dn * sumY2 - sumY * sumY);
            if (r2_den > 0.0) {
                s->r2 = fmin(1.0, fmax(0.0, r2_num / r2_den));
            }

            /* 회귀선 주변의 잔차 표준편차 (원본 148~153줄) */
            double sst = sumY2 - (sumY * sumY / dn);
            double sse = fmax(0.0, sst * (1.0 - s->r2));
            if (calc > 2) {
                s->residual = sqrt(sse / (double)(calc - 2));
            }
        }
    }
}

bool tr_fxreg_relink(tr_fxreg_t *s) {
    if (s == 0) {
        return false;
    }
    return ylv_relink(&s->prices, s->prices_buf);
}
