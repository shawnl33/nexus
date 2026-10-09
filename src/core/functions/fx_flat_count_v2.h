#ifndef TR_FX_FLAT_COUNT_V2_H
#define TR_FX_FLAT_COUNT_V2_H

/* WSF_FXFlatCountV2_CO. 평탄선 위·아래 봉 수와 기울기 방향이 이어진 봉 수. */

typedef struct {
    int pos;
    int prev_up_pos;
    int prev_dn_pos;
    double range_ticks;
    int slope;
    int prev_up_slope;
    int prev_dn_slope;
    int valid;
    double hi, lo;
    int slope_dir;
} tr_fxflat_out_t;

void tr_fxflat_step(const tr_fxflat_out_t *prev, int session_reset,
                    double flat, double flat_prev, double high, double low,
                    double close, double close_prev, double price_scale, int min_bars,
                    tr_fxflat_out_t *out);

#endif
