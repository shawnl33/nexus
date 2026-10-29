#include "core/paintbar/fx_judge_paint95.h"

#include "core/model/civil_time.h"

#include <string.h>

#define RGB_(r, g, b) ((uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))
#define GRAY210 RGB_(210, 210, 210)

static int dow_sun0(int yyyymmdd) {
    int month = (yyyymmdd / 100) % 100;
    int day = yyyymmdd % 100;
    return (int)tr_weekday_from_days(tr_days_from_civil(yyyymmdd / 10000, (unsigned)month, (unsigned)day));
}

static int us_summer(int yyyymmdd) {
    int month = (yyyymmdd / 100) % 100;
    int day = yyyymmdd % 100;
    int prev_sun = day - dow_sun0(yyyymmdd);
    if (month >= 4 && month <= 10) {
        return 1;
    }
    if (month == 3 && prev_sun >= 8) {
        return 1;
    }
    if (month == 11 && prev_sun < 1) {
        return 1;
    }
    return 0;
}

static int uk_summer(int yyyymmdd) {
    int month = (yyyymmdd / 100) % 100;
    int day = yyyymmdd % 100;
    int prev_sun = day - dow_sun0(yyyymmdd);
    if (month >= 4 && month <= 9) {
        return 1;
    }
    if (month == 3 && prev_sun >= 25) {
        return 1;
    }
    if (month == 10 && prev_sun < 25) {
        return 1;
    }
    return 0;
}

static int clock_min(int64_t hhmmss) {
    int t = (int)hhmmss;
    return (t / 10000) * 60 + ((t / 100) % 100);
}

static int rth_base(int yyyymmdd) {
    return us_summer(yyyymmdd) ? (22 * 60 + 30) : (23 * 60 + 30);
}

static double ago(const double *ring, int wr, int n, int k) {
    if (k < 1 || k > n || k > 16) {
        return 0.0;
    }
    return ring[(wr - k + 16) % 16];
}

static int count_side(const double *ring, int wr, int n, int want, double line, int above) {
    int k, hit = 0;
    int lim = n < 15 ? n : 15;
    for (k = 1; k <= lim && k <= want; k++) {
        double v = ago(ring, wr, n, k);
        if (above ? v > line : v < line) {
            hit++;
        }
    }
    return hit;
}

static double extreme_close(const tr_fxp95_rt *r, int n, int highest) {
    int k;
    int lim = r->n < n ? r->n : n;
    double x = 0.0;
    int any = 0;
    for (k = 1; k <= lim; k++) {
        double v = ago(r->cl, r->wr, r->n, k);
        if (!any || (highest ? v > x : v < x)) {
            x = v;
            any = 1;
        }
    }
    return any ? x : 0.0;
}

const char *tr_fxp95_order_name(int order_no) {
    if (order_no == 55) return "시가회복매수";
    if (order_no == 58) return "시가반락매도";
    if (order_no == 67) return "무거래_확장반등매수";
    if (order_no == 79) return "전일저이탈매도";
    if (order_no == 82) return "전일고돌파매수";
    if (order_no == 85) return "초기하락매도";
    if (order_no == 88) return "초기상승매수";
    if (order_no == 94) return "무거래30매수";
    return order_no > 0 ? "승률진입" : "";
}

uint32_t tr_fxp95_color(int group, int shadow, uint32_t judge_rgb) {
    uint32_t c = judge_rgb;
    if (group == 0) {
        return c;
    }
    if (shadow) {
        return RGB_(200, 200, 200);
    }
    if (group == 1) {
        return RGB_(255, 120, 0);
    }
    if (group == 2) {
        return RGB_(255, 0, 0);
    }
    if (group == 3) {
        return RGB_(220, 0, 140);
    }
    if (group == 4) {
        return RGB_(140, 0, 0);
    }
    if (group == -1) {
        return RGB_(0, 170, 255);
    }
    if (group == -2) {
        return RGB_(0, 0, 255);
    }
    if (group == -3) {
        return RGB_(0, 140, 140);
    }
    if (group == -4) {
        return RGB_(0, 0, 130);
    }
    return c;
}

/* 진입명번호 → 신호그룹. 이벤트표시는 V81 이후 그룹 번호를 바꾸지 않는다. */
static int signal_group(int name, int kind30) {
    int g = 0;
    (void)kind30;
    if (name == 9) {
        g = 1;
    } else if (name == 10) {
        g = -1;
    } else if (name == 11) {
        g = 2;
    } else if (name == 12) {
        g = -2;
    } else if (name == 7 || name == 15) {
        g = 3;
    } else if (name == 8 || name == 16) {
        g = -3;
    } else if (name == 1 || name == 3 || name == 5 || name == 13) {
        g = 4;
    } else if (name == 2 || name == 4 || name == 6 || name == 14) {
        g = -4;
    } else if (name == 21 || name == 25) {
        g = 4;
    } else if (name == 22) {
        g = -4;
    } else if (name == 23 || name == 33) {
        g = 2;
    } else if (name == 24 || name == 32) {
        g = -2;
    } else if (name == 34) {
        g = -3;
    } else if (name == 35 || name == 39) {
        g = 3;
    }
    return g;
}

static int name_of(int order_no) {
    if (order_no == 55) {
        return 21;
    }
    if (order_no == 58) {
        return 22;
    }
    if (order_no == 61) {
        return 23;
    }
    if (order_no == 64) {
        return 24;
    }
    if (order_no == 67) {
        return 25;
    }
    if (order_no == 79) {
        return 32;
    }
    if (order_no == 82) {
        return 33;
    }
    if (order_no == 85) {
        return 34;
    }
    if (order_no == 88) {
        return 35;
    }
    if (order_no == 94) {
        return 39;
    }
    return 0;
}

static void rotate_amp(tr_fxp95_rt *r) {
    int k;
    if (!(r->amp_keep && r->amp_last >= 0)) {
        return;
    }
    for (k = 0; k < TR_FXP95_MIN; k++) {
        if (k > r->amp_last) {
            r->amp_today[k] = r->amp_today[r->amp_last];
            r->vol_today[k] = r->vol_today[r->amp_last];
        }
        r->amp_h[4][k] = r->amp_h[3][k];
        r->amp_h[3][k] = r->amp_h[2][k];
        r->amp_h[2][k] = r->amp_h[1][k];
        r->amp_h[1][k] = r->amp_h[0][k];
        r->amp_h[0][k] = r->amp_today[k];
        r->vol_h[4][k] = r->vol_h[3][k];
        r->vol_h[3][k] = r->vol_h[2][k];
        r->vol_h[2][k] = r->vol_h[1][k];
        r->vol_h[1][k] = r->vol_h[0][k];
        r->vol_h[0][k] = r->vol_today[k];
    }
    if (r->amp_n < 5) {
        r->amp_n++;
    }
}

static void rates(const tr_fxp95_rt *r, int minute, double range, double vol, double *amp_pct, double *vol_pct) {
    int i;
    double as = 0.0, vs = 0.0;
    *amp_pct = 0.0;
    *vol_pct = 0.0;
    if (minute < 0 || minute >= TR_FXP95_MIN || r->amp_n < 1) {
        return;
    }
    for (i = 0; i < r->amp_n; i++) {
        as += r->amp_h[i][minute];
        vs += r->vol_h[i][minute];
    }
    as /= (double)r->amp_n;
    vs /= (double)r->amp_n;
    if (as > 0.0) {
        *amp_pct = range / as * 100.0;
    }
    if (vs > 0.0) {
        *vol_pct = vol / vs * 100.0;
    }
}

static void take(tr_fxp95_t *s, tr_fxp95_rt *r, int order_no, int kind30, int dir) {
    int group = signal_group(name_of(order_no), kind30);
    s->group = group;
    s->shadow = 0;
    s->order_no = order_no;
    s->dir = dir;
    s->rgb = tr_fxp95_color(group, 0, 0);
    r->pos_dir = dir;
    if (order_no == 55) {
        r->n_recov = 1;
    }
    if (order_no == 58) {
        r->n_fade = 1;
    }
    if (order_no == 61 || order_no == 64) {
        r->n_absorb = 1;
    }
    if (order_no == 67) {
        r->n_ext = 1;
        r->ext_wait = 0;
    }
    if (order_no == 79) {
        r->n_ylo = 1;
    }
    if (order_no == 82) {
        r->n_yhi = 1;
    }
    if (order_no == 85) {
        r->n_edn = 1;
    }
    if (order_no == 88) {
        r->n_eup = 1;
    }
    if (order_no == 94) {
        r->n_q30 = 1;
    }
    r->real_n++;
}

void tr_fxp95_bar(tr_fxp95_t *s, const tr_fxp95_in_t *in) {
    tr_fxp95_rt *r;
    int base, bmin, raw_min, rth, dir, gray, color_dir, elapsed, code, kind30;
    int london0;
    double amp_pct, vol_pct, range;
    if (s == 0 || in == 0) {
        return;
    }
    if (in->is_new_bar || !s->has_open) {
        s->open = s->cur;
        s->has_open = 1;
    } else {
        s->cur = s->open;
    }
    r = &s->cur;
    s->group = 0;
    s->shadow = 0;
    s->order_no = 0;
    s->dir = 0;
    s->rgb = 0;
    if (in->session_reset) {
        r->uni = 0;
        r->uni_side = 0;
        r->prev_uni = 0;
        r->n_recov = r->n_fade = r->n_absorb = r->n_ext = 0;
        r->ext_wait = 0;
        r->n_ylo = r->n_yhi = r->n_edn = r->n_eup = r->n_q30 = 0;
        r->real_n = 0;
        r->pos_dir = 0;
    }
    base = rth_base((int)in->date);
    raw_min = clock_min(in->time);
    bmin = raw_min < 720 ? raw_min + 1440 : raw_min;
    rth = bmin >= base && bmin < base + TR_FXP95_MIN;
    elapsed = bmin - base;
    dir = in->judge > 0 ? 1 : (in->judge < 0 ? -1 : 0);
    gray = dir != 0 && (in->fut != dir || in->di != dir || in->state5 * dir < 0);
    color_dir = gray ? 0 : dir;
    if (dir != 0 && dir != r->uni_side) {
        r->prev_uni = r->uni;
        r->uni = 0;
        r->uni_side = dir;
    }
    if (color_dir != 0) {
        r->uni += color_dir;
    }
    if (rth && !r->prev_rth) {
        if (r->day_hi > 0.0) {
            rotate_amp(r);
            r->prev2_hi = r->prev_hi;
            r->prev2_lo = r->prev_lo;
            r->prev_hi = r->day_hi;
            r->prev_lo = r->day_lo;
        }
        memset(r->amp_today, 0, sizeof(r->amp_today));
        memset(r->vol_today, 0, sizeof(r->vol_today));
        r->amp_keep = r->seen && !r->prev_rth;
        r->amp_last = -1;
        r->run_hi = in->high;
        r->run_lo = in->low;
        r->run_vol = 0.0;
        r->sess_open = in->open;
        r->day_hi = in->high;
        r->day_lo = in->low;
    } else if (rth) {
        if (in->high > r->day_hi) {
            r->day_hi = in->high;
        }
        if (r->day_lo <= 0.0 || in->low < r->day_lo) {
            r->day_lo = in->low;
        }
    }
    amp_pct = 0.0;
    vol_pct = 0.0;
    if (rth) {
        int minute = elapsed;
        if (r->amp_last >= 0 && minute > r->amp_last + 1) {
            int k;
            for (k = r->amp_last + 1; k < minute && k < TR_FXP95_MIN; k++) {
                r->amp_today[k] = r->amp_today[r->amp_last];
                r->vol_today[k] = r->vol_today[r->amp_last];
            }
        }
        if (in->high > r->run_hi) {
            r->run_hi = in->high;
        }
        if (r->run_lo <= 0.0 || in->low < r->run_lo) {
            r->run_lo = in->low;
        }
        r->run_vol += in->volume;
        range = r->run_hi - r->run_lo;
        if (minute >= 0 && minute < TR_FXP95_MIN) {
            r->amp_today[minute] = range;
            r->vol_today[minute] = r->run_vol;
            r->amp_last = minute;
        }
        rates(r, minute, range, r->run_vol, &amp_pct, &vol_pct);
    }
    {
        if (rth && vol_pct > 110.0 && amp_pct > 0.0 && amp_pct < 80.0) {
            r->absorb++;
        } else {
            r->absorb = 0;
        }
        london0 = uk_summer((int)in->date) ? 16 * 60 : 17 * 60;
        if (r->seen && raw_min >= london0 && raw_min < london0 + 60 &&
            (r->prev_bmin < london0 || in->date != r->prev_date)) {
            r->london = in->open;
        }
        code = 0;
        kind30 = 0;
        if (r->pos_dir != 0 && color_dir == -r->pos_dir) {
            r->pos_dir = 0;
        }
        if (code == 0 && r->n_recov == 0 && r->pos_dir != 1 && rth && r->sess_open > 0.0 && elapsed >= 60 &&
            in->close > r->sess_open && extreme_close(r, 5, 0) > 0.0 && extreme_close(r, 5, 0) <= r->sess_open &&
            in->pos >= 1 && in->pos < 12 && in->slope >= 1 && in->slope < 12 &&
            r->uni >= 1 && r->uni <= 9 && in->judge > 0 && !gray &&
            in->swing_ratio < 100.0 && amp_pct > 0.0 && amp_pct < 80.0 && vol_pct > 100.0 &&
            r->prev_uni <= -5 && (in->target5 <= 0.0 || in->close >= in->target5) &&
            count_side(r->hi, r->wr, r->n, 15, r->sess_open, 0) >= 3) {
            code = 55;
        }
        if (rth && elapsed == 30) {
            r->ext_wait = (amp_pct >= 100.0 && vol_pct >= 100.0 && r->sess_open > 0.0 &&
                           in->close < r->sess_open && r->real_n == 0) ? 1 : 0;
        }
        if (!rth || elapsed >= 60 || r->real_n > 0) {
            r->ext_wait = 0;
        }
        if (code == 0 && r->ext_wait && r->n_ext == 0 && r->pos_dir != 1 && elapsed >= 30 && elapsed < 60 &&
            in->pos >= 1 && in->pos < 12 && in->pos_prev <= 0) {
            int ban = 0;
            if (r->london > 0.0 && r->sess_open > 0.0 && r->london > r->sess_open && in->high < r->sess_open &&
                in->judge < 0 && !gray) {
                ban = 1;
            }
            if (r->prev_lo > 0.0 && in->close < r->prev_lo) {
                ban = 1;
            }
            if (in->red_struct && in->up_lo > 0.0 && in->high < in->up_lo && in->judge < 0 && !gray) {
                ban = 1;
            }
            if (r->london > 0.0 && in->close < r->london) {
                ban = 1;
            }
            if (!ban) {
                code = 67;
            }
        }
        if (code == 0 && r->n_ylo == 0 && r->pos_dir != -1 && rth &&
            r->prev_hi > 0.0 && r->prev_lo > 0.0 && r->prev2_hi > 0.0 && r->prev2_lo > 0.0 &&
            (r->prev_hi - r->prev_lo) <= (r->prev2_hi - r->prev2_lo) * 0.7 &&
            in->close < r->prev_lo && ago(r->cl, r->wr, r->n, 1) >= r->prev_lo &&
            in->pos > -10 && in->pos <= 0 && amp_pct > 0.0 && amp_pct <= 100.0 && in->judge == -100) {
            code = 79;
        }
        if (code == 0 && r->n_yhi == 0 && r->pos_dir != 1 && rth && elapsed < 60 && r->real_n == 0 &&
            r->prev_hi > 0.0 && r->prev_lo > 0.0 && r->prev2_hi > 0.0 && r->prev2_lo > 0.0 &&
            (r->prev_hi - r->prev_lo) <= (r->prev2_hi - r->prev2_lo) * 0.7 &&
            in->close > r->prev_hi && ago(r->cl, r->wr, r->n, 1) <= r->prev_hi &&
            in->pos < 10 && in->pos >= 0 && amp_pct > 0.0 && amp_pct <= 100.0 && in->judge == 100) {
            code = 82;
        }
        if (code == 0 && r->n_edn == 0 && r->pos_dir != -1 && rth &&
            in->pos < 0 && in->pos > -6 && in->slope < 0 && in->slope > -6 &&
            in->judge < 0 && !gray && amp_pct > 0.0 && amp_pct < 90.0 &&
            r->sess_open > 0.0 && in->close < r->sess_open && elapsed >= 30 &&
            (elapsed < 90 || elapsed >= 120) && in->swing_ratio < 70.0 && r->uni > -10) {
            code = 85;
        }
        if (code == 0 && r->n_eup == 0 && r->pos_dir != 1 && rth &&
            in->pos > 0 && in->pos < 6 && in->slope > 0 && in->slope < 6 &&
            in->judge > 0 && !gray && amp_pct > 0.0 && amp_pct < 90.0 &&
            r->sess_open > 0.0 && in->close > r->sess_open && elapsed >= 30 &&
            (elapsed < 90 || elapsed >= 120) &&
            (r->real_n == 0 || elapsed < 60 || elapsed >= 90) &&
            in->swing_ratio < 70.0 && r->uni < 10) {
            code = 88;
        }
        if (rth && elapsed == 30 && r->sess_open > 0.0 && amp_pct > 0.0 && vol_pct > 0.0) {
            if (vol_pct >= 100.0 && amp_pct >= 100.0 && in->close < r->sess_open && us_summer((int)in->date) &&
                in->judge != -100 && (r->prev_hi <= 0.0 || in->close <= r->prev_hi)) {
                kind30 = 2;
            }
            if (vol_pct >= 100.0 && amp_pct < 100.0 && in->close >= r->sess_open &&
                (r->prev_lo <= 0.0 || in->close >= r->prev_lo) &&
                (r->london <= 0.0 || in->close >= r->london) &&
                (in->swing_ratio <= 90.0) &&
                (r->prev_hi <= 0.0 || in->target5 <= r->prev_hi || in->close >= r->prev_hi)) {
                kind30 = 3;
            }
        }
        if (code == 0 && kind30 > 0 && r->n_q30 == 0 && r->pos_dir != 1 && r->real_n == 0) {
            code = 94;
        }
        if (code == 0 && r->n_fade == 0 && r->pos_dir != -1 && rth && r->sess_open > 0.0 && elapsed >= 60 &&
            in->close < r->sess_open && extreme_close(r, 5, 1) >= r->sess_open &&
            in->pos <= -1 && in->pos > -12 && in->slope <= -1 && in->slope > -12 &&
            r->uni <= -1 && r->uni >= -9 && in->judge < 0 && !gray &&
            in->swing_ratio < 100.0 && amp_pct > 0.0 && amp_pct < 80.0 && vol_pct > 100.0 &&
            r->prev_uni >= 5 && (in->target5 <= 0.0 || in->close <= in->target5) &&
            count_side(r->lo, r->wr, r->n, 15, r->sess_open, 1) >= 3 &&
            (r->prev_hi <= 0.0 || in->low <= r->prev_hi)) {
            code = 58;
        }
        if (code != 0) {
            take(s, r, code, kind30, code == 55 || code == 67 || code == 82 || code == 88 || code == 94 ? 1 : -1);
        }
    }
    if (r->n < 16) {
        r->hi[r->wr] = in->high;
        r->lo[r->wr] = in->low;
        r->cl[r->wr] = in->close;
        r->wr = (r->wr + 1) % 16;
        r->n++;
    } else {
        r->hi[r->wr] = in->high;
        r->lo[r->wr] = in->low;
        r->cl[r->wr] = in->close;
        r->wr = (r->wr + 1) % 16;
    }
    r->prev_rth = rth;
    r->prev_date = (int)in->date;
    r->prev_bmin = raw_min;
    r->seen = 1;
}
