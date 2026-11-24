#ifndef TR_WEEKLY_PAIR_V1_H
#define TR_WEEKLY_PAIR_V1_H

/* 위클리 옵션 페어 주문.
 * 양매수 V9: reference/yeslanguage/signals/#우드스탁_위클리_페어시스템_양매수.txt
 *   (2026-11-22). 손익은 신호 다음 1분 봉 시가. 공통손절은 -20 고정.
 *   호가·평탄 반감은 선물 호가와 WSF_KFDayFutureValuesV3_CO가 없어 내지 않는다.
 * 양매수 V34: 같은 계열의 이전 사슬.
 * 양매도 V3:  reference/yeslanguage/signals/#우드스탁_위클리_페어시스템_양매도.txt 704~1076.
 * V34와 양매도는 청산 사슬이 같고 V12 양매수는 1차 다음 봉부터 요일과 관계없이 2차 추적을 쓴다.
 * 양매도는 손익 부호와 진입 조건만 다르다.
 * 점수는 넣지 않는다. 삼선·이탈 값은 호출자가 tr_ksscore_eval 결과를 넘긴다.
 * 화면 시뮬은 tr_wpair_replay가 보유 1분봉을 다시 돌려 신호만 만든다.
 * 실주문·자동주문은 연결하지 않는다. 백테스트 신호다.
 *
 * 체결: 직전 봉이 낸 주문은 바로 다음 Index 봉의 시가에 체결된 뒤 그 봉의 식을 돈다.
 * 같은 Index 재평가는 체결 직후·식 이전으로 되돌린 뒤 주문을 다시 낸다.
 * 같은 봉 주문은 그 봉에서 포지션을 바꾸지 않는다.
 * Index가 한 칸이 아니면 직전 주문은 체결하지 않고 버린다. 청산 요청은 그 봉이 다시 낸다.
 */

#include <stddef.h>
#include <stdint.h>

#include "core/functions/ks_score_v1.h"

#define TR_WPAIR_LONG 1
#define TR_WPAIR_SHORT (-1)

#define TR_WPAIR_RULES_V34 0
#define TR_WPAIR_RULES_V45 1
#define TR_WPAIR_RULES_V12 2

#define TR_WPAIR_ORDER_NONE 0
#define TR_WPAIR_ORDER_ENTRY 1
#define TR_WPAIR_ORDER_EXIT_PART 2
#define TR_WPAIR_ORDER_EXIT_ALL 3

typedef struct {
    int trade_off; /* 매매종료. 1이면 신규 진입만 막는다 */
    double capital; /* 총투자금 */
    double take_pct; /* 익절률 */
    double stop_pct; /* 손절률 */
    int32_t entry_start_bar; /* 양매수 진입시작봉수 */
    int32_t afternoon_switch_bar; /* 양매수 오후전환봉수 */
    double morning_three_max; /* 오전삼선만남최대비율 */
    double afternoon_three_max; /* 오후삼선만남최대비율 */
    int64_t entry_start_time; /* 양매도 진입시작 HHMMSS */
    int64_t flat_time; /* 전량청산 HHMMSS */
    int meet_mode; /* 만남방식 1 범위, 2 종가 */
    double tolerance; /* 허용오차 */
    int diag; /* 진단출력사용. 주문에는 영향 없다 */
    int split_exit; /* 분할청산 */
    double expiry_resid_pct; /* V34 만기잔량익절률 */
    int32_t resid_wait_min; /* V34 잔량익절대기분 */
    int32_t low_wait_min; /* 저수익대기분 */
    double low_pct; /* 저수익기준률 */
    double low_cut_pct; /* 저수익청산비율 */
    double low_rewiden; /* 저수익재악화폭 */
    int32_t low_recover_min; /* 저수익회복대기분 */
    int ruleset; /* TR_WPAIR_RULES_*. V12가 현재 양매수 원본 */
    int expiry_override; /* V45 만기당일지정 */
    int32_t early_second_min; /* V45 조기이차기준분 */
    double trail_interval; /* V45 이차추적간격 */
} tr_wpair_config_t;

/* 이 차트(Data1)와 상대(Data3), 선물 1분(Data2), 점수에서 읽은 값. */
typedef struct {
    int32_t index; /* Index */
    int64_t bdate; /* BDate YYYYMMDD */
    int64_t sdate; /* sDate */
    int64_t stime; /* sTime HHMMSS */
    int64_t next_sdate;
    int64_t next_stime;
    int compress; /* DataCompress. 1분은 2 */
    int interval; /* BarInterval. 1분은 1 */
    double open, high, low, close;
    int64_t d3_date, d3_time;
    int d3_compress, d3_interval;
    double d3_open, d3_high, d3_low, d3_close;
    int64_t d2_date, d2_time;
    int d2_compress, d2_interval;
    int32_t d2_day_index;
    int calc_ready; /* 함수계산가능 */
    int three_ready; /* 삼선_목표준비 */
    double three_peak; /* 삼선_지속목표차_최고 */
    double three_ratio; /* 삼선_지속목표차_비율 */
    int break_ready; /* 이탈_삼선준비 */
    double two_max; /* 이탈_두구간최대가격범위 */
    double price_ratio; /* 이탈_두구간가격비율 */
    int d2_has_gap; /* 1이면 전일 종가와 당일 시가가 있다 */
    double d2_prev_close;
    double d2_day_open;
    int sq_ready; /* 압준비. 1이면 개장 30분 이후 매물대 폭이 있다 */
    double sq_ratio; /* 압비율. 최근 90봉 최대폭 대비 백분율 */
    int sq_release; /* 50% 미만 15봉 뒤 폭이 넓어진 30분 창 */
    int flat_valid; /* 평탄 카운트 유효. 0이면 호가 반감 청산 불가 */
    int flat_pos; /* 보호_위치Cnt */
    int flat_slope; /* 보호_기울기Cnt */
    int book_valid; /* 호가유효. 호가 잔량이 없는 봉은 0 */
    double book_score; /* 호가방향점수 */
} tr_wpair_bar_t;

typedef struct {
    int market_position; /* 양매수 1, 양매도 -1, 없음 0 */
    int32_t contracts;
    int order_kind;
    int32_t order_qty;
    int order_reason; /* 0 진입, 1·2·3·4·9·10·11·12·14 청산, 13 저수익 부분 */
    const char *order_name;
    int entry_pending;
    int exit_req;
    int exit_reason;
    int used_today;
    int model_valid;
    int holding;
    int first_done;
    int first_pending;
    int32_t first_qty;
    int first_reason;
    int low_done;
    int low_pending;
    int32_t low_qty;
    double pair_pnl;
    double pair_pct;
    double self_resid_pct;
    double book_realized;
    double book_unreal;
    double book_total;
    double book_total_pct;
    int32_t book_qty;
    int32_t self_qty;
    int32_t opp_qty;
    double self_first;
    double opp_first;
    double self_entry;
    int32_t entry_elapsed;
    int32_t resid_elapsed;
    int32_t signal_bar;
    int trail_active; /* V45 만기 2차 추적 상태 */
    double trail_target;
    double trail_peak;
    double trail_protect;
} tr_wpair_out_t;

/* 식의 Var. 봉 시작 복사용이며 포인터가 없다. */
typedef struct {
    int64_t saved_date;
    int32_t entry_fut_bar;
    int64_t entry_fut_date;
    int32_t entry_elapsed;
    int low_done;
    int low_pending;
    int32_t low_qty;
    int32_t low_prev_qty;
    int32_t book_qty;
    double book_realized;
    double book_unreal;
    double book_total;
    double book_total_pct;
    double book_principal;
    int used_today;
    int first_done;
    int first_pending;
    int32_t first_qty;
    int32_t first_prev_qty;
    int first_reason;
    double first_basis_pct;
    int32_t first_fut_bar;
    int64_t first_fut_date;
    int32_t resid_elapsed;
    int ratio_dipped;
    int holding;
    int exit_req;
    int model_valid;
    int32_t self_qty;
    int32_t opp_qty;
    double model_notional;
    double self_entry;
    double opp_entry;
    double self_first;
    double opp_first;
    int32_t signal_bar;
    int64_t signal_date;
    int64_t signal_time;
    int32_t open_target_min; /* 신호 시각의 다음 1분. 그 봉 시가만 손익 기준 */
    int64_t open_date;
    int64_t open_time;
    double rec_meet; /* 진입 신호의 공통 만남가격 */
    int64_t rec_date;
    int32_t rec_bar;
    int32_t rec_total;
    int32_t rec_loss;
    int rec_cur_loss;
    int32_t rec_fire_bar; /* 손실봉 부분청산을 낸 선물 봉. 그 봉의 1차 익절은 보류 */
    int entry_pending;
    int exit_reason;
    double pair_pnl;
    double pair_pct;
    double self_resid_pct;
    int market_position;
    int32_t contracts;
    int order_kind;
    int32_t order_qty;
    int order_reason;
    const char *order_name;
    int trail_active;
    double trail_target;
    double trail_peak;
    double trail_protect;
    int three40_watch; /* 진입 후 삼선 비율을 한 번이라도 봤으면 1 */
    int three40_hit; /* 그 비율이 40 이상인 적이 있으면 1 */
    int64_t low_obs_date;
    int32_t low_obs_bar;
    int low_hist_n;
    double low_cur, low_p1, low_p2, low_p3, low_p4, low_p5;
    int low_drop_ok;
    int low_extra_done;
    int32_t low_extra_bar;
    double low_extra_peak;
    int32_t low_extra_wait;
    int low_extra_ok;
    int64_t high_date;
    int32_t morn_hi_self;
    int32_t morn_hi_opp;
    int high_block; /* 오전 고가 20 초과가 한쪽이라도 3봉이면 그날 진입 금지 */
    int morn_hi_any; /* 오전 고가 20 초과가 한 봉이라도 있으면 오전 진입 금지, 오후 삼선 상한 70 */
    int entry_path; /* V12 양매수 1 삼선, 2 압축해제. 0이면 이전 주문명 */
    /* V9. 호가 반감(사유 19)은 계산하지 않는다. */
    int v9_saw10;
    int v9_early_done;
    int v9_book_done;
    int v9_lead;
    int v9_book_with;
    int v9_book_flip;
    int32_t v9_book_min;
    int64_t v9_book_date;
    int v9_full_cut;
    int v9_recovering;
    int32_t v9_full_qty;
    int32_t v9_entry_min;
    int64_t v9_entry_date;
    double v9_peak;
    int32_t v9_peak_min;
    double v9_trough;
    double v9_recover_high;
    int32_t v9_obs;
    int32_t v9_loss;
    int v9_hist_n;
    int32_t v9_last_bar;
    int64_t v9_hist_date;
    double v9_p[6];
    int32_t v9_m[6];
} tr_wpair_rt;

typedef struct {
    int side;
    tr_wpair_config_t cfg; /* 봉 사이에 바꿔도 된다 */
    tr_wpair_rt now;
    tr_wpair_rt checkpoint;
    int32_t bar_index;
    int has_bar;
} tr_wpair_t;

void tr_wpair_default_config(tr_wpair_config_t *cfg);

/* side는 TR_WPAIR_LONG 또는 TR_WPAIR_SHORT. 실패 시 -1. */
int tr_wpair_init(tr_wpair_t *st, int side, const tr_wpair_config_t *cfg);

/* 실패 시 -1. out은 NULL이면 상태만 갱신한다. */
int tr_wpair_eval(tr_wpair_t *st, const tr_wpair_bar_t *bar, tr_wpair_out_t *out);

/* 오래된 봉이 앞이다. 시각·가격은 호출자가 맞춘다. day_index는 선물 봉의 당일 순번(0부터). */
typedef struct {
    int64_t open_us;
    int64_t ymd;
    int64_t hhmmss;
    int32_t day_index;
    double open, high, low, close, volume;
} tr_wpair_pxbar_t;

/* 신호봉에서 새로 낸 주문. name은 리터럴이라 반환 뒤에도 유효하다. */
typedef struct {
    int64_t time_sec; /* 체결봉 시작, unix 초. 다음 봉이 없으면 신호봉 */
    int kind;
    int reason;
    int32_t qty;
    int pending; /* 1이면 다음 봉이 없어 아직 체결되지 않음 */
    int32_t contracts; /* 이 봉의 식 이후 잔량. 이 주문은 아직 미체결 */
    const char *name;
} tr_wpair_event_t;

/* st는 init된 상태다. 호출할 때마다 그 설정으로 처음부터 다시 계산한다.
 * 자기 봉이 Index다. 자기 봉 이전에 열린 선물 봉은 점수에 먼저 넣는다.
 * 상대·선물 시각이 자기 봉과 같지 않으면 그 다리는 없는 것으로 둔다.
 * 마지막 봉이 낸 주문은 체결하지 않고 pending으로 남긴다.
 * 성공 시 신호 개수(cap보다 클 수 있다). ev에는 시간순으로 최근 cap개.
 * score_cfg가 NULL이면 틱 0.05 기본값. 실패 시 -1. */
/* 선물 1분봉의 매물대 압축. out은 n개. 실패 시 -1. */
typedef struct {
    int ready;
    double ratio;
    int release;
    int n30, n40, n50, n60;
} tr_wsq_point_t;

/* 압축비교 입력. 페어 원본은 이 기본값을 식 안에 고정한다.
 * price_scale은 Data2 코스피200 선물 PriceScale 0.05. */
typedef struct {
    double value_mult;
    int32_t min_bars;
    int32_t width_bars;
    int32_t ratio_bars;
    int32_t start_bar;
    double price_scale;
} tr_wsq_config_t;

void tr_wsq_default_config(tr_wsq_config_t *cfg);
int tr_wsq_eval(const tr_wpair_pxbar_t *fut, size_t n, const tr_wsq_config_t *cfg, tr_wsq_point_t *out);

int tr_wpair_replay(tr_wpair_t *st, const tr_wpair_pxbar_t *self, size_t nself,
                    const tr_wpair_pxbar_t *opp, size_t nopp, const tr_wpair_pxbar_t *fut,
                    size_t nfut, const tr_ksscore_config_t *score_cfg, tr_wpair_event_t *ev,
                    size_t cap);

/* eval이 낸 AtMarket을 장부에 넣는다. 체결 시각, 이름, 수량은 replay 이벤트와 같다. */
struct tr_yl_book;
int tr_wpair_fill(tr_wpair_t *st, const tr_wpair_pxbar_t *self, size_t nself,
                  const tr_wpair_pxbar_t *opp, size_t nopp, const tr_wpair_pxbar_t *fut,
                  size_t nfut, const tr_ksscore_config_t *score_cfg, struct tr_yl_book *book);

#endif
