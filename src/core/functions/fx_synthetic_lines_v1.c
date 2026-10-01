#include "core/functions/fx_synthetic_lines_v1.h"

#include <math.h>
#include <string.h>

#include "core/functions/fx_session_key_v1.h"
#include "core/functions/linreg.h"

/* Round(v/PS,0)*PS 대응 (국내 엔진의 floor 패턴과 동일) */
static double round_to_tick(double v, double ps) {
    return floor(v / ps + 0.5) * ps;
}

/* 봉시각기준==1(종료시각)의 자정 롤백: 날짜를 하루 당긴다 (원본 52~70줄) */
static int64_t prev_day_rollback(int64_t yyyymmdd) {
    int64_t day = yyyymmdd % 100;
    int64_t month = ((yyyymmdd - day) / 100) % 100;
    int64_t year = (yyyymmdd - month * 100 - day) / 10000;
    day -= 1;
    if (day == 0) {
        month -= 1;
        if (month == 0) {
            month = 12;
            year -= 1;
        }
        day = 31;
        if (month == 4 || month == 6 || month == 9 || month == 11) {
            day = 30;
        }
        if (month == 2) {
            day = 28;
            if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) {
                day = 29;
            }
        }
    }
    return year * 10000 + month * 100 + day;
}

bool tr_fxsyn_init(tr_fxsyn_t *s, const tr_fxsyn_config_t *cfg) {
    if (s == 0 || cfg == 0 ||
        (cfg->synth_min != 5 && cfg->synth_min != 15 && cfg->synth_min != 30) ||
        (cfg->time_basis != 0 && cfg->time_basis != 1) || cfg->price_scale <= 0.0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->cfg = *cfg;
    s->saved_bucket = -1;
    s->prev_elapsed = -2;
    /* memset 다음에 시계열 저장소를 연결한다 (순서 고정) */
    ylv_init(&s->mids, s->mids_buf, 100);
    ylv_init(&s->typical, s->typical_buf, 100);
    ylv_init(&s->vols, s->vols_buf, 100);
    return true;
}

void tr_fxsyn_eval(tr_fxsyn_t *s, const tr_fxsyn_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    int64_t cur_key = tr_fx_session_key_v1(in->cur_date, in->cur_time);

    /* 새 1분봉 첫 평가에서만 직전 완료 봉을 처리한다 (원본 27~28줄) */
    if (!in->is_new_bar) {
        return;
    }
    /* 세션 키 변경 리셋 (원본 30~39줄) */
    if (!s->has_key || cur_key != s->saved_key) {
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
    if (!in->has_prev) {
        return; /* CurrentBar > 1 대응 (원본 41줄) */
    }

    /* 직전 봉의 분 (원본 45~46줄) + 봉시각기준==1 시 1분 당김·자정 롤백 (47~71줄) */
    int64_t prev_date = in->prev_date;
    int32_t prev_min = (int32_t)((in->prev_time - in->prev_time % 10000) / 10000) * 60 +
                       (int32_t)((in->prev_time % 10000 - in->prev_time % 100) / 100);
    if (s->cfg.time_basis == 1) {
        prev_min -= 1;
        if (prev_min < 0) {
            prev_min = 1439;
            prev_date = prev_day_rollback(prev_date);
        }
    }
    /* 이전시각 재구성 (원본 72줄) 후 세션 키 대조 (73~74줄) */
    int64_t prev_time = (int64_t)(prev_min / 60) * 10000 + (int64_t)(prev_min % 60) * 100;
    int64_t prev_key = tr_fx_session_key_v1(prev_date, prev_time);
    if (prev_key != cur_key) {
        return; /* 세션 첫 봉의 직전 봉은 이전 세션 — 처리하지 않는다 */
    }

    /* 경과분/버킷 (원본 76~79줄): 구간 시작 07:00/15:30 기준 */
    int32_t elapsed;
    if (prev_min >= 420 && prev_min < 930) {
        elapsed = prev_min - 420;
    } else if (prev_min >= 930) {
        elapsed = prev_min - 930;
    } else {
        elapsed = prev_min + 510;
    }
    int32_t bucket = (elapsed - elapsed % s->cfg.synth_min) / s->cfg.synth_min;
    if (bucket != s->saved_bucket) {
        s->saved_bucket = bucket;
        s->agg_count = 0;
        s->prev_elapsed = -2;
    }

    /* 누락 분 제외 규칙 (원본 85~100줄) */
    if (elapsed % s->cfg.synth_min == 0) {
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

    /* 버킷 완성 (원본 103~151줄) */
    if (s->agg_count == s->cfg.synth_min && elapsed % s->cfg.synth_min == s->cfg.synth_min - 1) {
        ylv_push(&s->mids, (s->agg_high + s->agg_low) / 2.0);
        ylv_push(&s->typical, (s->agg_high + s->agg_low + s->agg_close) / 3.0);
        ylv_push(&s->vols, s->agg_vol);
        s->agg_count = 0;

        s->out_reg = 0.0;
        s->out_mkt = 0.0;
        s->out_reg_valid = 0;
        s->out_mkt_valid = 0;

        /* 평탄회귀 (원본 119~137줄): 완료중간 OLS, 5개 이상 */
        size_t done = ylv_count(&s->mids);
        int32_t rc = (int32_t)fmin((double)done, fmin(100.0, fmax(5.0, (double)s->cfg.reg_period)));
        if (rc >= 5) {
            double y[100];
            for (int32_t j = 0; j < rc; j++) {
                ylv_at(&s->mids, (size_t)(rc - 1 - j), &y[j]); /* 오래된 합성봉이 x=j */
            }
            tr_ols_result_t r;
            tr_ols_fit(y, (size_t)rc, 0.0, 5, &r);
            if (r.valid) {
                s->out_reg = round_to_tick(r.current, s->cfg.price_scale);
                s->out_reg_valid = 1;
            }
        }

        /* 마켓 (원본 139~150줄): 완료대표 거래량가중, 2개 이상 */
        int32_t mc = (int32_t)fmin((double)done, fmin(100.0, fmax(2.0, (double)s->cfg.mkt_period)));
        double vol_sum = 0.0, pv_sum = 0.0;
        for (int32_t j = 0; j < mc; j++) {
            double v = 0.0, tp = 0.0;
            ylv_at(&s->vols, (size_t)j, &v);
            ylv_at(&s->typical, (size_t)j, &tp);
            vol_sum += v;
            pv_sum += tp * v; /* 최신→과거 합산 순서 유지 (원본 루프 순서) */
        }
        if (mc >= 2 && vol_sum > 0.0) {
            s->out_mkt = pv_sum / vol_sum;
            s->out_mkt_valid = 1;
        }
    }
}

bool tr_fxsyn_relink(tr_fxsyn_t *s) {
    if (s == 0) {
        return false;
    }
    bool ok = ylv_relink(&s->mids, s->mids_buf);
    ok = ylv_relink(&s->typical, s->typical_buf) && ok;
    return ylv_relink(&s->vols, s->vols_buf) && ok;
}
