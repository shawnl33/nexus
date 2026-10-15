#ifndef TR_FX_OS_FILTER_H
#define TR_FX_OS_FILTER_H

#include <stdint.h>

/* #우드스탁_해외선물1분통합판정 V14 이후 필터.
 * 스코프가 만든 후보·선행·구조돌파를 받아 원본 순서로 지운다.
 * 막는 조건이 없으면 후보와 조정은 그대로다. 선행과 구조돌파는 항상 없다.
 */

typedef struct {
    int dst_auto;         /* 서머타임자동(1) */
    int rth_manual;       /* 본장시작시각(223000) */
    int rth_summer;       /* 서머타임본장(223000) */
    int rth_winter;       /* 표준시본장(233000) */
    int pre_min;          /* 본장전제한분(5) */
    int adj_sell_allow;  /* 조정매도허용분(60) */
    int s_buy_min;       /* 후보S매수허용분(150) */
    int s_sell_min;      /* 후보S매도허용분(150) */
    int s_buy_sq;        /* 후보S매수압축중금지(0) */
    int b_three_max;     /* 후보B삼선상한(30) */
    int b_two_max;       /* 후보B두구간상한(60) */
    int b_buy_pre;       /* 후보B매수본장전금지(1) */
    int sell_pos_lim;    /* 후보매도위치기준(-10). 0이면 끔 */
    int sell_width_lim;  /* 후보매도폭비율기준(100) */
    int sell_engulf;     /* 후보매도감싸기통합(1) */
    int brk_gap;         /* 돌파매도목표5여유(5) */
    int sell_engulf2;    /* 후보매도감싸기삼선두구간(1) */
    int sell_two_min;    /* 후보매도두구간기준(99) */
    int tall_on;         /* 장대봉필터(1) */
    int tall_amp;        /* 장대진폭율기준(80) */
    int adj_prev_up;     /* 조정매수직전상승제한(30) */
    int adj_sell_swing;  /* 조정매도스윙폭제한(90) */
    int adj_buy_swing;   /* 조정매수스윙폭제한(100) */
    int adj_slope;       /* 조정매도기울기기준(-20) */
    int adj_engulf;      /* 조정매수감싸기금지(1) */
    int adj_engulf_dn;   /* 조정매수하락감싸기금지(1) */
    int adj_engulf3;     /* 조정매수감싸기삼선(1) */
    int adj_flat_on;     /* 조정매수두구간평탄(1) */
    int adj_two_max;     /* 조정매수두구간기준(90) */
    int adj_flat_max;    /* 조정매수평탄기준(90) */
    int turn_buy_on;     /* 전환매수사용(1) */
    int turn_sell_on;    /* 전환매도사용(1) */
    int turn_pos_lo;     /* 전환위치하한(-3) */
    int turn_swing_max;  /* 전환스윙폭상한(50) */
    int turn_two_max;    /* 전환두구간상한(90) */
    int turn_excl;       /* 전환제외사용(1) */
    int turn_sq_lim;     /* 전환압축봉수제한(60) */
    int turn_gray;       /* 전환삼선회색금지(1). 매도만 */
    int turn_color_n;    /* 전환압축색봉수(10). 매도만 */
    int turn_opp_bars;   /* 전환반대금지봉수(10). 매도만 */
    int turn_sell_u;     /* 전환매도통합기준(1). -50만 허용 */
    int turn_sell_prev;  /* 전환매도직전100봉수(10) */
    int turn_buy_prev;   /* 전환매수직전100봉수(10) */
    int adj_a_mid_block; /* 조정A중하락구조금지(1). 0이어도 조정A매수_중은 지운다. */
} tr_fxos_filter_cfg_t;

typedef struct {
    int unified;
    int cand_dir;
    int cand_grade;
    int lead_dir;
    int brk_dir;
    int adj_dir;
    int adj_kind;
    int adj_grade;
    int date;
    int time;
    int turn_dir;
    int sq_bars;
    double three_ratio;
    double two_ratio;
    int lead_valid;
    int lead_pos;
    double low;
    double high;
    double close;
    double price_unit; /* 1포인트의 입력 단위. 0이면 원가격(1). */
    double target1;
    double target5;
    double width_ratio;
    double rise_hi;
    double rise_lo;
    double fall_hi;
    double fall_lo;
    double amp;
    int tall_buy;
    int tall_sell;
    int prev_up;
    int slope;
    int slope_prev;
    int width_seen;
    double swing;
    double flat_ratio;
    int pos;
    int prev_dn;
    int session_first;
    double prior_low;
    double prior_high;
    int hot_up;
    int hot_dn;
    int gray;
    int red_n;
    int bars_since_buy;
    int had_m100;
    int had_p100;
} tr_fxos_filter_in_t;

typedef struct {
    int cand_dir;
    int cand_grade;
    int lead_dir;
    int brk_dir;
    int adj_dir;
    int adj_kind;
    int adj_grade;
    int turn_dir;
    int turn_y;
    uint32_t turn_rgb;
    int turn_w;
} tr_fxos_filter_out_t;

void tr_fxos_filter_cfg_default(tr_fxos_filter_cfg_t *cfg);
/* 아는 키면 1을 반환하고 기본값 위의 그 칸만 바꾼다. */
int tr_fxos_filter_cfg_set(tr_fxos_filter_cfg_t *cfg, const char *key, double value);
/* 봉 날짜의 본장 시작 시각(hhmmss). 서머타임자동이 0이면 rth_manual. */
int tr_fxos_rth_open(int yyyymmdd, const tr_fxos_filter_cfg_t *cfg);
/* 인덱스 0이 이번 봉. 평균은 확인봉 바로 앞 avg_n개. buy_flag·sell_flag에 0 또는 1. */
void tr_fxos_tall_flags(const double *open, const double *high, const double *low, const double *close,
                        int avg_n, int confirm_n, double mult, double wick_pct, double body_pct,
                        int *buy_flag, int *sell_flag);
void tr_fxos_filter(const tr_fxos_filter_cfg_t *cfg, const tr_fxos_filter_in_t *in,
                    tr_fxos_filter_out_t *out);
/* 나중 신호가 앞 신호를 덮는다. 종류: 1 후보, 2 돌파, 3 선행, 5 전환, 4 조정. */
void tr_fxos_pick(int cand_dir, int brk_dir, int lead_dir, int turn_dir, int adj_dir, int *dir,
                  int *kind);
const char *tr_fxos_order_name(int dir, int kind, int grade, int adj_kind, int adj_grade);
const char *tr_fxos_exit_name(int sig_exit);

#endif
