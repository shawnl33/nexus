#include "core/functions/ks_synthetic_v1.h"

#include <math.h>
#include <string.h>

#include "core/functions/linreg.h"

static double round_to_tick(double v, double ps) {
    return floor(v / ps + 0.5) * ps;
}

static int32_t hms_to_sec(int64_t hhmmss) {
    int32_t h = (int32_t)(hhmmss / 10000);
    int32_t m = (int32_t)((hhmmss / 100) % 100);
    int32_t sec = (int32_t)(hhmmss % 100);
    return h * 3600 + m * 60 + sec;
}

/* 음수 시각도 하루 안으로 되돌린다. 원본의 (x+86400)%86400 대응. */
static int32_t mod_day(int32_t sec) {
    sec %= 86400;
    if (sec < 0) {
        sec += 86400;
    }
    return sec;
}

bool tr_kssyn_init(tr_kssyn_t *s, const tr_kssyn_config_t *cfg) {
    if (s == 0 || cfg == 0 ||
        (cfg->synth_min != 5 && cfg->synth_min != 15 && cfg->synth_min != 30) ||
        (cfg->time_basis != 0 && cfg->time_basis != 1) || cfg->price_scale <= 0.0 ||
        (cfg->bar_sec != 60 && cfg->bar_sec != 15)) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->cfg = *cfg;
    s->bar_sec = cfg->bar_sec;
    /* 1분: 합성봉수=합성분. 15초: 합성봉수=합성분*4. */
    s->synth_bars = cfg->synth_min * (60 / cfg->bar_sec);
    s->saved_bucket = -1;
    s->prev_elapsed = -2;
    tr_ks_session_init(&s->session);
    ylv_init(&s->mids, s->mids_buf, 100);
    ylv_init(&s->typical, s->typical_buf, 100);
    ylv_init(&s->vols, s->vols_buf, 100);
    return true;
}

void tr_kssyn_eval(tr_kssyn_t *s, const tr_kssyn_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    int64_t prev_key = s->session.has_bar ? s->session.session_no : 0;
    int64_t cur_key = tr_ks_session_eval(&s->session, in->is_new_bar, in->current_bar,
                                         in->bdate, in->day_index);
    if (!in->is_new_bar) {
        return;
    }

    /* 세션 키 변경 리셋 (원본 28~38줄). 장시작초는 이 봉의 시각·DayIndex로 구한다. */
    if (!s->has_key || cur_key != s->saved_key) {
        int32_t now = hms_to_sec(in->cur_time);
        s->session_start = mod_day(now - in->day_index * s->bar_sec -
                                   s->cfg.time_basis * s->bar_sec);
        s->saved_key = cur_key;
        s->has_key = true;
        s->saved_bucket = -1;
        s->agg_count = 0;
        s->prev_elapsed = -2;
        s->out_reg = 0.0;
        s->out_mkt = 0.0;
        s->out_reg_valid = 0;
        s->out_mkt_valid = 0;
        ylv_clear(&s->mids);
        ylv_clear(&s->typical);
        ylv_clear(&s->vols);
    }
    if (!in->has_prev || prev_key != cur_key) {
        return; /* 세션 첫 봉의 직전 봉은 넣지 않는다 (원본 41, 47줄) */
    }

    int32_t prev_sec = mod_day(hms_to_sec(in->prev_time) - s->cfg.time_basis * s->bar_sec);
    int32_t elapsed = mod_day(prev_sec - s->session_start) / s->bar_sec;
    int32_t span = s->synth_bars;
    int32_t bucket = (elapsed - elapsed % span) / span;
    if (bucket != s->saved_bucket) {
        s->saved_bucket = bucket;
        s->agg_count = 0;
        s->prev_elapsed = -2;
    }

    if (elapsed % span == 0) {
        s->agg_count = 1;
        s->agg_high = in->prev_h;
        s->agg_low = in->prev_l;
        s->agg_close = in->prev_c;
        s->agg_vol = fmax(0.0, in->prev_v);
    } else if (s->agg_count > 0 && elapsed == s->prev_elapsed + 1) {
        s->agg_count += 1;
        s->agg_high = fmax(s->agg_high, in->prev_h);
        s->agg_low = fmin(s->agg_low, in->prev_l);
        s->agg_close = in->prev_c;
        s->agg_vol += fmax(0.0, in->prev_v);
    } else {
        s->agg_count = 0;
    }
    s->prev_elapsed = elapsed;

    if (s->agg_count == span && elapsed % span == span - 1) {
        ylv_push(&s->mids, (s->agg_high + s->agg_low) / 2.0);
        ylv_push(&s->typical, (s->agg_high + s->agg_low + s->agg_close) / 3.0);
        ylv_push(&s->vols, s->agg_vol);
        s->agg_count = 0;

        s->out_reg = 0.0;
        s->out_mkt = 0.0;
        s->out_reg_valid = 0;
        s->out_mkt_valid = 0;

        size_t done = ylv_count(&s->mids);
        int32_t rc = (int32_t)fmin((double)done, fmin(100.0, fmax(5.0, (double)s->cfg.reg_period)));
        if (rc >= 5) {
            double y[100];
            for (int32_t j = 0; j < rc; j++) {
                ylv_at(&s->mids, (size_t)(rc - 1 - j), &y[j]);
            }
            tr_ols_result_t r;
            tr_ols_fit(y, (size_t)rc, 0.0, 5, &r);
            if (r.valid) {
                s->out_reg = round_to_tick(r.current, s->cfg.price_scale);
                s->out_reg_valid = 1;
            }
        }

        int32_t mc = (int32_t)fmin((double)done, fmin(100.0, fmax(2.0, (double)s->cfg.mkt_period)));
        double vol_sum = 0.0, pv_sum = 0.0;
        for (int32_t j = 0; j < mc; j++) {
            double v = 0.0, tp = 0.0;
            ylv_at(&s->vols, (size_t)j, &v);
            ylv_at(&s->typical, (size_t)j, &tp);
            vol_sum += v;
            pv_sum += tp * v;
        }
        if (mc >= 2 && vol_sum > 0.0) {
            s->out_mkt = pv_sum / vol_sum;
            s->out_mkt_valid = 1;
        }
    }
}

bool tr_kssyn_relink(tr_kssyn_t *s) {
    if (s == 0) {
        return false;
    }
    bool ok = ylv_relink(&s->mids, s->mids_buf);
    ok = ylv_relink(&s->typical, s->typical_buf) && ok;
    return ylv_relink(&s->vols, s->vols_buf) && ok;
}
