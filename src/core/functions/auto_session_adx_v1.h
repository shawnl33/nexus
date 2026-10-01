#ifndef TR_AUTO_SESSION_ADX_V1_H
#define TR_AUTO_SESSION_ADX_V1_H

/* WSF_AutoSessionADXV1 포팅 (원본 138줄, 완전 제공).
 *
 * Wilder 방식 ADX/+DI/−DI. 1분봉이면 일자 변경마다 리셋, 그 외에는 연속 계산.
 * - 기간값 = Round(Abs(ADX기간입력)), 하한 2.
 * - 세션 첫 봉: 평활TR = H−L 시드, DM은 0.
 * - 계산봉수 <= 기간값: 단순 누적. 이후: X − X/N + 새값 Wilder 평활.
 * - DX = |+DI − −DI| / (+DI + −DI) × 100 (합>0일 때).
 * - ADX: 계산봉수 == 기간값×2 에서 DX 평균으로 시드, 이후 (ADX×(N−1)+DX)/N 재귀.
 * - 유효: 계산봉수 >= 기간값×2. 무효 시 출력 4개 모두 0.
 *
 * 호출 체인은 원본에서 죽은 코드(출력 미사용)로 확인됨 — 완결성을 위해 포팅하되
 * 매매 의사결정 경로에서는 사용하지 않는다.
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t period;     /* 기간값 (하한 2) */
    /* 상태 */
    uint32_t bar_count;  /* 계산봉수 */
    double prev_h, prev_l, prev_c;
    double sm_tr, sm_up_dm, sm_dn_dm; /* 평활TR/평활상승DM/평활하락DM */
    double dx_sum;       /* DX누적 */
    double adx_state;    /* 계산ADX */
    bool valid_state;
    /* 출력 */
    double adx;          /* ADX출력 */
    double plus_di;      /* 상승DI출력 */
    double minus_di;     /* 하락DI출력 */
    bool valid;          /* ADX유효출력 */
} tr_adx1_t;

bool tr_adx1_init(tr_adx1_t *s, int32_t period_input);

/* 매 봉 1회. session_first는 CurrentBar==1 또는 (1분봉 && 일자 변경) 대응. */
void tr_adx1_on_bar(tr_adx1_t *s, double h, double l, double c, bool session_first);

#endif
