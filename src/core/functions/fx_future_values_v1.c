#include "core/functions/fx_future_values_v1.h"

#include <math.h>
#include <string.h>

/* Round(v/PS,0)*PS 대응 (국내 엔진의 floor 패턴과 동일) */
static double round_to_tick(double v, double ps) {
    return floor(v / ps + 0.5) * ps;
}

bool tr_fxfv_init(tr_fxfv_t *s, const tr_fxfv_config_t *cfg) {
    if (s == 0 || cfg == 0 || cfg->market_period < 1 || cfg->price_scale <= 0.0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->cfg = *cfg;
    /* memset 다음에 윈도우 저장소를 연결한다 (순서 고정) */
    if (!tr_ring_init(&s->bar_win, s->bar_win_buf, sizeof(tr_fxfv_bar_t),
                      (size_t)cfg->market_period)) {
        return false;
    }
    tr_fxreg_init(&s->reg_pred, TR_COMPRESS_MIN, 1);
    tr_fxreg_init(&s->reg_core, TR_COMPRESS_MIN, 1);
    tr_fxp2_init(&s->predict);
    tr_fxc_init(&s->curve);
    tr_fxsw_init(&s->swing);
    return true;
}

void tr_fxfv_eval(tr_fxfv_t *s, const tr_fxfv_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }
    const double ps = s->cfg.price_scale;
    int64_t key = tr_fx_session_key_v1(in->date, in->time);

    /* 출력 기본값 (원본 129~147줄) */
    memset(&s->out, 0, sizeof(s->out));

    /* 봉 상대 시리즈 복원/스냅샷: 새 봉이면 직전 봉 말 값으로 [1]을 갱신하고,
     * 같은 봉 재평가는 직전 봉 말 상태에서 다시 시작한다 */
    bool is_new_bar = !s->has_bar || in->bar_open != s->last_bar_open;
    if (is_new_bar) {
        s->prev_bar = s->series;
    } else {
        s->series = s->prev_bar;
    }
    s->is_new_bar = is_new_bar;
    if (is_new_bar) {
        s->last_bar_open = in->bar_open;
        s->has_bar = true;
    }
    /* FX초기화/FX세션봉수 (원본 152~155줄) — 같은 봉 재평가에도 같은 값 */
    bool reset = !s->has_bar || key != s->series.key;
    if (is_new_bar) {
        s->session_reset = reset;
        s->session_bars = reset ? 1 : s->series.session_bars + 1;
    }
    reset = s->session_reset;
    int32_t bars = s->session_bars;

    /* 마켓 윈도우 갱신: 새 봉 push, 같은 봉은 [0] 갱신 (chart H/L/C/V[j] 대응) */
    if (is_new_bar) {
        tr_fxfv_bar_t b = {in->high, in->low, in->close, in->volume};
        tr_ring_push(&s->bar_win, &b);
    } else {
        tr_fxfv_bar_t *b0 = (tr_fxfv_bar_t *)tr_ring_get_mut(&s->bar_win, 0);
        if (b0 != 0) {
            b0->h = in->high;
            b0->l = in->low;
            b0->c = in->close;
            b0->v = in->volume;
        }
    }

    /* FXRegV1 #1 (곡선회귀 — 예측용, 원본 158~161줄) */
    tr_fxreg_input_t rin;
    memset(&rin, 0, sizeof(rin));
    rin.session_reset = reset;
    rin.session_bars = bars;
    rin.price = (in->high + in->low) / 2.0;
    rin.bar_open = in->bar_open;
    tr_fxreg_eval(&s->reg_pred, &rin);

    /* FXPredictV2 (원본 165~185줄) — H/L/C는 세션TR용 (원본 chart builtin 대응) */
    tr_fxp2_input_t pin;
    memset(&pin, 0, sizeof(pin));
    pin.cur_line = s->reg_pred.line;
    pin.slope = s->reg_pred.slope;
    pin.reg_valid = s->reg_pred.reg_valid;
    pin.r2 = s->reg_pred.r2;
    pin.high = in->high;
    pin.low = in->low;
    pin.close = in->close;
    pin.session_reset = reset;
    pin.session_bars = bars;
    pin.is_new_bar = is_new_bar;
    for (int k = 0; k < 5; k++) {
        pin.horizons[k] = s->cfg.predict_bars[k];
    }
    tr_fxp2_eval(&s->predict, &pin);

    /* 상태_곡선회귀선_평탄 (원본 188줄) — 매 평가 갱신 */
    s->state_reg_flat = round_to_tick(s->reg_pred.line, ps);

    /* 지속저장 (원본 189~246줄) */
    if (reset) {
        s->series.persist_streak = 0.0;
        s->series.persist_valid = 0;
        s->series.persist_dir = 0;
        memset(s->series.persist_target, 0, sizeof(s->series.persist_target));
    } else {
        int dir2 = s->predict.pred_dir[1];
        if (dir2 == s->series.prev_pred_dir2 && dir2 != 0) {
            s->series.persist_streak += 1.0;
        } else {
            s->series.persist_streak = 1.0;
        }
    }
    if (s->reg_pred.reg_valid && s->reg_pred.r2 >= s->cfg.min_r2 &&
        s->predict.pred_dir[1] != 0 &&
        s->series.persist_streak == fmax(1.0, (double)s->cfg.persist_bars)) {
        s->series.persist_valid = 1;
        s->series.persist_dir = s->predict.pred_dir[1];
        for (int k = 0; k < 5; k++) {
            s->series.persist_target[k] = round_to_tick(s->predict.pred_price[k], ps);
        }
    }

    /* 마켓중심가격 VWAP (원본 249~267줄): (H+L+C)/3 거래량가중, 세션봉수 상한 */
    s->state_mkt_center = 0.0;
    s->mkt_valid = false;
    if (reset) {
        s->state_mkt_center = 0.0;
    }
    int32_t calc_bars = bars < s->cfg.market_period ? bars : s->cfg.market_period;
    double vol_sum = 0.0, pv_sum = 0.0;
    for (int32_t j = 0; j < calc_bars; j++) {
        tr_fxfv_bar_t b;
        if (!tr_ring_at(&s->bar_win, (size_t)j, &b)) {
            break;
        }
        double tp = (b.h + b.l + b.c) / 3.0;
        if (b.v > 0.0) {
            vol_sum += b.v;
            pv_sum += tp * b.v;
        }
    }
    if (vol_sum > 0.0 && calc_bars >= 2) {
        s->state_mkt_center = pv_sum / vol_sum;
        s->mkt_valid = true;
    }

    /* 핵심일분봉 블록 (원본 277~335줄 — 실행 게이트상 항상 참) */
    /* FXCurveV1 (원본 279~280줄) */
    tr_fxc_input_t cin;
    memset(&cin, 0, sizeof(cin));
    cin.session_reset = reset;
    cin.session_bars = bars;
    cin.high = in->high;
    cin.low = in->low;
    cin.ticks = s->cfg.predict_ticks;
    cin.is_new_bar = is_new_bar;
    tr_fxc_eval(&s->curve, &cin);

    /* 핵심세션미래방향 (원본 282~283줄): 세션 첫 봉은 0 */
    double cur_future = reset ? 0.0 : s->curve.direction;

    /* FXRegV1 #2 (핵심곡선회귀 — 회귀방향용, 원본 288~289줄) */
    tr_fxreg_eval(&s->reg_core, &rin);

    /* 핵심회귀방향 (원본 290~296줄) */
    double core_flat = round_to_tick(s->reg_core.line, ps);
    int reg_dir = 0;
    if (s->reg_core.reg_valid && s->reg_core.r2 >= s->cfg.min_r2) {
        if (in->close > core_flat) {
            reg_dir = 1;
        }
        if (in->close < core_flat) {
            reg_dir = -1;
        }
    }

    /* 핵심마켓 (원본 298~305줄): 차트에 그리는 거래량 가중 마켓중심.
     * 무효 봉은 중심 0. 기울기는 세션봉수>1 이고 이번·직전 봉이 모두 유효일 때만. */
    double core_mkt = 0.0;
    if (s->mkt_valid) {
        core_mkt = s->state_mkt_center;
    }
    double mkt_slope = 0.0;
    if (bars > 1 && s->mkt_valid && s->series.prev_mkt_valid) {
        mkt_slope = core_mkt - s->series.prev_core_mkt;
    }
    int mkt_dir = 0;
    if (in->close > core_mkt && mkt_slope > 0.0) {
        mkt_dir = 1;
    }
    if (in->close < core_mkt && mkt_slope < 0.0) {
        mkt_dir = -1;
    }

    /* 단계화_1분_통합 (원본 309~315줄, 기세 기준은 V3_CO 312~318줄). 리셋 봉은 0.
     * 기세무시틱 0이면 변화의 부호만 본다. 음수는 0과 같다. */
    double mom_change = cur_future - s->series.prev_future_dir;
    double mom_basis = fmax(0.0, s->cfg.momentum_ignore_ticks) * ps;
    double score = (cur_future > 0.0 ? 2.0 : (cur_future < 0.0 ? -2.0 : 0.0)) +
                   (mom_change > mom_basis ? 1.0 : 0.0) +
                   (mom_change < -mom_basis ? -1.0 : 0.0) +
                   (double)mkt_dir + (double)reg_dir;
    if (reset) {
        score = 0.0;
    }

    /* FXSwingV1 (원본 317~334줄) */
    tr_fxsw_input_t win;
    memset(&win, 0, sizeof(win));
    win.session_reset = reset;
    win.session_bars = bars;
    win.future_dir = cur_future;
    win.min_hold_bars = s->cfg.min_hold_bars;
    win.high = in->high;
    win.low = in->low;
    win.is_new_bar = is_new_bar;
    win.swing_link = s->cfg.swing_link;
    tr_fxsw_eval(&s->swing, &win);

    /* 출력 (원본 337~357줄) */
    s->out.score = score;
    if (s->reg_pred.reg_valid) {
        s->out.reg_flat = s->state_reg_flat;
    }
    if (s->mkt_valid) {
        s->out.market_center = s->state_mkt_center;
    }
    if (s->series.persist_valid) {
        for (int k = 0; k < 5; k++) {
            s->out.persist_target[k] = s->series.persist_target[k];
        }
    }
    s->out.lup_high = s->swing.out_lup.high;
    s->out.lup_low = s->swing.out_lup.low;
    s->out.lup_382 = s->swing.out_lup.lvl_382;
    s->out.lup_500 = s->swing.out_lup.lvl_500;
    s->out.lup_618 = s->swing.out_lup.lvl_618;
    s->out.ldn_high = s->swing.out_ldn.high;
    s->out.ldn_low = s->swing.out_ldn.low;
    s->out.ldn_382 = s->swing.out_ldn.lvl_382;
    s->out.ldn_500 = s->swing.out_ldn.lvl_500;
    s->out.ldn_618 = s->swing.out_ldn.lvl_618;

    /* 시리즈 봉 말 값 갱신 (다음 봉의 [1] 대응) */
    s->series.key = key;
    s->series.session_bars = bars;
    s->series.prev_pred_dir2 = s->predict.pred_dir[1];
    s->series.prev_future_dir = cur_future;
    s->series.prev_core_mkt = core_mkt;
    s->series.prev_mkt_valid = s->mkt_valid ? 1 : 0;
}

bool tr_fxfv_relink(tr_fxfv_t *s) {
    if (s == 0) {
        return false;
    }
    bool ok = tr_fxreg_relink(&s->reg_pred);
    ok = tr_fxreg_relink(&s->reg_core) && ok;
    ok = tr_fxp2_relink(&s->predict) && ok;
    ok = tr_fxc_relink(&s->curve) && ok;
    s->bar_win.storage = s->bar_win_buf; /* 마켓 윈도우 저장소 재연결 */
    return ok;
}
