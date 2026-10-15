#include "core/indicators/fx_sig_pair.h"

#include "core/indicators/fx_os_filter.h"

#include <stdio.h>
#include <string.h>

static int hm(int hms) {
    return (hms / 10000) * 60 + (hms / 100) % 100;
}

static int base_min(int hms) {
    int m = hm(hms);
    if (m < 12 * 60) {
        m += 1440;
    }
    return m;
}

static const char *group_name(int dir, int kind, int grade) {
    int buy = dir > 0;
    int brk = kind == 2 || kind == 3 || (kind == 1 && (grade == 8 || grade == 7 || grade == 4));
    int rev = kind == 5 || (kind == 1 && grade == 6);
    if (brk) {
        return buy ? "돌파매수" : "돌파매도";
    }
    if (rev) {
        return buy ? "반전매수" : "반전매도";
    }
    if (kind == 4) {
        return buy ? "조정매수" : "조정매도";
    }
    return buy ? "압축매수" : "압축매도";
}

static void set_name(tr_fxpair_t *s, int dir, int kind, int grade, int adj_kind, int adj_grade, const char *suf) {
    const char *tail = suf != 0 ? suf : "";
    if (s->v68) {
        if (strcmp(tail, "_장약세") == 0) {
            tail = "_약세";
        } else if (strcmp(tail, "_반대방향") == 0) {
            tail = "_반대";
        } else {
            tail = "";
        }
        snprintf(s->name, sizeof(s->name), "%s%s", group_name(dir, kind, grade), tail);
        return;
    }
    snprintf(s->name, sizeof(s->name), "%s%s", tr_fxos_order_name(dir, kind, grade, adj_kind, adj_grade), tail);
}

static int entry_qty(int weak, int dir, int kind, int grade, int adj_kind, int adj_grade, int fail, int yday,
                     int prev_buy_fail, int oppose, int base_min_v, int rth_min) {
    if (weak) {
        return 1;
    }
    if (dir > 0 && kind == 4 && adj_kind == 1 && adj_grade == 1 && !fail) {
        return 1;
    }
    if (dir < 0 && kind == 4 && adj_kind == 2 && adj_grade == 1 && oppose) {
        return 1;
    }
    if (dir > 0 && kind == 4 && adj_kind == 2 && adj_grade == 2 && !weak &&
        base_min_v >= rth_min - 75 && base_min_v < rth_min) {
        return 1;
    }
    if (dir < 0 && kind == 1 && grade == 3 && !weak && !yday &&
        base_min_v >= rth_min && base_min_v < rth_min + 15) {
        return 1;
    }
    (void)prev_buy_fail;
    return 2;
}

void tr_fxpair_init(tr_fxpair_t *s) {
    if (s == 0) {
        return;
    }
    memset(s, 0, sizeof(*s));
}

void tr_fxpair_on_bar(tr_fxpair_t *s, const tr_fxpair_in_t *in, tr_fxpair_out_t *out) {
    int dir;
    int bm;
    int rm;
    int trade;
    int virt0;
    int color_exit;
    int weak;
    int oppose;
    int fail;
    int yday;
    int prev_buy_fail;
    int i;
    int hits;
    const char *suf;
    if (out != 0) {
        memset(out, 0, sizeof(*out));
    }
    if (s == 0 || in == 0 || out == 0) {
        return;
    }
    double price_unit = in->price_unit > 0.0 ? in->price_unit : 1.0;
    if (in->session_first) {
        s->session_n = 0;
        s->pnl_buy = 0;
        s->pnl_sell = 0;
        s->uni_count = 0;
        s->uni_last = 0;
    }
    {
        /* 통합카운트: 색이 있으면 그 방향으로 더하고, 통합 위치가 바뀌면 0에서 다시 센다. */
        int udir = in->unified > 0 ? 1 : (in->unified < 0 ? -1 : 0);
        if (udir != 0 && udir != s->uni_last) {
            s->uni_count = 0;
            s->uni_last = udir;
        }
        if (in->color_dir != 0) {
            s->uni_count += in->color_dir;
        }
    }
    s->bar_n += 1;
    dir = in->dir;
    bm = base_min(in->time_hms);
    rm = hm(in->rth_hms);
    if (bm >= rm && bm < rm + 30) {
        if (in->kind == 5) {
            dir = 0;
        }
        if (in->kind == 4 && dir > 0 && in->adj_kind == 2 && (in->adj_grade == 1 || in->adj_grade == 2)) {
            dir = 0;
        }
    }
    if (in->kind == 1 && dir > 0 && in->grade == 3 && bm >= rm + 90 && bm < rm + 150) {
        dir = 0;
    }
    if (in->kind == 1 && dir > 0 && in->grade == 8 && hm(in->time_hms) >= rm - 90 && hm(in->time_hms) < rm - 60) {
        dir = 0;
    }
    if (in->width_ratio > 95.0 && in->swing > 95.0 && in->kind == 1 && dir > 0 && in->grade == 8 &&
        in->time_hms >= 70000 && hm(in->time_hms) < rm) {
        dir = 0;
    }
    trade = bm >= rm - 90 && bm < rm + 210;
    virt0 = s->virt;
    if (s->virt != 0) {
        if (in->high > s->hi) {
            s->hi = in->high;
        }
        if (in->low < s->lo) {
            s->lo = in->low;
        }
    }
    /* 1차 지정가는 진입 봉 종가에 걸리므로, 다음 봉부터 고가·저가가 닿은 첫 봉만 표시한다. */
    if (virt0 != 0 && !s->limit_filled && s->first_ticks > 0 && s->bar_n > s->entry_bar &&
        in->price_scale > 0.0) {
        double px = (double)s->first_ticks * in->price_scale;
        if ((virt0 > 0 && in->high >= s->entry_px + px) || (virt0 < 0 && in->low <= s->entry_px - px)) {
            s->limit_filled = 1;
            out->limit_hit = virt0;
            out->limit_hit_qty = s->remaining_qty > 0 ? 1 : 0;
            s->remaining_qty -= out->limit_hit_qty;
        }
    }
    color_exit = 0;
    /* 원본 ExitLong/ExitShort는 MarketPosition이 있을 때만 낸다. 가상포지션 청산은 그대로다. */
    int order_ok = !in->has_market || (virt0 > 0 && in->market > 0) || (virt0 < 0 && in->market < 0);
    if (virt0 > 0 && in->color_dir == -1 && s->last_color != -1) {
        color_exit = 1;
        if (order_ok) {
            out->exit_code = 1;
        }
    }
    if (virt0 < 0 && in->color_dir == 1 && s->last_color != 1) {
        color_exit = 1;
        if (order_ok) {
            out->exit_code = -1;
        }
    }
    if (order_ok && !trade && virt0 > 0) {
        out->exit_code = 2;
    }
    if (order_ok && !trade && virt0 < 0) {
        out->exit_code = -2;
    }
    if (out->exit_code != 0) {
        out->exit_qty = s->remaining_qty;
    }
    if (s->v68 && out->exit_code != 0) {
        int loss = (out->exit_code > 0 && in->close < s->entry_px) || (out->exit_code < 0 && in->close > s->entry_px);
        const char *side = out->exit_code > 0 ? "매수" : "매도";
        const char *kindn = (out->exit_code == 2 || out->exit_code == -2) ? "시간청산" : "색청산";
        snprintf(s->exit_name, sizeof(s->exit_name), "%s%s%s", kindn, side, loss ? "_손절" : "");
        out->exit_name = s->exit_name;
        out->exit_loss = loss;
    }
    if ((virt0 > 0 && ((in->color_dir == -1 && s->last_color != -1) || !trade || (trade && dir < 0))) ||
        (virt0 < 0 && ((in->color_dir == 1 && s->last_color != 1) || !trade || (trade && dir > 0)))) {
        int hit10 = 0;
        int hit1 = 0;
        double pnl;
        if (s->first_ticks > 0) {
            if (virt0 > 0 && s->hi >= s->entry_px + (double)s->first_ticks * in->price_scale) {
                hit1 = 1;
            }
            if (virt0 < 0 && s->lo <= s->entry_px - (double)s->first_ticks * in->price_scale) {
                hit1 = 1;
            }
        }
        if (virt0 > 0 && s->hi >= s->entry_px + 10.0 * in->price_scale) {
            hit10 = 1;
        }
        if (virt0 < 0 && s->lo <= s->entry_px - 10.0 * in->price_scale) {
            hit10 = 1;
        }
        if (hit1) {
            pnl = (double)s->first_ticks * in->price_scale + (in->close - s->entry_px) * (double)virt0;
        } else {
            pnl = (in->close - s->entry_px) * (double)virt0 * 2.0;
        }
        if (virt0 > 0) {
            s->pnl_buy += pnl;
        } else {
            s->pnl_sell += pnl;
        }
        s->prev_dir = virt0;
        s->prev_pnl = pnl;
        if (in->price_scale > 0.0) {
            s->move_ticks = (int)((in->close - s->entry_px) / in->price_scale * (double)virt0);
        }
        s->exit_bar = s->bar_n;
        s->prev_color_exit = color_exit;
        s->reach[s->done % 50] = hit10;
        s->done += 1;
        s->virt = 0;
        s->remaining_qty = 0;
    }
    oppose = 0;
    if (s->session_n == 0 && s->last_dir != 0 && dir != 0 && dir != s->last_dir) {
        oppose = 1;
    }
    if (oppose && in->kind == 1 && in->grade == 8 && in->time_hms >= 70000 && hm(in->time_hms) < rm) {
        dir = 0;
        oppose = 0;
    }
    if (oppose && in->kind == 1 && in->grade == 3 && dir < 0 && in->time_hms >= 70000 && bm < rm - 10) {
        dir = 0;
        oppose = 0;
    }
    if (oppose && in->kind == 1 && dir > 0 && in->grade == 8 && bm >= 1440 && bm < 1860) {
        dir = 0;
        oppose = 0;
    }
    if (in->kind == 1 && dir > 0 && in->grade == 8 && bm >= rm && bm < rm + 10) {
        dir = 0;
        oppose = 0;
    }
    if (dir > 0 && bm >= rm - 60 && bm < rm - 45) {
        dir = 0;
        oppose = 0;
    }
    if (in->lead_valid && dir > 0 && in->slope > 30 && in->pos < 0) {
        dir = 0;
        oppose = 0;
    }
    if (in->lead_valid && dir < 0 && in->slope < -30 && in->pos > 0) {
        dir = 0;
        oppose = 0;
    }
    if (in->kind == 4 && dir > 0 && in->adj_kind == 2 && in->adj_grade == 1 && bm >= rm + 180 && bm < 1860 &&
        s->pnl_buy > 0) {
        dir = 0;
        oppose = 0;
    }
    if (in->kind == 4 && dir > 0 && in->adj_kind == 2 && in->adj_grade == 2 && bm >= rm + 150 && bm < 1860 &&
        s->session_n == 0) {
        dir = 0;
        oppose = 0;
    }
    if (oppose && bm >= rm + 150 && bm < 1860) {
        dir = 0;
        oppose = 0;
    }
    fail = dir > 0 && s->prev_dir > 0 && s->prev_color_exit;
    if (fail && s->prev_pnl <= -2.5 * price_unit && in->kind == 4 &&
        (in->adj_kind == 1 || in->adj_kind == 2) && in->adj_grade == 1) {
        dir = 0;
        oppose = 0;
        fail = 0;
    }
    yday = dir < 0 && s->session_n == 0 && s->prev_dir < 0 && s->prev_color_exit;
    if (yday && in->kind == 1 && (in->grade == 3 || in->grade == 8)) {
        dir = 0;
        oppose = 0;
        yday = 0;
    }
    prev_buy_fail = dir < 0 && s->prev_dir > 0 && s->prev_color_exit;
    if (prev_buy_fail && in->kind == 1 && in->grade == 5) {
        dir = 0;
        oppose = 0;
        prev_buy_fail = 0;
    }
    weak = 0;
    if (s->done >= 10) {
        hits = 0;
        for (i = 0; i < 10; i++) {
            hits += s->reach[(s->done - 1 - i) % 50];
        }
        if (hits * 100 < 60 * 10) {
            weak = 1;
        }
    }
    if (weak && ((in->kind == 4 && dir > 0 && in->adj_kind == 1 && in->adj_grade == 1) ||
                 (in->kind == 5 && dir < 0) || (in->kind == 1 && dir > 0 && in->grade == 7) ||
                 (in->kind == 1 && dir < 0 && (in->grade == 7 || in->grade == 6 || in->grade == 5)) ||
                 (in->kind == 4 && dir > 0 && in->adj_kind == 2 && in->adj_grade == 2) ||
                 (in->kind == 1 && dir < 0 && in->grade == 4))) {
        dir = 0;
        oppose = 0;
        weak = 0;
    }
    if (in->kind == 5 && dir < 0 && in->rise_hi > 0 && in->rise_lo > 0 && in->fall_hi > 0 && in->fall_lo > 0 &&
        in->fall_hi >= in->rise_hi && in->fall_lo <= in->rise_lo) {
        dir = 0;
        oppose = 0;
        weak = 0;
    }
    if (in->kind == 5 && dir < 0 && bm >= 1440 && bm < 1860 && s->pnl_buy + s->pnl_sell > 0) {
        dir = 0;
        oppose = 0;
        weak = 0;
    }
    if (oppose && !weak && in->kind == 5) {
        dir = 0;
        oppose = 0;
    }
    if (oppose && !weak && in->kind == 1 && dir < 0 && in->grade == 8 && bm >= rm && bm < rm + 5) {
        dir = 0;
        oppose = 0;
    }
    if (s->v68) {
        int brk = in->kind == 2 || in->kind == 3 || (in->kind == 1 && (in->grade == 8 || in->grade == 7 || in->grade == 4));
        int comp = in->kind == 1 && in->grade != 8 && in->grade != 7 && in->grade != 6 && in->grade != 4;
        if (weak && dir > 0 && in->kind == 1 && (in->grade == 6 || in->grade == 8)) {
            dir = 0;
            oppose = 0;
            weak = 0;
        }
        if (in->kind == 1 && dir < 0 && in->grade == 7 && bm >= rm + 150 && bm < 1860) {
            dir = 0;
            oppose = 0;
        }
        if (s->pnl_buy + s->pnl_sell > 0.0 && in->lead_valid && in->prev_dn > 40 && in->kind == 4 && dir < 0 &&
            in->adj_kind == 2) {
            dir = 0;
            oppose = 0;
            weak = 0;
        }
        if (dir > 0 && in->di < 0 && ((in->kind == 1 && in->grade == 6) || in->kind == 5)) {
            dir = 0;
            oppose = 0;
            weak = 0;
        }
        if (dir < 0 && in->di > 0 && ((in->kind == 1 && in->grade == 6) || in->kind == 5)) {
            dir = 0;
            oppose = 0;
            weak = 0;
        }
        if (s->prev_dir > 0 && s->move_ticks >= 100 && s->done > 0 &&
            s->bar_n - s->exit_bar <= 60 && dir < 0 && (brk || in->kind == 4)) {
            dir = 0;
            oppose = 0;
            weak = 0;
        }
        if (s->session_n == 0 && bm >= 1440 + 105 && bm < 1860 &&
            ((dir > 0 && brk) || (dir < 0 && oppose && comp))) {
            dir = 0;
            oppose = 0;
            weak = 0;
        }
        /* 매도 수익 후 60봉, 압축이 늘고 통합카운트 >= 30 이면 압축매수 삭제. */
        if (dir > 0 && comp && s->prev_dir < 0 && s->prev_pnl > 0.0 && s->done > 0 &&
            s->bar_n - s->exit_bar <= 60 && in->sq_hold > s->prev_sq && s->uni_count >= 30) {
            dir = 0;
            oppose = 0;
            weak = 0;
        }
    }
    if (trade && dir > 0 && virt0 != 1) {
        int ticks = 15;
        if (bm >= rm + 150) {
            ticks = 10;
        }
        suf = "";
        out->suf = 0;
        if (weak) {
            suf = "_장약세";
            out->suf = 2;
        } else if (fail && in->kind == 4 && in->adj_grade == 1) {
            suf = "_직전실패";
            out->suf = 3;
        } else if (oppose) {
            suf = "_반대방향";
            out->suf = 1;
        }
        set_name(s, 1, in->kind, in->grade, in->adj_kind, in->adj_grade, suf);
        out->entry_dir = 1;
        out->entry_qty = entry_qty(weak, 1, in->kind, in->grade, in->adj_kind, in->adj_grade, fail, 0, 0, oppose, bm, rm);
        s->remaining_qty = out->entry_qty;
        out->entry_name = s->name;
        s->session_n += 1;
        s->last_dir = 1;
        s->virt = 1;
        s->entry_px = in->close;
        s->hi = in->close;
        s->lo = in->close;
        s->first_ticks = ticks;
        s->entry_bar = s->bar_n;
        s->limit_filled = 0;
        s->flat_after = s->done > 0 && s->bar_n - s->exit_bar <= 60;
        s->profit_flips = 0;
        s->prev_mtm = 0;
        out->limit_dir = 1;
        out->limit_px = in->close + (double)ticks * in->price_scale;
        out->limit_qty = 1;
        out->limit_name = s->v68 ? "1차청산매수" : "1차목표청산매수";
    } else if (trade && dir < 0 && virt0 != -1) {
        int ticks = 15;
        if (bm >= rm + 150) {
            ticks = 10;
        }
        suf = "";
        out->suf = 0;
        if (weak) {
            suf = "_장약세";
            out->suf = 2;
        } else if (yday && in->kind == 1 && (in->grade == 8 || in->grade == 3)) {
            suf = "_전일실패";
            out->suf = 4;
        } else if (prev_buy_fail && in->kind == 1 && in->grade == 5) {
            suf = "_직전매수실패";
            out->suf = 5;
        } else if (oppose) {
            suf = "_반대방향";
            out->suf = 1;
        }
        set_name(s, -1, in->kind, in->grade, in->adj_kind, in->adj_grade, suf);
        out->entry_dir = -1;
        out->entry_qty = entry_qty(weak, -1, in->kind, in->grade, in->adj_kind, in->adj_grade, 0, yday, prev_buy_fail, oppose, bm, rm);
        s->remaining_qty = out->entry_qty;
        out->entry_name = s->name;
        s->session_n += 1;
        s->last_dir = -1;
        s->virt = -1;
        s->entry_px = in->close;
        s->hi = in->close;
        s->lo = in->close;
        s->first_ticks = ticks;
        s->entry_bar = s->bar_n;
        s->limit_filled = 0;
        s->flat_after = s->done > 0 && s->bar_n - s->exit_bar <= 60;
        s->profit_flips = 0;
        s->prev_mtm = 0;
        out->limit_dir = -1;
        out->limit_px = in->close - (double)ticks * in->price_scale;
        out->limit_qty = 1;
        out->limit_name = s->v68 ? "1차청산매도" : "1차목표청산매도";
    } else if (s->virt != 0 && s->first_ticks > 0) {
        out->limit_dir = s->virt;
        out->limit_qty = 1;
        out->limit_px = s->virt > 0 ? s->entry_px + (double)s->first_ticks * in->price_scale
                                    : s->entry_px - (double)s->first_ticks * in->price_scale;
        out->limit_name = s->v68 ? (s->virt > 0 ? "1차청산매수" : "1차청산매도")
                                 : (s->virt > 0 ? "1차목표청산매수" : "1차목표청산매도");
    }
    if (s->v68 && s->virt != 0 && in->price_scale > 0.0) {
        double mtm = (in->close - s->entry_px) * (double)s->virt;
        if (s->bar_n > s->entry_bar && mtm > 0.0 && s->prev_mtm <= 0.0) {
            s->profit_flips += 1;
        }
        s->prev_mtm = mtm;
        if (s->flat_after && in->three_ratio >= 80.0 && s->bar_n - s->entry_bar >= 4 && s->first_ticks > 10) {
            s->first_ticks = 10;
        }
        if (s->profit_flips >= 3 && s->first_ticks > 10) {
            s->first_ticks = 10;
        }
        if (out->limit_dir != 0) {
            out->limit_px = s->virt > 0 ? s->entry_px + (double)s->first_ticks * in->price_scale
                                        : s->entry_px - (double)s->first_ticks * in->price_scale;
        }
    }
    s->last_color = in->color_dir;
    s->prev_sq = in->sq_hold;
}
