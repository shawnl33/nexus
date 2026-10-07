#include "core/functions/fx_swing_v1.h"

#include <math.h>
#include <string.h>

/* 상승 대기 확정 (원본 198~248줄): 지난하락 메모리 확정 + 지난상승 유지/붕괴/확장
 * + 지난하락 구조약화. 이전 구간이 하락(leg_dir==-1)일 때만 메모리를 갱신한다.
 * 스윙연결=1이면 새 상승 저점을 직전 하락 저점과 잇는다 (WSF_FXSwingV2 247~249줄).
 * 방향은 그 비교 뒤에 덮어쓴다. */
static void confirm_up(tr_fxsw_state_t *t, int swing_link) {
    if (t->leg_dir == -1) {
        t->ldn.high = t->dn_high;
        t->ldn.low = t->dn_low;
        t->ldn.size = t->ldn.high - t->ldn.low;
        t->ldn.lvl_382 = t->ldn.low + t->ldn.size * 0.382;
        t->ldn.lvl_500 = t->ldn.low + t->ldn.size * 0.500;
        t->ldn.lvl_618 = t->ldn.low + t->ldn.size * 0.618;
        t->ldn.count = t->dn_count;

        /* 지금 끝난 하락 조정이 직전 상승의 38.2/50/61.8%를 각각 훼손했는지 확인 */
        t->lup.keep_382 = 0;
        t->lup.keep_500 = 0;
        t->lup.keep_618 = 0;
        t->lup.broken = 0;
        t->lup.ext_100 = 0.0;
        t->lup.ext_1382 = 0.0;
        t->lup.ext_1618 = 0.0;
        t->lup.ext_200 = 0.0;
        t->lup.ext_2382 = 0.0;
        if (t->lup.count > 0.0) {
            t->lup.ext_100 = t->lup.low + t->lup.size * 1.000;
            t->lup.ext_1382 = t->lup.low + t->lup.size * 1.382;
            t->lup.ext_1618 = t->lup.low + t->lup.size * 1.618;
            t->lup.ext_200 = t->lup.low + t->lup.size * 2.000;
            t->lup.ext_2382 = t->lup.low + t->lup.size * 2.382;

            if (t->ldn.low > t->lup.lvl_618) {
                t->lup.keep_618 = 1;
            }
            if (t->ldn.low > t->lup.lvl_500) {
                t->lup.keep_500 = 1;
            }
            if (t->ldn.low > t->lup.lvl_382) {
                t->lup.keep_382 = 1;
            }
            /* 붕괴: 되돌림이 기준 상승의 시작가(직전 저가, 100% 되돌림선)까지 뚫림 */
            if (t->ldn.low < t->lup.low) {
                t->lup.broken = 1;
            }
        }

        /* 지난하락 구조약화: 되돌림이 직전 상승의 50%보다 깊게 파고들었다면(500유지 실패) */
        t->ldn.weak = (t->lup.keep_500 == 0) ? 1 : 0;
    }
    t->up_high = t->wait_high;
    t->up_low = t->wait_low;
    if (swing_link == 1 && t->leg_dir == -1) {
        t->up_low = fmin(t->wait_low, t->dn_low);
    }
    t->up_count = t->wait_count;
    t->leg_dir = 1;
}

/* 하락 대기 확정 (원본 249~299줄): 지난상승 메모리 확정 + 지난하락 유지/붕괴/확장
 * + 지난상승 구조약화. 이전 구간이 상승(leg_dir==1)일 때만 메모리를 갱신한다.
 * 스윙연결=1이면 새 하락 고점을 직전 상승 고점과 잇는다 (WSF_FXSwingV2 300~302줄). */
static void confirm_dn(tr_fxsw_state_t *t, int swing_link) {
    if (t->leg_dir == 1) {
        t->lup.high = t->up_high;
        t->lup.low = t->up_low;
        t->lup.size = t->lup.high - t->lup.low;
        t->lup.lvl_382 = t->lup.low + t->lup.size * 0.382;
        t->lup.lvl_500 = t->lup.low + t->lup.size * 0.500;
        t->lup.lvl_618 = t->lup.low + t->lup.size * 0.618;
        t->lup.count = t->up_count;

        /* 지금 끝난 상승 반등이 직전 하락의 38.2/50/61.8%를 각각 넘었는지(훼손) 확인 */
        t->ldn.keep_382 = 0;
        t->ldn.keep_500 = 0;
        t->ldn.keep_618 = 0;
        t->ldn.broken = 0;
        t->ldn.ext_100 = 0.0;
        t->ldn.ext_1382 = 0.0;
        t->ldn.ext_1618 = 0.0;
        t->ldn.ext_200 = 0.0;
        t->ldn.ext_2382 = 0.0;
        if (t->ldn.count > 0.0) {
            t->ldn.ext_100 = t->ldn.high - t->ldn.size * 1.000;
            t->ldn.ext_1382 = t->ldn.high - t->ldn.size * 1.382;
            t->ldn.ext_1618 = t->ldn.high - t->ldn.size * 1.618;
            t->ldn.ext_200 = t->ldn.high - t->ldn.size * 2.000;
            t->ldn.ext_2382 = t->ldn.high - t->ldn.size * 2.382;

            if (t->lup.high < t->ldn.lvl_618) {
                t->ldn.keep_618 = 1;
            }
            if (t->lup.high < t->ldn.lvl_500) {
                t->ldn.keep_500 = 1;
            }
            if (t->lup.high < t->ldn.lvl_382) {
                t->ldn.keep_382 = 1;
            }
            /* 붕괴: 반등이 기준 하락의 시작가(직전 고점, 100% 되돌림선)까지 뚫림 */
            if (t->lup.high > t->ldn.high) {
                t->ldn.broken = 1;
            }
        }

        /* 지난상승 구조약화: 반등이 직전 하락의 50%도 못 넘어섰다면(500유지) 약한 반등 */
        t->lup.weak = t->ldn.keep_500;
    }
    t->dn_high = t->wait_high;
    if (swing_link == 1 && t->leg_dir == 1) {
        t->dn_high = fmax(t->wait_high, t->up_high);
    }
    t->dn_low = t->wait_low;
    t->dn_count = t->wait_count;
    t->leg_dir = -1;
}

/* 실효등급 (원본 410~422줄). 지난상승은 618유지가, 지난하락은 382유지가 가장 엄격한
 * 조건이라 (레벨 정의 방향이 반대) 각 체인의 검사 순서도 그에 따른다 */
static int grade_up(const tr_fxsw_leg_t *l) {
    if (l->high == 0.0) {
        return -1;
    }
    if (l->broken == 1) {
        return -2;
    }
    if (l->keep_618 == 1) {
        return 0;
    }
    if (l->keep_382 == 1) {
        return 1;
    }
    return l->weak == 1 ? 1 : 2;
}

static int grade_dn(const tr_fxsw_leg_t *l) {
    if (l->high == 0.0) {
        return -1;
    }
    if (l->broken == 1) {
        return -2;
    }
    if (l->keep_382 == 1) {
        return 0;
    }
    if (l->keep_618 == 1) {
        return 1;
    }
    return l->weak == 1 ? 1 : 2;
}

bool tr_fxsw_init(tr_fxsw_t *s) {
    if (s == 0) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    return true;
}

void tr_fxsw_eval(tr_fxsw_t *s, const tr_fxsw_input_t *in) {
    if (s == 0 || in == 0) {
        return;
    }

    /* 동일 봉 재평가 복원 (원본 101~155줄): 모든 상태를 직전 봉 말 값으로 복원해
     * 카운터 중복 누적을 막는다. 새 봉 첫 평가는 직전 봉 말 상태로 스냅샷을 갱신한다. */
    if (in->is_new_bar || !s->has_bar) {
        s->prev_bar = s->st;
    } else {
        s->st = s->prev_bar;
    }
    s->has_bar = true;

    tr_fxsw_state_t *t = &s->st;
    if (in->session_reset) {
        memset(t, 0, sizeof(*t)); /* 원본 156~182줄 — 리셋 봉은 상태 기계를 진행하지 않는다 */
    } else {
        /* 전환 감지 (원본 185~186줄) */
        int pos_cross = (t->prev_dir <= 0.0 && in->future_dir > 0.0) ? 1 : 0;
        int neg_cross = (t->prev_dir >= 0.0 && in->future_dir < 0.0) ? 1 : 0;

        if (t->wait_dir != 0) {
            if ((t->wait_dir == 1 && in->future_dir > 0.0) ||
                (t->wait_dir == -1 && in->future_dir < 0.0)) {
                /* 대기 유지 (원본 190~195줄) */
                t->wait_count += 1.0;
                t->wait_high = fmax(t->wait_high, in->high);
                t->wait_low = fmin(t->wait_low, in->low);
                if (t->wait_count >= (double)in->min_hold_bars) {
                    if (t->wait_dir == 1) {
                        confirm_up(t, in->swing_link);
                    } else {
                        confirm_dn(t, in->swing_link);
                    }
                    t->wait_dir = 0;
                    t->wait_count = 0.0;
                    t->wait_high = 0.0;
                    t->wait_low = 0.0;
                }
            } else if ((t->wait_dir == 1 && in->future_dir < 0.0) ||
                       (t->wait_dir == -1 && in->future_dir > 0.0)) {
                /* 대기 취소: 현재 구간에 흡수 (원본 304~320줄) */
                if (t->leg_dir == 1) {
                    t->up_high = fmax(t->up_high, fmax(t->wait_high, in->high));
                    t->up_low = fmin(t->up_low, fmin(t->wait_low, in->low));
                    t->up_count += t->wait_count + 1.0;
                } else if (t->leg_dir == -1) {
                    t->dn_high = fmax(t->dn_high, fmax(t->wait_high, in->high));
                    t->dn_low = fmin(t->dn_low, fmin(t->wait_low, in->low));
                    t->dn_count += t->wait_count + 1.0;
                }
                t->wait_dir = 0;
                t->wait_count = 0.0;
                t->wait_high = 0.0;
                t->wait_low = 0.0;
            }
            /* 방향 0이면 대기는 동결된다 (아무 분기도 타지 않음) */
        } else {
            if (pos_cross == 1 && t->leg_dir != 1) {
                t->wait_dir = 1;
                t->wait_count = 1.0;
                t->wait_high = in->high;
                t->wait_low = in->low;
            } else if (neg_cross == 1 && t->leg_dir != -1) {
                t->wait_dir = -1;
                t->wait_count = 1.0;
                t->wait_high = in->high;
                t->wait_low = in->low;
            } else if (t->leg_dir == 1 && in->future_dir > 0.0) {
                /* 상승 구간 연장 (원본 338~343줄) */
                t->up_high = fmax(t->up_high, in->high);
                t->up_low = fmin(t->up_low, in->low);
                t->up_count += 1.0;
            } else if (t->leg_dir == -1 && in->future_dir < 0.0) {
                /* 하락 구간 연장 (원본 344~349줄) */
                t->dn_high = fmax(t->dn_high, in->high);
                t->dn_low = fmin(t->dn_low, in->low);
                t->dn_count += 1.0;
            }
        }

        /* 시간비율 (원본 352~357줄): 되돌리고 있는 반대 구간 기준 */
        t->time_ratio = 0.0;
        if (t->leg_dir == -1 && t->lup.count > 0.0) {
            t->time_ratio = t->dn_count / t->lup.count;
        }
        if (t->leg_dir == 1 && t->ldn.count > 0.0) {
            t->time_ratio = t->up_count / t->ldn.count;
        }

        t->prev_dir = in->future_dir;
    }

    /* 출력 (원본 362~430줄): 상태 복사 + 실효등급 계산 — 리셋 봉에도 실행된다 */
    s->out_lup = t->lup;
    s->out_ldn = t->ldn;
    s->out_lup_grade = grade_up(&t->lup);
    s->out_ldn_grade = grade_dn(&t->ldn);
    s->out_leg_dir = t->leg_dir;
    s->out_time_ratio = t->time_ratio;
}
