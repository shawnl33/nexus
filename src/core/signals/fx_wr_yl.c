#include "core/signals/fx_wr_yl.h"
#include <math.h>
void tr_fxwr_yl_body(tr_fxwr_t *s, const tr_fxwr_bar_t *b) {
    s->cur[YL_F_FX세션키] = yl_WSF_FXSessionKeyV1(s, b);
    s->cur[YL_F_봉길이] = (b->high - b->low);
    s->cur[YL_F_봉길이평균] = yl_avg(s, "봉길이", s->cur[YL_F_봉길이평균봉수]);
    if ((s->cur[YL_F_본장기준봉분] == s->cur[YL_F_본장시작분])) 
        s->cur[YL_F_개장최대배율] = 0;
    if ((((s->cur[YL_F_본장기준봉분] >= s->cur[YL_F_본장시작분]) && (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_개장장대표시분]))) && (yl_lag(s, YL_F_봉길이평균, 1) > 0))) 
        s->cur[YL_F_개장최대배율] = yl_max(s->cur[YL_F_개장최대배율], (s->cur[YL_F_봉길이] / yl_lag(s, YL_F_봉길이평균, 1)));
    s->cur[YL_F_FX초기화] = 0;
    if ((s->bar_n == 1)) 
        s->cur[YL_F_FX초기화] = 1;
    else if ((s->cur[YL_F_FX세션키] != yl_lag(s, YL_F_FX세션키, 1))) 
        s->cur[YL_F_FX초기화] = 1;
    if ((s->cur[YL_F_FX초기화] == 1)) 
        s->cur[YL_F_FX세션봉수] = 1;
    else 
        s->cur[YL_F_FX세션봉수] = (yl_lag(s, YL_F_FX세션봉수, 1) + 1);
    s->cur[YL_F_봉월] = (trunc((b->date / 100)) - (trunc((b->date / 10000)) * 100));
    s->cur[YL_F_봉일] = (b->date - (trunc((b->date / 100)) * 100));
    s->cur[YL_F_직전일요일] = (s->cur[YL_F_봉일] - yl_dow(b->date));
    s->cur[YL_F_서머타임] = 0;
    if (((s->cur[YL_F_봉월] >= 4) && (s->cur[YL_F_봉월] <= 10))) 
        s->cur[YL_F_서머타임] = 1;
    if (((s->cur[YL_F_봉월] == 3) && (s->cur[YL_F_직전일요일] >= 8))) 
        s->cur[YL_F_서머타임] = 1;
    if (((s->cur[YL_F_봉월] == 11) && (s->cur[YL_F_직전일요일] < 1))) 
        s->cur[YL_F_서머타임] = 1;
    s->cur[YL_F_적용본장시작] = s->cur[YL_F_본장시작시각];
    if ((s->cur[YL_F_서머타임자동] == 1)) 
        s->cur[YL_F_적용본장시작] = ((s->cur[YL_F_서머타임] == 1) ? s->cur[YL_F_서머타임본장] : s->cur[YL_F_표준시본장]);
    s->cur[YL_F_본장시작분] = ((trunc((s->cur[YL_F_적용본장시작] / 10000)) * 60) + trunc((((s->cur[YL_F_적용본장시작] - (trunc((s->cur[YL_F_적용본장시작] / 10000)) * 10000))) / 100)));
    s->cur[YL_F_봉시작분] = ((trunc((b->time / 10000)) * 60) + trunc((((b->time - (trunc((b->time / 10000)) * 10000))) / 100)));
    s->cur[YL_F_본장기준봉분] = s->cur[YL_F_봉시작분];
    if ((s->cur[YL_F_봉시작분] < 720)) 
        s->cur[YL_F_본장기준봉분] = (s->cur[YL_F_봉시작분] + 1440);
    s->cur[YL_F_본장중] = 0;
    if (((s->cur[YL_F_본장기준봉분] >= s->cur[YL_F_본장시작분]) && (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_본장분수])))) 
        s->cur[YL_F_본장중] = 1;
    if (((s->cur[YL_F_본장중] == 1) && (((s->bar_n == 1) || (yl_lag(s, YL_F_본장중, 1) == 0)))))         {
            if ((s->cur[YL_F_당일본장고가] > 0))                 {
                    s->cur[YL_F_전전일본장고가] = s->cur[YL_F_전일본장고가];
                    s->cur[YL_F_전전일본장저가] = s->cur[YL_F_전일본장저가];
                    s->cur[YL_F_전일본장고가] = s->cur[YL_F_당일본장고가];
                    s->cur[YL_F_전일본장저가] = s->cur[YL_F_당일본장저가];
                    s->cur[YL_F_전일본장종가] = s->cur[YL_F_진행본장종가];
                }
            s->cur[YL_F_본장시가] = b->open;
            s->cur[YL_F_본장수익있음] = 0;
            s->cur[YL_F_당일본장고가] = b->high;
            s->cur[YL_F_당일본장저가] = b->low;
        }
    else if ((s->cur[YL_F_본장중] == 1))         {
            s->cur[YL_F_당일본장고가] = yl_max(s->cur[YL_F_당일본장고가], b->high);
            s->cur[YL_F_당일본장저가] = yl_min(s->cur[YL_F_당일본장저가], b->low);
        }
    if ((s->cur[YL_F_본장중] == 1)) 
        s->cur[YL_F_진행본장종가] = b->close;
    if (((s->cur[YL_F_본장중] == 1) && (((s->bar_n == 1) || (yl_lag(s, YL_F_본장중, 1) == 0)))))         {
            if (((s->cur[YL_F_회복본장완전] == 1) && (s->cur[YL_F_회복마지막분] >= 0)))                 {
                    for (s->cur[YL_F_회복k] = (0); s->cur[YL_F_회복k] <= ((s->cur[YL_F_본장분수] - 1)); s->cur[YL_F_회복k] += 1)                         {
                            if ((s->cur[YL_F_회복k] > s->cur[YL_F_회복마지막분]))                                 {
                                    yl_arr_set(s, YL_A_회복진폭오늘, (int)(s->cur[YL_F_회복k]), yl_arr(s, YL_A_회복진폭오늘, (int)(s->cur[YL_F_회복마지막분])));
                                    yl_arr_set(s, YL_A_회복량오늘, (int)(s->cur[YL_F_회복k]), yl_arr(s, YL_A_회복량오늘, (int)(s->cur[YL_F_회복마지막분])));
                                }
                            yl_arr_set(s, YL_A_회복진폭5, (int)(s->cur[YL_F_회복k]), yl_arr(s, YL_A_회복진폭4, (int)(s->cur[YL_F_회복k])));
                            yl_arr_set(s, YL_A_회복진폭4, (int)(s->cur[YL_F_회복k]), yl_arr(s, YL_A_회복진폭3, (int)(s->cur[YL_F_회복k])));
                            yl_arr_set(s, YL_A_회복진폭3, (int)(s->cur[YL_F_회복k]), yl_arr(s, YL_A_회복진폭2, (int)(s->cur[YL_F_회복k])));
                            yl_arr_set(s, YL_A_회복진폭2, (int)(s->cur[YL_F_회복k]), yl_arr(s, YL_A_회복진폭1, (int)(s->cur[YL_F_회복k])));
                            yl_arr_set(s, YL_A_회복진폭1, (int)(s->cur[YL_F_회복k]), yl_arr(s, YL_A_회복진폭오늘, (int)(s->cur[YL_F_회복k])));
                            yl_arr_set(s, YL_A_회복량5, (int)(s->cur[YL_F_회복k]), yl_arr(s, YL_A_회복량4, (int)(s->cur[YL_F_회복k])));
                            yl_arr_set(s, YL_A_회복량4, (int)(s->cur[YL_F_회복k]), yl_arr(s, YL_A_회복량3, (int)(s->cur[YL_F_회복k])));
                            yl_arr_set(s, YL_A_회복량3, (int)(s->cur[YL_F_회복k]), yl_arr(s, YL_A_회복량2, (int)(s->cur[YL_F_회복k])));
                            yl_arr_set(s, YL_A_회복량2, (int)(s->cur[YL_F_회복k]), yl_arr(s, YL_A_회복량1, (int)(s->cur[YL_F_회복k])));
                            yl_arr_set(s, YL_A_회복량1, (int)(s->cur[YL_F_회복k]), yl_arr(s, YL_A_회복량오늘, (int)(s->cur[YL_F_회복k])));
                        }
                    s->cur[YL_F_회복개수] = yl_min(5, (s->cur[YL_F_회복개수] + 1));
                }
            for (s->cur[YL_F_회복k] = (0); s->cur[YL_F_회복k] <= ((s->cur[YL_F_본장분수] - 1)); s->cur[YL_F_회복k] += 1)                 {
                    yl_arr_set(s, YL_A_회복진폭오늘, (int)(s->cur[YL_F_회복k]), 0);
                    yl_arr_set(s, YL_A_회복량오늘, (int)(s->cur[YL_F_회복k]), 0);
                }
            s->cur[YL_F_회복본장완전] = (((s->bar_n > 1) && (yl_lag(s, YL_F_본장중, 1) == 0)) ? 1 : 0);
            s->cur[YL_F_회복마지막분] = (-1);
            s->cur[YL_F_회복고가] = b->high;
            s->cur[YL_F_회복저가] = b->low;
            s->cur[YL_F_회복누적량] = 0;
        }
    s->cur[YL_F_본장진폭율] = 0;
    s->cur[YL_F_본장거래량율] = 0;
    if ((s->cur[YL_F_본장중] == 1))         {
            s->cur[YL_F_회복분] = (s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]);
            s->cur[YL_F_회복고가] = yl_max(s->cur[YL_F_회복고가], b->high);
            s->cur[YL_F_회복저가] = yl_min(s->cur[YL_F_회복저가], b->low);
            s->cur[YL_F_회복누적량] = (s->cur[YL_F_회복누적량] + b->volume);
            if (((s->cur[YL_F_회복마지막분] >= 0) && (s->cur[YL_F_회복분] > (s->cur[YL_F_회복마지막분] + 1))))                 {
                    for (s->cur[YL_F_회복k] = ((s->cur[YL_F_회복마지막분] + 1)); s->cur[YL_F_회복k] <= ((s->cur[YL_F_회복분] - 1)); s->cur[YL_F_회복k] += 1)                         {
                            yl_arr_set(s, YL_A_회복진폭오늘, (int)(s->cur[YL_F_회복k]), yl_arr(s, YL_A_회복진폭오늘, (int)(s->cur[YL_F_회복마지막분])));
                            yl_arr_set(s, YL_A_회복량오늘, (int)(s->cur[YL_F_회복k]), yl_arr(s, YL_A_회복량오늘, (int)(s->cur[YL_F_회복마지막분])));
                        }
                }
            yl_arr_set(s, YL_A_회복진폭오늘, (int)(s->cur[YL_F_회복분]), (s->cur[YL_F_회복고가] - s->cur[YL_F_회복저가]));
            yl_arr_set(s, YL_A_회복량오늘, (int)(s->cur[YL_F_회복분]), s->cur[YL_F_회복누적량]);
            s->cur[YL_F_회복마지막분] = s->cur[YL_F_회복분];
            s->cur[YL_F_회복평균진폭] = 0;
            s->cur[YL_F_회복평균량] = 0;
            if ((s->cur[YL_F_회복개수] >= 1))                 {
                    s->cur[YL_F_회복평균진폭] = (s->cur[YL_F_회복평균진폭] + yl_arr(s, YL_A_회복진폭1, (int)(s->cur[YL_F_회복분])));
                    s->cur[YL_F_회복평균량] = (s->cur[YL_F_회복평균량] + yl_arr(s, YL_A_회복량1, (int)(s->cur[YL_F_회복분])));
                }
            if ((s->cur[YL_F_회복개수] >= 2))                 {
                    s->cur[YL_F_회복평균진폭] = (s->cur[YL_F_회복평균진폭] + yl_arr(s, YL_A_회복진폭2, (int)(s->cur[YL_F_회복분])));
                    s->cur[YL_F_회복평균량] = (s->cur[YL_F_회복평균량] + yl_arr(s, YL_A_회복량2, (int)(s->cur[YL_F_회복분])));
                }
            if ((s->cur[YL_F_회복개수] >= 3))                 {
                    s->cur[YL_F_회복평균진폭] = (s->cur[YL_F_회복평균진폭] + yl_arr(s, YL_A_회복진폭3, (int)(s->cur[YL_F_회복분])));
                    s->cur[YL_F_회복평균량] = (s->cur[YL_F_회복평균량] + yl_arr(s, YL_A_회복량3, (int)(s->cur[YL_F_회복분])));
                }
            if ((s->cur[YL_F_회복개수] >= 4))                 {
                    s->cur[YL_F_회복평균진폭] = (s->cur[YL_F_회복평균진폭] + yl_arr(s, YL_A_회복진폭4, (int)(s->cur[YL_F_회복분])));
                    s->cur[YL_F_회복평균량] = (s->cur[YL_F_회복평균량] + yl_arr(s, YL_A_회복량4, (int)(s->cur[YL_F_회복분])));
                }
            if ((s->cur[YL_F_회복개수] >= 5))                 {
                    s->cur[YL_F_회복평균진폭] = (s->cur[YL_F_회복평균진폭] + yl_arr(s, YL_A_회복진폭5, (int)(s->cur[YL_F_회복분])));
                    s->cur[YL_F_회복평균량] = (s->cur[YL_F_회복평균량] + yl_arr(s, YL_A_회복량5, (int)(s->cur[YL_F_회복분])));
                }
            if ((s->cur[YL_F_회복개수] >= 1))                 {
                    s->cur[YL_F_회복평균진폭] = (s->cur[YL_F_회복평균진폭] / s->cur[YL_F_회복개수]);
                    s->cur[YL_F_회복평균량] = (s->cur[YL_F_회복평균량] / s->cur[YL_F_회복개수]);
                }
            if ((s->cur[YL_F_회복평균진폭] > 0)) 
                s->cur[YL_F_본장진폭율] = ((((s->cur[YL_F_회복고가] - s->cur[YL_F_회복저가])) / s->cur[YL_F_회복평균진폭]) * 100);
            if ((s->cur[YL_F_회복평균량] > 0)) 
                s->cur[YL_F_본장거래량율] = ((s->cur[YL_F_회복누적량] / s->cur[YL_F_회복평균량]) * 100);
        }
    if (((((s->cur[YL_F_본장중] == 1) && (s->cur[YL_F_본장거래량율] > 110)) && (s->cur[YL_F_본장진폭율] > 0)) && (s->cur[YL_F_본장진폭율] < 80))) 
        s->cur[YL_F_흡수지속봉] = (s->cur[YL_F_흡수지속봉] + 1);
    else 
        s->cur[YL_F_흡수지속봉] = 0;
    s->cur[YL_F_영국서머타임] = 0;
    if (((s->cur[YL_F_봉월] >= 4) && (s->cur[YL_F_봉월] <= 9))) 
        s->cur[YL_F_영국서머타임] = 1;
    if (((s->cur[YL_F_봉월] == 3) && (s->cur[YL_F_직전일요일] >= 25))) 
        s->cur[YL_F_영국서머타임] = 1;
    if (((s->cur[YL_F_봉월] == 10) && (s->cur[YL_F_직전일요일] < 25))) 
        s->cur[YL_F_영국서머타임] = 1;
    s->cur[YL_F_아시아시작분] = ((trunc((s->cur[YL_F_아시아시작] / 10000)) * 60) + trunc((((s->cur[YL_F_아시아시작] - (trunc((s->cur[YL_F_아시아시작] / 10000)) * 10000))) / 100)));
    s->cur[YL_F_런던시작분] = ((s->cur[YL_F_영국서머타임] == 1) ? ((trunc((s->cur[YL_F_런던서머타임시작] / 10000)) * 60) + trunc((((s->cur[YL_F_런던서머타임시작] - (trunc((s->cur[YL_F_런던서머타임시작] / 10000)) * 10000))) / 100))) : ((trunc((s->cur[YL_F_런던표준시시작] / 10000)) * 60) + trunc((((s->cur[YL_F_런던표준시시작] - (trunc((s->cur[YL_F_런던표준시시작] / 10000)) * 10000))) / 100))));
    if (((((s->bar_n > 1) && (s->cur[YL_F_봉시작분] >= s->cur[YL_F_아시아시작분])) && (s->cur[YL_F_봉시작분] < (s->cur[YL_F_아시아시작분] + 60))) && (((yl_lag(s, YL_F_봉시작분, 1) < s->cur[YL_F_아시아시작분]) || (b->date != yl_lag(s, YL_F_sDate, 1)))))) 
        s->cur[YL_F_아시아시가] = b->open;
    if (((((s->bar_n > 1) && (s->cur[YL_F_봉시작분] >= s->cur[YL_F_런던시작분])) && (s->cur[YL_F_봉시작분] < (s->cur[YL_F_런던시작분] + 60))) && (((yl_lag(s, YL_F_봉시작분, 1) < s->cur[YL_F_런던시작분]) || (b->date != yl_lag(s, YL_F_sDate, 1))))))         {
            if ((s->cur[YL_F_런던저가] > 0)) 
                s->cur[YL_F_전일런던저가] = s->cur[YL_F_런던저가];
            if ((s->cur[YL_F_런던고가] > 0)) 
                s->cur[YL_F_전일런던고가] = s->cur[YL_F_런던고가];
            s->cur[YL_F_런던시가] = b->open;
            s->cur[YL_F_런던고가] = b->high;
            s->cur[YL_F_런던저가] = b->low;
        }
    else if ((((s->cur[YL_F_런던시가] > 0) && (s->cur[YL_F_본장기준봉분] >= s->cur[YL_F_런던시작분])) && (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_런던시작분] + 510))))         {
            s->cur[YL_F_런던고가] = yl_max(s->cur[YL_F_런던고가], b->high);
            s->cur[YL_F_런던저가] = yl_min(s->cur[YL_F_런던저가], b->low);
        }
    if ((s->cur[YL_F_런던고가] > 0)) 
        s->cur[YL_F_런던중심가] = (((s->cur[YL_F_런던고가] + s->cur[YL_F_런던저가])) / 2);
    if ((((s->cur[YL_F_본장중] == 0) && (s->cur[YL_F_본장기준봉분] < s->cur[YL_F_본장시작분])) && (s->cur[YL_F_본장기준봉분] >= (s->cur[YL_F_본장시작분] - s->cur[YL_F_장전장대감시분]))))         {
            if ((((s->cur[YL_F_장전장대틱] > 0) && ((b->high - b->low) >= (s->cur[YL_F_장전장대틱] * b->price_scale))) && (((s->cur[YL_F_장전위꼬리비율] <= 0) || ((b->high - yl_max(b->open, b->close)) >= ((((b->high - b->low)) * s->cur[YL_F_장전위꼬리비율]) / 100)))))) 
                s->cur[YL_F_장전장대고점] = yl_max(s->cur[YL_F_장전장대고점], b->high);
        }
    else if ((s->cur[YL_F_본장중] == 0)) 
        s->cur[YL_F_장전장대고점] = 0;
    if ((s->cur[YL_F_FX초기화] == 1))         {
            s->cur[YL_F_이벤트방향] = 0;
            s->cur[YL_F_이벤트가] = 0;
        }
    if (((((((s->cur[YL_F_이벤트장대틱] > 0) && (s->cur[YL_F_본장중] == 0)) && (s->cur[YL_F_본장기준봉분] >= (s->cur[YL_F_본장시작분] - s->cur[YL_F_이벤트감시시작분]))) && (s->cur[YL_F_본장기준봉분] <= (s->cur[YL_F_본장시작분] - s->cur[YL_F_이벤트감시끝분]))) && ((b->high - b->low) >= (s->cur[YL_F_이벤트장대틱] * b->price_scale))) && (b->close != b->open)))         {
            if ((b->close > b->open))                 {
                    s->cur[YL_F_이벤트방향] = 1;
                    s->cur[YL_F_이벤트가] = b->high;
                }
            else                 {
                    s->cur[YL_F_이벤트방향] = (-1);
                    s->cur[YL_F_이벤트가] = b->low;
                }
        }
    s->cur[YL_F_본장전구간] = 0;
    if ((((s->cur[YL_F_본장전제한분] > 0) && (s->cur[YL_F_봉시작분] >= (s->cur[YL_F_본장시작분] - s->cur[YL_F_본장전제한분]))) && (s->cur[YL_F_봉시작분] < s->cur[YL_F_본장시작분]))) 
        s->cur[YL_F_본장전구간] = 1;
    s->cur[YL_F_조정매도제한] = 0;
    if ((((s->cur[YL_F_조정매도허용분] > 0) && (b->time >= 70000)) && (s->cur[YL_F_봉시작분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_조정매도허용분])))) 
        s->cur[YL_F_조정매도제한] = 1;
    if (((2 == 2) && (1 == 1)))         {
            yl_WSF_FXCurveV1(s, b);
            s->cur[YL_F_미래방향] = ((s->cur[YL_F_곡선예측방향] > 0) ? 1 : ((s->cur[YL_F_곡선예측방향] < 0) ? (-1) : 0));
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_미래방향] = 0;
            yl_WSF_FXSessionProfileV1_CO(s, b);
            yl_WSF_FXADXV1(s, b);
            yl_WSF_FXDecisionV2_CO(s, b);
            s->cur[YL_F_통합색상] = ((s->cur[YL_F_통합상태] == 100) ? yl_rgb(255, 0, 0) : ((s->cur[YL_F_통합상태] == 50) ? yl_rgb(255, 140, 140) : ((s->cur[YL_F_통합상태] == (-100)) ? yl_rgb(0, 0, 255) : ((s->cur[YL_F_통합상태] == (-50)) ? yl_rgb(140, 165, 255) : yl_rgb(150, 150, 150)))));
            s->cur[YL_F_통합방향] = ((s->cur[YL_F_통합상태] > 0) ? 1 : ((s->cur[YL_F_통합상태] < 0) ? (-1) : 0));
            if ((s->cur[YL_F_통합방향] != 0))                 {
                    if ((((s->cur[YL_F_미래방향] != s->cur[YL_F_통합방향]) || (s->cur[YL_F_DI방향] != s->cur[YL_F_통합방향])) || ((s->cur[YL_F_매물대5상태] * s->cur[YL_F_통합방향]) < 0))) 
                        s->cur[YL_F_통합색상] = yl_rgb(210, 210, 210);
                }
            yl_WSF_FXEntryCandV16_CO(s, b);
            s->cur[YL_F_미래계산가능] = yl_WSF_FXFutureValuesV3_CO(s, b);
            if ((((s->cur[YL_F_FX초기화] == 1) || (s->cur[YL_F_미래계산가능] != 1)) || (s->cur[YL_F_지속저장목표2] == 0))) 
                s->cur[YL_F_지속목표방향] = 0;
            else if ((((((s->cur[YL_F_지속저장목표1] != yl_lag(s, YL_F_지속저장목표1, 1)) || (s->cur[YL_F_지속저장목표2] != yl_lag(s, YL_F_지속저장목표2, 1))) || (s->cur[YL_F_지속저장목표3] != yl_lag(s, YL_F_지속저장목표3, 1))) || (s->cur[YL_F_지속저장목표4] != yl_lag(s, YL_F_지속저장목표4, 1))) || (s->cur[YL_F_지속저장목표5] != yl_lag(s, YL_F_지속저장목표5, 1))))                 {
                    if ((s->cur[YL_F_지속저장목표2] > s->cur[YL_F_곡선회귀선_평탄])) 
                        s->cur[YL_F_지속목표방향] = 1;
                    else if ((s->cur[YL_F_지속저장목표2] < s->cur[YL_F_곡선회귀선_평탄])) 
                        s->cur[YL_F_지속목표방향] = (-1);
                }
            s->cur[YL_F_조정유효] = yl_WSF_FXWaveAdjV6_CO(s, b);
            s->cur[YL_F_돌파값유효] = 0;
            if ((((((s->cur[YL_F_미래계산가능] == 1) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (s->cur[YL_F_핵심지난상승최저가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] > 0)) && (s->cur[YL_F_핵심지난하락최저가] > 0))) 
                s->cur[YL_F_돌파값유효] = 1;
            s->cur[YL_F_돌파매수조건] = 0;
            s->cur[YL_F_돌파매도조건] = 0;
            if ((s->cur[YL_F_돌파값유효] == 1))                 {
                    s->cur[YL_F_돌파매수기준선] = ((s->cur[YL_F_완화단계] >= 3) ? s->cur[YL_F_핵심지난상승_382] : ((s->cur[YL_F_완화단계] == 2) ? s->cur[YL_F_핵심지난상승_500] : s->cur[YL_F_핵심지난상승_618]));
                    s->cur[YL_F_돌파매도기준선] = ((s->cur[YL_F_완화단계] >= 3) ? s->cur[YL_F_핵심지난하락_618] : ((s->cur[YL_F_완화단계] == 2) ? s->cur[YL_F_핵심지난하락_500] : s->cur[YL_F_핵심지난하락_382]));
                    if (((s->cur[YL_F_돌파매수기준선] < s->cur[YL_F_핵심지난하락최저가]) && (s->cur[YL_F_핵심지난하락최고가] >= s->cur[YL_F_핵심지난상승최고가]))) 
                        s->cur[YL_F_돌파매수조건] = 1;
                    if (((s->cur[YL_F_돌파매도기준선] > s->cur[YL_F_핵심지난상승최고가]) && (s->cur[YL_F_핵심지난상승최저가] <= s->cur[YL_F_핵심지난하락최저가]))) 
                        s->cur[YL_F_돌파매도조건] = 1;
                }
            s->cur[YL_F_구조돌파방향] = 0;
            if ((((((s->bar_n > 1) && (yl_lag(s, YL_F_돌파매수조건, 1) == 1)) && (((s->cur[YL_F_돌파통합확인] != 1) || (s->cur[YL_F_통합상태] >= 50)))) && (b->close > yl_lag(s, YL_F_핵심지난하락최고가, 1))) && (yl_px(s, 'C', 1) <= yl_lag(s, YL_F_핵심지난하락최고가, 1)))) 
                s->cur[YL_F_구조돌파방향] = 1;
            else if ((((((s->bar_n > 1) && (yl_lag(s, YL_F_돌파매도조건, 1) == 1)) && (((s->cur[YL_F_돌파통합확인] != 1) || (s->cur[YL_F_통합상태] <= (-50))))) && (b->close < yl_lag(s, YL_F_핵심지난상승최저가, 1))) && (yl_px(s, 'C', 1) >= yl_lag(s, YL_F_핵심지난상승최저가, 1)))) 
                s->cur[YL_F_구조돌파방향] = (-1);
            if ((((s->cur[YL_F_본장전구간] == 1) && (((((s->cur[YL_F_구조돌파방향] == 1) && (s->cur[YL_F_통합상태] != 100))) || (((s->cur[YL_F_구조돌파방향] == (-1)) && (s->cur[YL_F_통합상태] != (-100))))))))) 
                s->cur[YL_F_구조돌파방향] = 0;
            if ((s->cur[YL_F_구조돌파방향] == 1)) 
                s->cur[YL_F_구조돌파방향] = 0;
            if ((s->cur[YL_F_구조돌파방향] == (-1))) 
                s->cur[YL_F_구조돌파방향] = 0;
            s->cur[YL_F_매수돌파사용가] = yl_lag(s, YL_F_매수돌파사용가, 1);
            s->cur[YL_F_매도돌파사용가] = yl_lag(s, YL_F_매도돌파사용가, 1);
            if ((s->cur[YL_F_FX초기화] == 1))                 {
                    s->cur[YL_F_매수돌파사용가] = 0;
                    s->cur[YL_F_매도돌파사용가] = 0;
                }
            if ((((s->cur[YL_F_돌파1회] == 1) && (s->cur[YL_F_구조돌파방향] == 1)) && (yl_lag(s, YL_F_핵심지난하락최고가, 1) == s->cur[YL_F_매수돌파사용가]))) 
                s->cur[YL_F_구조돌파방향] = 0;
            if ((((s->cur[YL_F_돌파1회] == 1) && (s->cur[YL_F_구조돌파방향] == (-1))) && (yl_lag(s, YL_F_핵심지난상승최저가, 1) == s->cur[YL_F_매도돌파사용가]))) 
                s->cur[YL_F_구조돌파방향] = 0;
            if ((s->cur[YL_F_구조돌파방향] == 1)) 
                s->cur[YL_F_매수돌파사용가] = yl_lag(s, YL_F_핵심지난하락최고가, 1);
            if ((s->cur[YL_F_구조돌파방향] == (-1))) 
                s->cur[YL_F_매도돌파사용가] = yl_lag(s, YL_F_핵심지난상승최저가, 1);
            s->cur[YL_F_선행유효] = yl_WSF_FXFlatCountV2_CO(s, b);
            s->cur[YL_F_선행세션최대진폭] = yl_lag(s, YL_F_선행세션최대진폭, 1);
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_선행세션최대진폭] = 0;
            if (((s->cur[YL_F_선행유효] == 1) && (s->cur[YL_F_선행구간진폭] > s->cur[YL_F_선행세션최대진폭]))) 
                s->cur[YL_F_선행세션최대진폭] = s->cur[YL_F_선행구간진폭];
            s->cur[YL_F_선행진폭율] = ((s->cur[YL_F_선행세션최대진폭] > 0) ? ((s->cur[YL_F_선행구간진폭] / s->cur[YL_F_선행세션최대진폭]) * 100) : 0);
            if ((s->bar_n > 1))                 {
                    s->cur[YL_F_구조이전하락최저] = yl_lag(s, YL_F_구조이전하락최저, 1);
                    s->cur[YL_F_구조이전상승최저] = yl_lag(s, YL_F_구조이전상승최저, 1);
                    s->cur[YL_F_구조이전하락최고] = yl_lag(s, YL_F_구조이전하락최고, 1);
                    s->cur[YL_F_구조이전상승최고] = yl_lag(s, YL_F_구조이전상승최고, 1);
                }
            if ((s->cur[YL_F_핵심지난하락최저가] == 0)) 
                s->cur[YL_F_구조이전하락최저] = 0;
            else if ((((s->bar_n > 1) && (s->cur[YL_F_핵심지난하락최저가] != yl_lag(s, YL_F_핵심지난하락최저가, 1))) && (yl_lag(s, YL_F_핵심지난하락최저가, 1) > 0))) 
                s->cur[YL_F_구조이전하락최저] = yl_lag(s, YL_F_핵심지난하락최저가, 1);
            if ((s->cur[YL_F_핵심지난상승최저가] == 0)) 
                s->cur[YL_F_구조이전상승최저] = 0;
            else if ((((s->bar_n > 1) && (s->cur[YL_F_핵심지난상승최저가] != yl_lag(s, YL_F_핵심지난상승최저가, 1))) && (yl_lag(s, YL_F_핵심지난상승최저가, 1) > 0))) 
                s->cur[YL_F_구조이전상승최저] = yl_lag(s, YL_F_핵심지난상승최저가, 1);
            if ((s->cur[YL_F_핵심지난하락최고가] == 0)) 
                s->cur[YL_F_구조이전하락최고] = 0;
            else if ((((s->bar_n > 1) && (s->cur[YL_F_핵심지난하락최고가] != yl_lag(s, YL_F_핵심지난하락최고가, 1))) && (yl_lag(s, YL_F_핵심지난하락최고가, 1) > 0))) 
                s->cur[YL_F_구조이전하락최고] = yl_lag(s, YL_F_핵심지난하락최고가, 1);
            if ((s->cur[YL_F_핵심지난상승최고가] == 0)) 
                s->cur[YL_F_구조이전상승최고] = 0;
            else if ((((s->bar_n > 1) && (s->cur[YL_F_핵심지난상승최고가] != yl_lag(s, YL_F_핵심지난상승최고가, 1))) && (yl_lag(s, YL_F_핵심지난상승최고가, 1) > 0))) 
                s->cur[YL_F_구조이전상승최고] = yl_lag(s, YL_F_핵심지난상승최고가, 1);
            s->cur[YL_F_하락추세구조] = (((((((((s->cur[YL_F_구조이전하락최저] > 0) && (s->cur[YL_F_핵심지난하락최저가] < s->cur[YL_F_구조이전하락최저])) && (s->cur[YL_F_구조이전상승최저] > 0)) && (s->cur[YL_F_핵심지난상승최저가] < s->cur[YL_F_구조이전상승최저])) && (s->cur[YL_F_구조이전하락최고] > 0)) && (s->cur[YL_F_핵심지난하락최고가] < s->cur[YL_F_구조이전하락최고])) && (s->cur[YL_F_구조이전상승최고] > 0)) && (s->cur[YL_F_핵심지난상승최고가] < s->cur[YL_F_구조이전상승최고])) ? 1 : 0);
            s->cur[YL_F_상승추세구조] = (((((((((s->cur[YL_F_구조이전하락최저] > 0) && (s->cur[YL_F_핵심지난하락최저가] > s->cur[YL_F_구조이전하락최저])) && (s->cur[YL_F_구조이전상승최저] > 0)) && (s->cur[YL_F_핵심지난상승최저가] > s->cur[YL_F_구조이전상승최저])) && (s->cur[YL_F_구조이전하락최고] > 0)) && (s->cur[YL_F_핵심지난하락최고가] > s->cur[YL_F_구조이전하락최고])) && (s->cur[YL_F_구조이전상승최고] > 0)) && (s->cur[YL_F_핵심지난상승최고가] > s->cur[YL_F_구조이전상승최고])) ? 1 : 0);
            s->cur[YL_F_파랑구조] = 0;
            if (((((((s->cur[YL_F_핵심지난상승최고가] > 0) && (s->cur[YL_F_핵심지난하락최고가] > 0)) && (s->cur[YL_F_핵심지난상승최저가] > 0)) && (s->cur[YL_F_핵심지난하락최저가] > 0)) && (s->cur[YL_F_핵심지난상승최고가] < s->cur[YL_F_핵심지난하락최고가])) && (s->cur[YL_F_핵심지난하락최저가] < s->cur[YL_F_핵심지난상승최저가]))) 
                s->cur[YL_F_파랑구조] = 1;
            s->cur[YL_F_빨강구조] = 0;
            if (((((((s->cur[YL_F_핵심지난상승최고가] > 0) && (s->cur[YL_F_핵심지난하락최고가] > 0)) && (s->cur[YL_F_핵심지난상승최저가] > 0)) && (s->cur[YL_F_핵심지난하락최저가] > 0)) && (s->cur[YL_F_핵심지난상승최고가] > s->cur[YL_F_핵심지난하락최고가])) && (s->cur[YL_F_핵심지난상승최저가] < s->cur[YL_F_핵심지난하락최저가]))) 
                s->cur[YL_F_빨강구조] = 1;
            s->cur[YL_F_하락중위치이탈] = yl_lag(s, YL_F_하락중위치이탈, 1);
            s->cur[YL_F_상승중위치이탈] = yl_lag(s, YL_F_상승중위치이탈, 1);
            s->cur[YL_F_선행방향] = 0;
            if ((s->cur[YL_F_FX초기화] == 1))                 {
                    s->cur[YL_F_하락중위치이탈] = 1;
                    s->cur[YL_F_상승중위치이탈] = 1;
                }
            if (((s->cur[YL_F_선행유효] == 1) && (s->cur[YL_F_FX초기화] == 0)))                 {
                    if (((s->cur[YL_F_선행기울기Cnt] < 0) && (yl_lag(s, YL_F_선행기울기Cnt, 1) >= 0))) 
                        s->cur[YL_F_하락중위치이탈] = 0;
                    if (((s->cur[YL_F_선행기울기Cnt] > 0) && (yl_lag(s, YL_F_선행기울기Cnt, 1) <= 0))) 
                        s->cur[YL_F_상승중위치이탈] = 0;
                    if (((((s->cur[YL_F_선행기울기Cnt] < (-s->cur[YL_F_기울기기준])) && (s->cur[YL_F_하락중위치이탈] == 0)) && (s->cur[YL_F_선행위치Cnt] < 0)) && (yl_lag(s, YL_F_선행위치Cnt, 1) >= 0))) 
                        s->cur[YL_F_선행방향] = (-1);
                    if (((((s->cur[YL_F_선행기울기Cnt] > s->cur[YL_F_기울기기준]) && (s->cur[YL_F_상승중위치이탈] == 0)) && (s->cur[YL_F_선행위치Cnt] > 0)) && (yl_lag(s, YL_F_선행위치Cnt, 1) <= 0))) 
                        s->cur[YL_F_선행방향] = 1;
                    if (((s->cur[YL_F_선행기울기Cnt] < 0) && (s->cur[YL_F_선행위치Cnt] < 0))) 
                        s->cur[YL_F_하락중위치이탈] = 1;
                    if (((s->cur[YL_F_선행기울기Cnt] > 0) && (s->cur[YL_F_선행위치Cnt] > 0))) 
                        s->cur[YL_F_상승중위치이탈] = 1;
                }
            if (((s->cur[YL_F_선행통합100] == 1) && (s->cur[YL_F_선행방향] == (-1))))                 {
                    if (((((s->cur[YL_F_통합상태] > (-100)) || (s->cur[YL_F_미래방향] != (-1))) || (s->cur[YL_F_DI방향] != (-1))) || (s->cur[YL_F_매물대5상태] > 0))) 
                        s->cur[YL_F_선행방향] = 0;
                }
            if (((s->cur[YL_F_선행통합100] == 1) && (s->cur[YL_F_선행방향] == 1)))                 {
                    if (((((s->cur[YL_F_통합상태] < 100) || (s->cur[YL_F_미래방향] != 1)) || (s->cur[YL_F_DI방향] != 1)) || (s->cur[YL_F_매물대5상태] < 0))) 
                        s->cur[YL_F_선행방향] = 0;
                }
            s->cur[YL_F_조정표시] = 0;
            if (((((s->cur[YL_F_조정유효] == 1) && (s->cur[YL_F_조정신호방향] != 0)) && (((s->cur[YL_F_조정통합확인] != 1) || ((s->cur[YL_F_통합상태] * s->cur[YL_F_조정신호방향]) >= 50)))) && ((((s->cur[YL_F_조정최소비] <= 0) || (s->cur[YL_F_가격조정비] >= s->cur[YL_F_조정최소비])) || (s->cur[YL_F_시간조정비] >= s->cur[YL_F_조정최소비]))))) 
                s->cur[YL_F_조정표시] = 1;
            if ((((((s->cur[YL_F_조정매수382제한] == 1) && (s->cur[YL_F_조정표시] == 1)) && (s->cur[YL_F_조정신호방향] == 1)) && (s->cur[YL_F_핵심지난상승_382] > 0)) && (b->high < s->cur[YL_F_핵심지난상승_382]))) 
                s->cur[YL_F_조정표시] = 0;
            s->cur[YL_F_상승상태봉] = ((((s->cur[YL_F_조정유효] == 1) && (s->cur[YL_F_조정추세방향] == 1)) && (((s->cur[YL_F_조정중] == 0) || (s->cur[YL_F_조정연빨강포함] == 1)))) ? 1 : 0);
            if ((((((s->cur[YL_F_조정상태봉수] > 0) && (s->cur[YL_F_조정표시] == 1)) && (s->cur[YL_F_조정신호방향] == 1)) && (b->time >= 70000)) && (b->time < s->cur[YL_F_적용본장시작])))                 {
                    s->cur[YL_F_상승상태합] = 0;
                    for (s->cur[YL_F_상태i] = (0); s->cur[YL_F_상태i] <= ((s->cur[YL_F_조정상태봉수] - 1)); s->cur[YL_F_상태i] += 1)                         {
                            s->cur[YL_F_상승상태합] = (s->cur[YL_F_상승상태합] + yl_lag(s, YL_F_상승상태봉, s->cur[YL_F_상태i]));
                        }
                    if ((s->cur[YL_F_상승상태합] >= ((s->cur[YL_F_조정상태봉수] * s->cur[YL_F_조정상태비율]) / 100))) 
                        s->cur[YL_F_조정표시] = 0;
                }
            if ((((s->cur[YL_F_조정매도통합100] == 1) && (s->cur[YL_F_조정표시] == 1)) && (s->cur[YL_F_조정신호방향] == (-1))))                 {
                    if (((((s->cur[YL_F_통합상태] > (-100)) || (s->cur[YL_F_미래방향] != (-1))) || (s->cur[YL_F_DI방향] != (-1))) || (s->cur[YL_F_매물대5상태] > 0))) 
                        s->cur[YL_F_조정표시] = 0;
                }
            if ((((((s->cur[YL_F_조정매도저점근접비] > 0) && (s->cur[YL_F_조정표시] == 1)) && (s->cur[YL_F_조정신호방향] == (-1))) && (s->cur[YL_F_핵심지난하락최고가] > s->cur[YL_F_핵심지난하락최저가])) && (s->cur[YL_F_핵심지난하락최저가] > 0)))                 {
                    s->cur[YL_F_매도저점위치비] = ((((b->close - s->cur[YL_F_핵심지난하락최저가])) / ((s->cur[YL_F_핵심지난하락최고가] - s->cur[YL_F_핵심지난하락최저가]))) * 100);
                    if (((s->cur[YL_F_매도저점위치비] >= 0) && (s->cur[YL_F_매도저점위치비] <= s->cur[YL_F_조정매도저점근접비]))) 
                        s->cur[YL_F_조정표시] = 0;
                }
            if ((((((s->cur[YL_F_조정매수직전상승제한] > 0) && (s->cur[YL_F_조정표시] == 1)) && (s->cur[YL_F_조정신호방향] == 1)) && (s->cur[YL_F_선행유효] == 1)) && (s->cur[YL_F_선행직전상승기울기] > s->cur[YL_F_조정매수직전상승제한]))) 
                s->cur[YL_F_조정표시] = 0;
            s->cur[YL_F_스윙폭세션최대] = yl_lag(s, YL_F_스윙폭세션최대, 1);
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_스윙폭세션최대] = 0;
            s->cur[YL_F_스윙전체폭비] = 0;
            if ((((((s->cur[YL_F_미래계산가능] == 1) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] > 0)) && (s->cur[YL_F_핵심지난상승최저가] > 0)) && (s->cur[YL_F_핵심지난하락최저가] > 0)))                 {
                    s->cur[YL_F_스윙전체최고] = yl_max(s->cur[YL_F_핵심지난상승최고가], s->cur[YL_F_핵심지난하락최고가]);
                    s->cur[YL_F_스윙전체최저] = yl_min(s->cur[YL_F_핵심지난상승최저가], s->cur[YL_F_핵심지난하락최저가]);
                    s->cur[YL_F_스윙전체폭] = (s->cur[YL_F_스윙전체최고] - s->cur[YL_F_스윙전체최저]);
                    if ((s->cur[YL_F_스윙전체폭] > s->cur[YL_F_스윙폭세션최대])) 
                        s->cur[YL_F_스윙폭세션최대] = s->cur[YL_F_스윙전체폭];
                    if ((s->cur[YL_F_스윙폭세션최대] > 0)) 
                        s->cur[YL_F_스윙전체폭비] = ((s->cur[YL_F_스윙전체폭] / s->cur[YL_F_스윙폭세션최대]) * 100);
                }
            if (((((s->cur[YL_F_조정매도스윙폭제한] > 0) && (s->cur[YL_F_조정표시] == 1)) && (s->cur[YL_F_조정신호방향] == (-1))) && (s->cur[YL_F_스윙전체폭비] >= s->cur[YL_F_조정매도스윙폭제한]))) 
                s->cur[YL_F_조정표시] = 0;
            if ((((s->cur[YL_F_선행매수스윙폭제한] > 0) && (s->cur[YL_F_선행방향] == 1)) && (s->cur[YL_F_스윙전체폭비] >= s->cur[YL_F_선행매수스윙폭제한]))) 
                s->cur[YL_F_선행방향] = 0;
            if ((((s->cur[YL_F_본장전구간] == 1) && (((((s->cur[YL_F_선행방향] == 1) && (s->cur[YL_F_통합상태] != 100))) || (((s->cur[YL_F_선행방향] == (-1)) && (s->cur[YL_F_통합상태] != (-100))))))))) 
                s->cur[YL_F_선행방향] = 0;
            s->cur[YL_F_선행방향] = 0;
            s->cur[YL_F_S함수가능] = yl_WSF_FXFutureValuesV1(s, b);
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_S삼선차최고] = 0;
            else 
                s->cur[YL_F_S삼선차최고] = yl_lag(s, YL_F_S삼선차최고, 1);
            s->cur[YL_F_S삼선차] = 0;
            s->cur[YL_F_S삼선비율] = 0;
            if (((((s->cur[YL_F_S함수가능] == 1) && (s->cur[YL_F_S목표1] != 0)) && (s->cur[YL_F_S목표2] != 0)) && (s->cur[YL_F_S목표3] != 0)))                 {
                    s->cur[YL_F_S삼선차] = (yl_max(yl_max(s->cur[YL_F_S목표1], s->cur[YL_F_S목표2]), s->cur[YL_F_S목표3]) - yl_min(yl_min(s->cur[YL_F_S목표1], s->cur[YL_F_S목표2]), s->cur[YL_F_S목표3]));
                    s->cur[YL_F_S삼선차최고] = yl_max(s->cur[YL_F_S삼선차최고], s->cur[YL_F_S삼선차]);
                    if ((s->cur[YL_F_S삼선차최고] > 0)) 
                        s->cur[YL_F_S삼선비율] = ((s->cur[YL_F_S삼선차] / s->cur[YL_F_S삼선차최고]) * 100);
                }
            s->cur[YL_F_S삼선준비] = 0;
            if (((((s->cur[YL_F_S함수가능] == 1) && (s->cur[YL_F_S목표1] != 0)) && (s->cur[YL_F_S목표2] != 0)) && (s->cur[YL_F_S목표3] != 0))) 
                s->cur[YL_F_S삼선준비] = 1;
            if (((s->cur[YL_F_FX초기화] == 1) || (s->cur[YL_F_S삼선준비] == 0)))                 {
                    s->cur[YL_F_S구간Cnt] = 0;
                    s->cur[YL_F_S현재최고] = 0;
                    s->cur[YL_F_S현재최저] = 0;
                    s->cur[YL_F_S직전최고] = 0;
                    s->cur[YL_F_S직전최저] = 0;
                    s->cur[YL_F_S직전유효] = 0;
                }
            else                 {
                    s->cur[YL_F_S구간Cnt] = yl_lag(s, YL_F_S구간Cnt, 1);
                    s->cur[YL_F_S현재최고] = yl_lag(s, YL_F_S현재최고, 1);
                    s->cur[YL_F_S현재최저] = yl_lag(s, YL_F_S현재최저, 1);
                    s->cur[YL_F_S직전최고] = yl_lag(s, YL_F_S직전최고, 1);
                    s->cur[YL_F_S직전최저] = yl_lag(s, YL_F_S직전최저, 1);
                    s->cur[YL_F_S직전유효] = yl_lag(s, YL_F_S직전유효, 1);
                }
            if ((s->cur[YL_F_S삼선준비] == 1))                 {
                    if (((s->cur[YL_F_FX초기화] == 1) || (yl_lag(s, YL_F_S삼선준비, 1) == 0)))                         {
                            s->cur[YL_F_S구간Cnt] = 1;
                            s->cur[YL_F_S현재최고] = b->high;
                            s->cur[YL_F_S현재최저] = b->low;
                            s->cur[YL_F_S직전최고] = 0;
                            s->cur[YL_F_S직전최저] = 0;
                            s->cur[YL_F_S직전유효] = 0;
                        }
                    else if (((s->cur[YL_F_S삼선차] != yl_lag(s, YL_F_S삼선차, 1)) && (yl_lag(s, YL_F_S구간Cnt, 1) >= yl_max(1, s->cur[YL_F_두구간직전최소봉수]))))                         {
                            s->cur[YL_F_S직전최고] = yl_lag(s, YL_F_S현재최고, 1);
                            s->cur[YL_F_S직전최저] = yl_lag(s, YL_F_S현재최저, 1);
                            s->cur[YL_F_S직전유효] = 1;
                            s->cur[YL_F_S구간Cnt] = 1;
                            s->cur[YL_F_S현재최고] = b->high;
                            s->cur[YL_F_S현재최저] = b->low;
                        }
                    else                         {
                            s->cur[YL_F_S구간Cnt] = (yl_lag(s, YL_F_S구간Cnt, 1) + 1);
                            s->cur[YL_F_S현재최고] = yl_max(yl_lag(s, YL_F_S현재최고, 1), b->high);
                            s->cur[YL_F_S현재최저] = yl_min(yl_lag(s, YL_F_S현재최저, 1), b->low);
                        }
                }
            s->cur[YL_F_S두구간범위] = 0;
            if ((s->cur[YL_F_S삼선준비] == 1))                 {
                    s->cur[YL_F_S두구간최고] = s->cur[YL_F_S현재최고];
                    s->cur[YL_F_S두구간최저] = s->cur[YL_F_S현재최저];
                    if ((s->cur[YL_F_S직전유효] == 1))                         {
                            s->cur[YL_F_S두구간최고] = yl_max(s->cur[YL_F_S현재최고], s->cur[YL_F_S직전최고]);
                            s->cur[YL_F_S두구간최저] = yl_min(s->cur[YL_F_S현재최저], s->cur[YL_F_S직전최저]);
                        }
                    s->cur[YL_F_S두구간범위] = (s->cur[YL_F_S두구간최고] - s->cur[YL_F_S두구간최저]);
                }
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_S두구간세션최대] = 0;
            else 
                s->cur[YL_F_S두구간세션최대] = yl_lag(s, YL_F_S두구간세션최대, 1);
            s->cur[YL_F_S두구간최대] = 0;
            s->cur[YL_F_S두구간비율] = 0;
            if ((s->cur[YL_F_S삼선준비] == 1))                 {
                    s->cur[YL_F_S두구간세션최대] = yl_max(s->cur[YL_F_S두구간세션최대], s->cur[YL_F_S두구간범위]);
                    s->cur[YL_F_S두구간최대] = s->cur[YL_F_S두구간세션최대];
                    s->cur[YL_F_S두구간창] = 0;
                    if ((s->cur[YL_F_두구간비율기준봉수] > 0)) 
                        s->cur[YL_F_S두구간창] = yl_min(round(s->cur[YL_F_두구간비율기준봉수]), s->cur[YL_F_FX세션봉수]);
                    if ((s->cur[YL_F_S두구간창] > 0))                         {
                            s->cur[YL_F_S두구간최대] = 0;
                            for (s->cur[YL_F_S두구간j] = (0); s->cur[YL_F_S두구간j] <= ((s->cur[YL_F_S두구간창] - 1)); s->cur[YL_F_S두구간j] += 1)                                 {
                                    s->cur[YL_F_S두구간최대] = yl_max(s->cur[YL_F_S두구간최대], yl_lag(s, YL_F_S두구간범위, s->cur[YL_F_S두구간j]));
                                }
                        }
                    if ((s->cur[YL_F_S두구간최대] > 0)) 
                        s->cur[YL_F_S두구간비율] = ((s->cur[YL_F_S두구간범위] / s->cur[YL_F_S두구간최대]) * 100);
                }
            s->cur[YL_F_S합성가능5분] = yl_WSF_FXSyntheticLinesV1(s, b);
            s->cur[YL_F_S합성가능15분] = yl_WSF_FXSyntheticLinesV1(s, b);
            s->cur[YL_F_S합성가능30분] = yl_WSF_FXSyntheticLinesV1(s, b);
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_S평탄회귀차최고] = 0;
            else 
                s->cur[YL_F_S평탄회귀차최고] = yl_lag(s, YL_F_S평탄회귀차최고, 1);
            s->cur[YL_F_S기준선준비] = 0;
            if ((((((s->cur[YL_F_S함수가능] == 1) && (s->cur[YL_F_S곡선평탄] != 0)) && (s->cur[YL_F_S회귀유효5분] == 1)) && (s->cur[YL_F_S회귀유효15분] == 1)) && (s->cur[YL_F_S회귀유효30분] == 1))) 
                s->cur[YL_F_S기준선준비] = 1;
            s->cur[YL_F_S평탄회귀차] = 0;
            s->cur[YL_F_S평탄회귀차비율] = 0;
            if ((s->cur[YL_F_S기준선준비] == 1))                 {
                    s->cur[YL_F_S기준선최고] = yl_max(yl_max(yl_max(s->cur[YL_F_S곡선평탄], s->cur[YL_F_S평탄회귀5분]), s->cur[YL_F_S평탄회귀15분]), s->cur[YL_F_S평탄회귀30분]);
                    s->cur[YL_F_S기준선최저] = yl_min(yl_min(yl_min(s->cur[YL_F_S곡선평탄], s->cur[YL_F_S평탄회귀5분]), s->cur[YL_F_S평탄회귀15분]), s->cur[YL_F_S평탄회귀30분]);
                    s->cur[YL_F_S평탄회귀차] = (s->cur[YL_F_S기준선최고] - s->cur[YL_F_S기준선최저]);
                    s->cur[YL_F_S평탄회귀차최고] = yl_max(s->cur[YL_F_S평탄회귀차최고], s->cur[YL_F_S평탄회귀차]);
                    s->cur[YL_F_S평탄기준최고] = s->cur[YL_F_S평탄회귀차최고];
                    s->cur[YL_F_S평탄창] = 0;
                    if ((s->cur[YL_F_두구간비율기준봉수] > 0)) 
                        s->cur[YL_F_S평탄창] = yl_min(round(s->cur[YL_F_두구간비율기준봉수]), s->cur[YL_F_FX세션봉수]);
                    if ((s->cur[YL_F_S평탄창] > 0))                         {
                            s->cur[YL_F_S평탄기준최고] = 0;
                            for (s->cur[YL_F_S평탄j] = (0); s->cur[YL_F_S평탄j] <= ((s->cur[YL_F_S평탄창] - 1)); s->cur[YL_F_S평탄j] += 1)                                 {
                                    s->cur[YL_F_S평탄기준최고] = yl_max(s->cur[YL_F_S평탄기준최고], yl_lag(s, YL_F_S평탄회귀차, s->cur[YL_F_S평탄j]));
                                }
                        }
                    if ((s->cur[YL_F_S평탄기준최고] > 0)) 
                        s->cur[YL_F_S평탄회귀차비율] = ((s->cur[YL_F_S평탄회귀차] / s->cur[YL_F_S평탄기준최고]) * 100);
                }
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_S마켓차최고] = 0;
            else 
                s->cur[YL_F_S마켓차최고] = yl_lag(s, YL_F_S마켓차최고, 1);
            s->cur[YL_F_S마켓준비] = 0;
            if ((((((s->cur[YL_F_S함수가능] == 1) && (s->cur[YL_F_S마켓중심] != 0)) && (s->cur[YL_F_S마켓유효5분] == 1)) && (s->cur[YL_F_S마켓유효15분] == 1)) && (s->cur[YL_F_S마켓유효30분] == 1))) 
                s->cur[YL_F_S마켓준비] = 1;
            s->cur[YL_F_S마켓차] = 0;
            s->cur[YL_F_S마켓차비율] = 0;
            if ((s->cur[YL_F_S마켓준비] == 1))                 {
                    s->cur[YL_F_S마켓차] = (yl_max(yl_max(yl_max(s->cur[YL_F_S마켓중심], s->cur[YL_F_S마켓중심5분]), s->cur[YL_F_S마켓중심15분]), s->cur[YL_F_S마켓중심30분]) - yl_min(yl_min(yl_min(s->cur[YL_F_S마켓중심], s->cur[YL_F_S마켓중심5분]), s->cur[YL_F_S마켓중심15분]), s->cur[YL_F_S마켓중심30분]));
                    s->cur[YL_F_S마켓차최고] = yl_max(s->cur[YL_F_S마켓차최고], s->cur[YL_F_S마켓차]);
                    s->cur[YL_F_S마켓기준최고] = s->cur[YL_F_S마켓차최고];
                    if ((s->cur[YL_F_S평탄창] > 0))                         {
                            s->cur[YL_F_S마켓기준최고] = 0;
                            for (s->cur[YL_F_S평탄j] = (0); s->cur[YL_F_S평탄j] <= ((s->cur[YL_F_S평탄창] - 1)); s->cur[YL_F_S평탄j] += 1)                                 {
                                    s->cur[YL_F_S마켓기준최고] = yl_max(s->cur[YL_F_S마켓기준최고], yl_lag(s, YL_F_S마켓차, s->cur[YL_F_S평탄j]));
                                }
                        }
                    if ((s->cur[YL_F_S마켓기준최고] > 0)) 
                        s->cur[YL_F_S마켓차비율] = ((s->cur[YL_F_S마켓차] / s->cur[YL_F_S마켓기준최고]) * 100);
                }
            s->cur[YL_F_S이탈발생] = yl_lag(s, YL_F_S이탈발생, 1);
            if ((((s->cur[YL_F_FX초기화] == 1) || (s->cur[YL_F_S삼선준비] == 0)) || (s->cur[YL_F_S구간Cnt] == 1))) 
                s->cur[YL_F_S이탈발생] = 0;
            s->cur[YL_F_S현재위치] = 0;
            s->cur[YL_F_S첫이탈] = 0;
            if (((s->cur[YL_F_S삼선준비] == 1) && (s->cur[YL_F_S직전유효] == 1)))                 {
                    if ((b->close > (s->cur[YL_F_S직전최고] + (yl_max(0, s->cur[YL_F_이탈여유틱]) * b->price_scale)))) 
                        s->cur[YL_F_S현재위치] = 1;
                    else if ((b->close < (s->cur[YL_F_S직전최저] - (yl_max(0, s->cur[YL_F_이탈여유틱]) * b->price_scale)))) 
                        s->cur[YL_F_S현재위치] = (-1);
                    if (((s->cur[YL_F_S이탈발생] == 0) && (s->cur[YL_F_S현재위치] != 0)))                         {
                            s->cur[YL_F_S첫이탈] = s->cur[YL_F_S현재위치];
                            s->cur[YL_F_S이탈발생] = 1;
                        }
                }
            s->cur[YL_F_S복합압축조건] = 0;
            s->cur[YL_F_S가격압축이탈] = 0;
            if (((((s->cur[YL_F_S함수가능] == 1) && (s->cur[YL_F_S삼선준비] == 1)) && (s->cur[YL_F_S두구간최대] > 0)) && (s->cur[YL_F_S두구간비율] < 40)))                 {
                    if ((((((s->cur[YL_F_S기준선준비] == 1) && (s->cur[YL_F_S마켓준비] == 1)) && (s->cur[YL_F_S삼선비율] <= 30)) && (s->cur[YL_F_S평탄회귀차비율] < 30)) && (s->cur[YL_F_S마켓차비율] < 30))) 
                        s->cur[YL_F_S복합압축조건] = 1;
                    if ((s->cur[YL_F_S직전유효] == 1)) 
                        s->cur[YL_F_S가격압축이탈] = s->cur[YL_F_S현재위치];
                }
            s->cur[YL_F_S신호합계] = 0;
            s->cur[YL_F_S복합압축표시] = 0;
            if ((s->cur[YL_F_S현재위치] == (-1))) 
                s->cur[YL_F_S신호합계] = (s->cur[YL_F_S신호합계] + 1);
            else if ((s->cur[YL_F_S현재위치] == 1)) 
                s->cur[YL_F_S신호합계] = (s->cur[YL_F_S신호합계] - 1);
            if (((s->cur[YL_F_FX초기화] == 0) && (s->cur[YL_F_S삼선준비] == 1)))                 {
                    if ((yl_lag(s, YL_F_S첫이탈, 1) == 1)) 
                        s->cur[YL_F_S신호합계] = (s->cur[YL_F_S신호합계] - 1);
                    if ((yl_lag(s, YL_F_S첫이탈, 1) == (-1))) 
                        s->cur[YL_F_S신호합계] = (s->cur[YL_F_S신호합계] + 1);
                }
            if (((s->cur[YL_F_FX초기화] == 0) && (s->cur[YL_F_S함수가능] == 1)))                 {
                    if ((yl_lag(s, YL_F_S가격압축이탈, 1) == 1)) 
                        s->cur[YL_F_S신호합계] = (s->cur[YL_F_S신호합계] - 1);
                    if ((yl_lag(s, YL_F_S가격압축이탈, 1) == (-1))) 
                        s->cur[YL_F_S신호합계] = (s->cur[YL_F_S신호합계] + 1);
                    if ((yl_lag(s, YL_F_S복합압축조건, 1) == 1)) 
                        s->cur[YL_F_S복합압축표시] = 1;
                }
            s->cur[YL_F_S비율점수] = 0;
            if ((((s->cur[YL_F_S삼선준비] == 1) && (s->cur[YL_F_S두구간최대] > 0)) && (s->cur[YL_F_S두구간비율] < 40))) 
                s->cur[YL_F_S비율점수] = (s->cur[YL_F_S비율점수] + 1);
            if (((s->cur[YL_F_S기준선준비] == 1) && (s->cur[YL_F_S평탄회귀차비율] < 30))) 
                s->cur[YL_F_S비율점수] = (s->cur[YL_F_S비율점수] + 1);
            if (((s->cur[YL_F_S마켓준비] == 1) && (s->cur[YL_F_S마켓차비율] < 30))) 
                s->cur[YL_F_S비율점수] = (s->cur[YL_F_S비율점수] + 1);
            if (((((((s->cur[YL_F_S삼선준비] == 1) && (s->cur[YL_F_S두구간최대] > 0)) && (s->cur[YL_F_S두구간비율] > 80))) || (((s->cur[YL_F_S기준선준비] == 1) && (s->cur[YL_F_S평탄회귀차비율] > 80)))) || (((s->cur[YL_F_S마켓준비] == 1) && (s->cur[YL_F_S마켓차비율] > 80))))) 
                s->cur[YL_F_S비율점수] = 0;
            if ((((s->cur[YL_F_S삼선준비] == 1) && (s->cur[YL_F_S두구간최대] > 0)) && (s->cur[YL_F_S두구간비율] < 40))) 
                s->cur[YL_F_S비율점수] = yl_max(1, s->cur[YL_F_S비율점수]);
            s->cur[YL_F_S삼선회색] = 0;
            if (((((s->cur[YL_F_S삼선준비] == 1) && (s->cur[YL_F_S신호합계] == 0)) && (s->cur[YL_F_S복합압축표시] == 0)) && (s->cur[YL_F_S비율점수] == 0))) 
                s->cur[YL_F_S삼선회색] = 1;
            if (((((((((((s->cur[YL_F_선행매수하락감싸기삼선] == 1) && (s->cur[YL_F_선행방향] == 1)) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (s->cur[YL_F_핵심지난상승최저가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] > 0)) && (s->cur[YL_F_핵심지난하락최저가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] <= s->cur[YL_F_핵심지난상승최고가])) && (s->cur[YL_F_핵심지난하락최저가] >= s->cur[YL_F_핵심지난상승최저가])) && (b->low < s->cur[YL_F_핵심지난상승최고가])) && (s->cur[YL_F_S삼선비율] >= 100))) 
                s->cur[YL_F_선행방향] = 0;
            s->cur[YL_F_압축해제] = yl_WSF_FXSqueezeV1_CO(s, b);
            s->cur[YL_F_하락폭비율경험] = 0;
            if ((((((s->cur[YL_F_조정매도기울기기준] < 0) && (s->cur[YL_F_조정표시] == 1)) && (s->cur[YL_F_조정신호방향] == (-1))) && (s->cur[YL_F_선행유효] == 1)) && (s->cur[YL_F_선행기울기Cnt] < s->cur[YL_F_조정매도기울기기준])))                 {
                    for (s->cur[YL_F_폭비율k] = (0); s->cur[YL_F_폭비율k] <= (((-s->cur[YL_F_선행기울기Cnt]) - 1)); s->cur[YL_F_폭비율k] += 1)                         {
                            if ((yl_lag(s, YL_F_압축폭비율, s->cur[YL_F_폭비율k]) >= s->cur[YL_F_조정매도폭비율기준])) 
                                s->cur[YL_F_하락폭비율경험] = 1;
                        }
                    if ((s->cur[YL_F_하락폭비율경험] == 1)) 
                        s->cur[YL_F_조정표시] = 0;
                }
            if (((((((((((s->cur[YL_F_조정매수감싸기금지] == 1) && (s->cur[YL_F_조정표시] == 1)) && (s->cur[YL_F_조정신호방향] == 1)) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (s->cur[YL_F_핵심지난상승최저가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] > 0)) && (s->cur[YL_F_핵심지난하락최저가] > 0)) && (s->cur[YL_F_핵심지난상승최고가] >= s->cur[YL_F_핵심지난하락최고가])) && (s->cur[YL_F_핵심지난상승최저가] <= s->cur[YL_F_핵심지난하락최저가])) && (b->close < s->cur[YL_F_핵심지난상승최고가]))) 
                s->cur[YL_F_조정표시] = 0;
            if (((((s->cur[YL_F_조정매수스윙폭제한] > 0) && (s->cur[YL_F_조정표시] == 1)) && (s->cur[YL_F_조정신호방향] == 1)) && (s->cur[YL_F_스윙전체폭비] >= s->cur[YL_F_조정매수스윙폭제한]))) 
                s->cur[YL_F_조정표시] = 0;
            if (((((((((((s->cur[YL_F_조정매수하락감싸기금지] == 1) && (s->cur[YL_F_조정표시] == 1)) && (s->cur[YL_F_조정신호방향] == 1)) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (s->cur[YL_F_핵심지난상승최저가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] > 0)) && (s->cur[YL_F_핵심지난하락최저가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] >= s->cur[YL_F_핵심지난상승최고가])) && (s->cur[YL_F_핵심지난하락최저가] <= s->cur[YL_F_핵심지난상승최저가])) && (b->close < s->cur[YL_F_핵심지난상승최고가]))) 
                s->cur[YL_F_조정표시] = 0;
            if ((((((((((((s->cur[YL_F_조정매수감싸기삼선] == 1) && (s->cur[YL_F_조정표시] == 1)) && (s->cur[YL_F_조정신호방향] == 1)) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (s->cur[YL_F_핵심지난상승최저가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] > 0)) && (s->cur[YL_F_핵심지난하락최저가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] <= s->cur[YL_F_핵심지난상승최고가])) && (s->cur[YL_F_핵심지난하락최저가] >= s->cur[YL_F_핵심지난상승최저가])) && (b->low < s->cur[YL_F_핵심지난상승최고가])) && (s->cur[YL_F_S삼선비율] >= 100))) 
                s->cur[YL_F_조정표시] = 0;
            if (((s->cur[YL_F_조정표시] == 1) && (((s->cur[YL_F_본장전구간] == 1) && (((((s->cur[YL_F_조정신호방향] == 1) && (s->cur[YL_F_통합상태] != 100))) || (((s->cur[YL_F_조정신호방향] == (-1)) && (s->cur[YL_F_통합상태] != (-100)))))))))) 
                s->cur[YL_F_조정표시] = 0;
            if (((((s->cur[YL_F_조정표시] == 1) && (s->cur[YL_F_조정신호방향] == (-1))) && (s->cur[YL_F_조정신호종류] == 1)) && (s->cur[YL_F_조정신호등급] == 2))) 
                s->cur[YL_F_조정표시] = 0;
            if (((((s->cur[YL_F_조정표시] == 1) && (s->cur[YL_F_조정신호방향] == 1)) && (s->cur[YL_F_조정신호종류] == 2)) && (s->cur[YL_F_조정신호등급] == 3))) 
                s->cur[YL_F_조정표시] = 0;
            if ((((((((s->cur[YL_F_조정A중하락구조금지] == 1) && (s->cur[YL_F_조정표시] == 1)) && (s->cur[YL_F_조정신호방향] == 1)) && (s->cur[YL_F_조정신호종류] == 1)) && (s->cur[YL_F_조정신호등급] == 2)) && (s->cur[YL_F_하락추세구조] == 1)) && (s->cur[YL_F_빨강구조] == 0))) 
                s->cur[YL_F_조정표시] = 0;
            if (((((s->cur[YL_F_조정표시] == 1) && (s->cur[YL_F_조정신호방향] == (-1))) && (s->cur[YL_F_조정신호종류] == 2)) && (s->cur[YL_F_조정신호등급] == 2))) 
                s->cur[YL_F_조정표시] = 0;
            if (((((s->cur[YL_F_조정표시] == 1) && (s->cur[YL_F_조정신호방향] == 1)) && (s->cur[YL_F_조정신호종류] == 1)) && (s->cur[YL_F_조정신호등급] == 2))) 
                s->cur[YL_F_조정표시] = 0;
            if ((((s->cur[YL_F_조정표시] == 1) && (s->cur[YL_F_조정신호방향] == (-1))) && (s->cur[YL_F_조정매도제한] == 1))) 
                s->cur[YL_F_조정표시] = 0;
            if ((((((((s->cur[YL_F_조정매수두구간평탄] == 1) && (s->cur[YL_F_조정표시] == 1)) && (s->cur[YL_F_조정신호방향] == 1)) && (s->cur[YL_F_S목표5] > 0)) && (s->cur[YL_F_S두구간비율] > s->cur[YL_F_조정매수두구간기준])) && (b->high > s->cur[YL_F_S목표5])) && (s->cur[YL_F_S평탄회귀차비율] > s->cur[YL_F_조정매수평탄기준]))) 
                s->cur[YL_F_조정표시] = 0;
            s->cur[YL_F_조정재개완료] = yl_lag(s, YL_F_조정재개완료, 1);
            if (((s->cur[YL_F_FX초기화] == 1) || (s->cur[YL_F_조정추세방향] != yl_lag(s, YL_F_조정추세방향, 1)))) 
                s->cur[YL_F_조정재개완료] = 0;
            if (((s->cur[YL_F_조정재개1회] == 1) && (s->cur[YL_F_조정재개완료] == 1))) 
                s->cur[YL_F_조정표시] = 0;
            if ((s->cur[YL_F_조정표시] == 1)) 
                s->cur[YL_F_조정재개완료] = 1;
            if (((((s->cur[YL_F_직전기울기제한] > 0) && (s->cur[YL_F_선행유효] == 1)) && (s->cur[YL_F_후보방향] != 0)) && (s->cur[YL_F_후보등급] <= s->cur[YL_F_제한등급])))                 {
                    if (((s->cur[YL_F_후보방향] == 1) && (s->cur[YL_F_선행직전상승기울기] >= s->cur[YL_F_직전기울기제한]))) 
                        s->cur[YL_F_후보방향] = 0;
                    if (((s->cur[YL_F_후보방향] == (-1)) && (s->cur[YL_F_선행직전하락기울기] >= s->cur[YL_F_직전기울기제한]))) 
                        s->cur[YL_F_후보방향] = 0;
                }
            if (((((((s->cur[YL_F_후보매도위치기준] < 0) && (s->cur[YL_F_후보방향] == (-1))) && (s->cur[YL_F_선행유효] == 1)) && (s->cur[YL_F_지속저장목표1] > 0)) && (s->cur[YL_F_선행위치Cnt] < s->cur[YL_F_후보매도위치기준])) && (b->low > s->cur[YL_F_지속저장목표1]))) 
                s->cur[YL_F_후보방향] = 0;
            if ((((s->cur[YL_F_후보매도폭비율기준] > 0) && (s->cur[YL_F_후보방향] == (-1))) && (s->cur[YL_F_압축폭비율] >= s->cur[YL_F_후보매도폭비율기준]))) 
                s->cur[YL_F_후보방향] = 0;
            if (((((((((((s->cur[YL_F_후보매도감싸기통합] == 1) && (s->cur[YL_F_후보방향] == (-1))) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (s->cur[YL_F_핵심지난상승최저가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] > 0)) && (s->cur[YL_F_핵심지난하락최저가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] <= s->cur[YL_F_핵심지난상승최고가])) && (s->cur[YL_F_핵심지난하락최저가] >= s->cur[YL_F_핵심지난상승최저가])) && (b->high > s->cur[YL_F_핵심지난상승최저가])) && (s->cur[YL_F_통합상태] > (-100)))) 
                s->cur[YL_F_후보방향] = 0;
            if ((((s->cur[YL_F_본장전구간] == 1) && (((((s->cur[YL_F_후보방향] == 1) && (s->cur[YL_F_통합상태] != 100))) || (((s->cur[YL_F_후보방향] == (-1)) && (s->cur[YL_F_통합상태] != (-100))))))))) 
                s->cur[YL_F_후보방향] = 0;
            if (((((s->cur[YL_F_후보B삼선상한] > 0) && (s->cur[YL_F_후보방향] != 0)) && (s->cur[YL_F_후보등급] == 2)) && (s->cur[YL_F_S삼선비율] >= s->cur[YL_F_후보B삼선상한]))) 
                s->cur[YL_F_후보방향] = 0;
            if (((s->cur[YL_F_후보방향] == (-1)) && (s->cur[YL_F_후보등급] == 2))) 
                s->cur[YL_F_후보방향] = 0;
            if (((((s->cur[YL_F_후보B두구간상한] > 0) && (s->cur[YL_F_후보방향] == 1)) && (s->cur[YL_F_후보등급] == 2)) && (s->cur[YL_F_S두구간비율] >= s->cur[YL_F_후보B두구간상한]))) 
                s->cur[YL_F_후보방향] = 0;
            if ((((s->cur[YL_F_후보S매수허용분] > 0) && (s->cur[YL_F_후보방향] == 1)) && (s->cur[YL_F_후보등급] == 5)))                 {
                    s->cur[YL_F_후보S경과분] = (s->cur[YL_F_봉시작분] - s->cur[YL_F_본장시작분]);
                    if ((s->cur[YL_F_후보S경과분] < 0)) 
                        s->cur[YL_F_후보S경과분] = (s->cur[YL_F_후보S경과분] + 1440);
                    if ((s->cur[YL_F_후보S경과분] >= s->cur[YL_F_후보S매수허용분])) 
                        s->cur[YL_F_후보방향] = 0;
                }
            if (((((s->cur[YL_F_후보S매수압축중금지] == 1) && (s->cur[YL_F_후보방향] == 1)) && (s->cur[YL_F_후보등급] == 5)) && (s->cur[YL_F_압축지속봉수] > 0))) 
                s->cur[YL_F_후보방향] = 0;
            if (((s->cur[YL_F_후보방향] == 1) && (s->cur[YL_F_후보등급] == 4))) 
                s->cur[YL_F_후보방향] = 0;
            if ((((s->cur[YL_F_후보S매도허용분] > 0) && (s->cur[YL_F_후보방향] == (-1))) && (s->cur[YL_F_후보등급] == 5)))                 {
                    s->cur[YL_F_후보S경과분] = (s->cur[YL_F_봉시작분] - s->cur[YL_F_본장시작분]);
                    if ((s->cur[YL_F_후보S경과분] < 0)) 
                        s->cur[YL_F_후보S경과분] = (s->cur[YL_F_후보S경과분] + 1440);
                    if ((s->cur[YL_F_후보S경과분] >= s->cur[YL_F_후보S매도허용분])) 
                        s->cur[YL_F_후보방향] = 0;
                }
            if ((((((s->cur[YL_F_후보B매수본장전금지] == 1) && (s->cur[YL_F_후보방향] == 1)) && (s->cur[YL_F_후보등급] == 2)) && (b->time >= 70000)) && (s->cur[YL_F_봉시작분] < s->cur[YL_F_본장시작분]))) 
                s->cur[YL_F_후보방향] = 0;
            if ((((((((s->cur[YL_F_돌파매수본장전폭비] > 0) && (s->cur[YL_F_후보방향] == 1)) && (s->cur[YL_F_후보등급] == 8)) && (s->cur[YL_F_압축폭비율] > s->cur[YL_F_돌파매수본장전폭비])) && (s->cur[YL_F_스윙전체폭비] > s->cur[YL_F_돌파매수본장전폭비])) && (b->time >= 70000)) && (s->cur[YL_F_봉시작분] < s->cur[YL_F_본장시작분]))) 
                s->cur[YL_F_후보방향] = 0;
            if ((((((s->cur[YL_F_후보A매수자정금지] == 1) && (s->cur[YL_F_후보방향] == 1)) && (s->cur[YL_F_후보등급] == 3)) && (s->cur[YL_F_본장기준봉분] >= (s->cur[YL_F_본장시작분] + 90))) && (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] + 150)))) 
                s->cur[YL_F_후보방향] = 0;
            if ((((((s->cur[YL_F_돌파매수초반금지] == 1) && (s->cur[YL_F_후보방향] == 1)) && (s->cur[YL_F_후보등급] == 8)) && (s->cur[YL_F_봉시작분] >= (s->cur[YL_F_본장시작분] - 90))) && (s->cur[YL_F_봉시작분] < (s->cur[YL_F_본장시작분] - 60)))) 
                s->cur[YL_F_후보방향] = 0;
            if ((((((s->cur[YL_F_돌파매도목표5여유] > 0) && (s->cur[YL_F_후보방향] == (-1))) && (s->cur[YL_F_후보등급] == 8)) && (s->cur[YL_F_지속저장목표5] > 0)) && ((s->cur[YL_F_지속저장목표5] - b->close) < s->cur[YL_F_돌파매도목표5여유]))) 
                s->cur[YL_F_후보방향] = 0;
            if (((((s->cur[YL_F_장대봉필터] == 1) && (s->cur[YL_F_후보방향] != 0)) && (s->cur[YL_F_선행진폭율] >= s->cur[YL_F_장대진폭율기준])) && (s->bar_n > (s->cur[YL_F_장대평균봉수] + s->cur[YL_F_장대확인봉수]))))                 {
                    s->cur[YL_F_장대평균폭] = 0;
                    for (s->cur[YL_F_장대k] = (s->cur[YL_F_장대확인봉수]); s->cur[YL_F_장대k] <= (((s->cur[YL_F_장대확인봉수] + s->cur[YL_F_장대평균봉수]) - 1)); s->cur[YL_F_장대k] += 1)                         {
                            s->cur[YL_F_장대평균폭] = (s->cur[YL_F_장대평균폭] + ((yl_px(s, 'H', s->cur[YL_F_장대k]) - yl_px(s, 'L', s->cur[YL_F_장대k]))));
                        }
                    s->cur[YL_F_장대평균폭] = (s->cur[YL_F_장대평균폭] / s->cur[YL_F_장대평균봉수]);
                    s->cur[YL_F_장대매수봉] = 0;
                    s->cur[YL_F_장대매도봉] = 0;
                    for (s->cur[YL_F_장대k] = (0); s->cur[YL_F_장대k] <= ((s->cur[YL_F_장대확인봉수] - 1)); s->cur[YL_F_장대k] += 1)                         {
                            s->cur[YL_F_장대범위] = (yl_px(s, 'H', s->cur[YL_F_장대k]) - yl_px(s, 'L', s->cur[YL_F_장대k]));
                            if (((s->cur[YL_F_장대평균폭] > 0) && (s->cur[YL_F_장대범위] >= (s->cur[YL_F_장대평균폭] * s->cur[YL_F_장대봉배수]))))                                 {
                                    if ((((yl_px(s, 'H', s->cur[YL_F_장대k]) - yl_max(yl_px(s, 'O', s->cur[YL_F_장대k]), yl_px(s, 'C', s->cur[YL_F_장대k]))) >= ((s->cur[YL_F_장대범위] * s->cur[YL_F_장대꼬리비율]) / 100)) || ((yl_px(s, 'C', s->cur[YL_F_장대k]) - yl_px(s, 'O', s->cur[YL_F_장대k])) >= ((s->cur[YL_F_장대범위] * s->cur[YL_F_장대몸통비율]) / 100)))) 
                                        s->cur[YL_F_장대매수봉] = 1;
                                    if ((((yl_min(yl_px(s, 'O', s->cur[YL_F_장대k]), yl_px(s, 'C', s->cur[YL_F_장대k])) - yl_px(s, 'L', s->cur[YL_F_장대k])) >= ((s->cur[YL_F_장대범위] * s->cur[YL_F_장대꼬리비율]) / 100)) || ((yl_px(s, 'O', s->cur[YL_F_장대k]) - yl_px(s, 'C', s->cur[YL_F_장대k])) >= ((s->cur[YL_F_장대범위] * s->cur[YL_F_장대몸통비율]) / 100)))) 
                                        s->cur[YL_F_장대매도봉] = 1;
                                }
                        }
                    if (((s->cur[YL_F_후보방향] == 1) && (s->cur[YL_F_장대매수봉] == 1))) 
                        s->cur[YL_F_후보방향] = 0;
                    if (((s->cur[YL_F_후보방향] == (-1)) && (s->cur[YL_F_장대매도봉] == 1))) 
                        s->cur[YL_F_후보방향] = 0;
                }
            if ((((((((((((s->cur[YL_F_후보매도감싸기삼선두구간] == 1) && (s->cur[YL_F_후보방향] == (-1))) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (s->cur[YL_F_핵심지난상승최저가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] > 0)) && (s->cur[YL_F_핵심지난하락최저가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] <= s->cur[YL_F_핵심지난상승최고가])) && (s->cur[YL_F_핵심지난하락최저가] >= s->cur[YL_F_핵심지난상승최저가])) && (s->cur[YL_F_통합상태] > (-100))) && (s->cur[YL_F_S삼선비율] >= 100)) && (s->cur[YL_F_S두구간비율] >= s->cur[YL_F_후보매도두구간기준]))) 
                s->cur[YL_F_후보방향] = 0;
            s->cur[YL_F_색방향] = s->cur[YL_F_통합방향];
            if ((s->cur[YL_F_통합방향] != 0))                 {
                    if ((((s->cur[YL_F_미래방향] != s->cur[YL_F_통합방향]) || (s->cur[YL_F_DI방향] != s->cur[YL_F_통합방향])) || ((s->cur[YL_F_매물대5상태] * s->cur[YL_F_통합방향]) < 0))) 
                        s->cur[YL_F_색방향] = 0;
                }
            if ((s->cur[YL_F_FX초기화] == 1))                 {
                    s->cur[YL_F_통합카운트] = 0;
                    s->cur[YL_F_통합마지막위치] = 0;
                    s->cur[YL_F_통합100발생] = 0;
                }
            if (((((s->cur[YL_F_통합방향] != 0) && (s->cur[YL_F_통합방향] != s->cur[YL_F_통합마지막위치])) && (s->cur[YL_F_본장압축해제직전구간기준] > 0)) && (fabs(s->cur[YL_F_통합카운트]) >= s->cur[YL_F_본장압축해제직전구간기준]))) 
                s->cur[YL_F_직전주요통합방향] = ((s->cur[YL_F_통합카운트] > 0) ? 1 : (-1));
            if (((s->cur[YL_F_통합방향] != 0) && (s->cur[YL_F_통합방향] != s->cur[YL_F_통합마지막위치]))) 
                s->cur[YL_F_직전통합카운트값] = s->cur[YL_F_통합카운트];
            if (((s->cur[YL_F_통합방향] != 0) && (s->cur[YL_F_통합방향] != s->cur[YL_F_통합마지막위치])))                 {
                    s->cur[YL_F_통합카운트] = 0;
                    s->cur[YL_F_통합마지막위치] = s->cur[YL_F_통합방향];
                    s->cur[YL_F_통합100발생] = 0;
                }
            if ((s->cur[YL_F_색방향] != 0)) 
                s->cur[YL_F_통합카운트] = (s->cur[YL_F_통합카운트] + s->cur[YL_F_색방향]);
            if ((s->cur[YL_F_통합상태] >= 100)) 
                s->cur[YL_F_통합100발생] = 1;
            yl_WSF_FXSessionProfileV2_CO(s, b);
            s->cur[YL_F_압축위치구분] = 0;
            if (((s->cur[YL_F_P2유효] == 1) && (s->cur[YL_F_P2상단1] > s->cur[YL_F_P2하단1])))                 {
                    s->cur[YL_F_압축박스위치] = (((b->close - s->cur[YL_F_P2하단1])) / ((s->cur[YL_F_P2상단1] - s->cur[YL_F_P2하단1])));
                    if ((s->cur[YL_F_압축박스위치] >= (2 / 3))) 
                        s->cur[YL_F_압축위치구분] = 1;
                    else if ((s->cur[YL_F_압축박스위치] <= (1 / 3))) 
                        s->cur[YL_F_압축위치구분] = (-1);
                }
            s->cur[YL_F_전환압축빨강수] = 0;
            s->cur[YL_F_전환압축파랑수] = 0;
            for (s->cur[YL_F_전환c] = (0); s->cur[YL_F_전환c] <= ((s->cur[YL_F_전환압축색봉수] - 1)); s->cur[YL_F_전환c] += 1)                 {
                    if (((yl_lag(s, YL_F_압축지속봉수, s->cur[YL_F_전환c]) > 0) && (yl_lag(s, YL_F_압축위치구분, s->cur[YL_F_전환c]) == 1))) 
                        s->cur[YL_F_전환압축빨강수] = (s->cur[YL_F_전환압축빨강수] + 1);
                    if (((yl_lag(s, YL_F_압축지속봉수, s->cur[YL_F_전환c]) > 0) && (yl_lag(s, YL_F_압축위치구분, s->cur[YL_F_전환c]) == (-1)))) 
                        s->cur[YL_F_전환압축파랑수] = (s->cur[YL_F_전환압축파랑수] + 1);
                }
            s->cur[YL_F_전환매수경과] = (yl_lag(s, YL_F_전환매수경과, 1) + 1);
            s->cur[YL_F_전환매도경과] = (yl_lag(s, YL_F_전환매도경과, 1) + 1);
            if ((s->cur[YL_F_FX초기화] == 1))                 {
                    s->cur[YL_F_전환매수경과] = 999;
                    s->cur[YL_F_전환매도경과] = 999;
                }
            s->cur[YL_F_전환매수] = 0;
            if ((((((((((((s->cur[YL_F_전환매수사용] == 1) && (s->cur[YL_F_선행유효] == 1)) && (s->cur[YL_F_FX초기화] == 0)) && (s->cur[YL_F_선행기울기Cnt] == 1)) && (yl_lag(s, YL_F_선행기울기Cnt, 1) <= 0)) && (s->cur[YL_F_선행위치Cnt] <= 0)) && (s->cur[YL_F_선행위치Cnt] >= s->cur[YL_F_전환위치하한])) && (s->cur[YL_F_통합상태] > (-100))) && (s->cur[YL_F_핵심지난하락최저가] > 0)) && (s->cur[YL_F_선행직전하락기울기] > 0)) && (((s->cur[YL_F_전환스윙폭상한] <= 0) || (s->cur[YL_F_스윙전체폭비] < s->cur[YL_F_전환스윙폭상한])))))                 {
                    s->cur[YL_F_전환직전저가] = yl_px(s, 'L', 1);
                    for (s->cur[YL_F_전환k] = (1); s->cur[YL_F_전환k] <= (s->cur[YL_F_선행직전하락기울기]); s->cur[YL_F_전환k] += 1)                         {
                            s->cur[YL_F_전환직전저가] = yl_min(s->cur[YL_F_전환직전저가], yl_px(s, 'L', s->cur[YL_F_전환k]));
                        }
                    if ((s->cur[YL_F_전환직전저가] > s->cur[YL_F_핵심지난하락최저가])) 
                        s->cur[YL_F_전환매수] = 1;
                }
            if ((((s->cur[YL_F_전환매수] == 1) && (s->cur[YL_F_본장전구간] == 1)) && (s->cur[YL_F_통합상태] != 100))) 
                s->cur[YL_F_전환매수] = 0;
            if ((((s->cur[YL_F_전환매수] == 1) && (s->cur[YL_F_전환두구간상한] > 0)) && (s->cur[YL_F_S두구간비율] > s->cur[YL_F_전환두구간상한]))) 
                s->cur[YL_F_전환매수] = 0;
            s->cur[YL_F_전환직전상승초과] = 0;
            if (((s->cur[YL_F_전환제외사용] == 1) && (s->cur[YL_F_전환매수] == 1)))                 {
                    for (s->cur[YL_F_전환j] = (0); s->cur[YL_F_전환j] <= ((s->cur[YL_F_전환직전상승기간] - 1)); s->cur[YL_F_전환j] += 1)                         {
                            if ((yl_lag(s, YL_F_선행직전상승기울기, s->cur[YL_F_전환j]) > s->cur[YL_F_전환직전상승제한])) 
                                s->cur[YL_F_전환직전상승초과] = 1;
                        }
                    if ((((s->cur[YL_F_통합상태] < 0) || (s->cur[YL_F_압축지속봉수] >= s->cur[YL_F_전환압축봉수제한])) || (s->cur[YL_F_전환직전상승초과] == 1))) 
                        s->cur[YL_F_전환매수] = 0;
                }
            s->cur[YL_F_전환매도] = 0;
            if ((((((((((((s->cur[YL_F_전환매도사용] == 1) && (s->cur[YL_F_선행유효] == 1)) && (s->cur[YL_F_FX초기화] == 0)) && (s->cur[YL_F_선행기울기Cnt] == (-1))) && (yl_lag(s, YL_F_선행기울기Cnt, 1) >= 0)) && (s->cur[YL_F_선행위치Cnt] >= 0)) && (s->cur[YL_F_선행위치Cnt] <= (-s->cur[YL_F_전환위치하한]))) && (s->cur[YL_F_통합상태] < 100)) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (s->cur[YL_F_선행직전상승기울기] > 0)) && (((s->cur[YL_F_전환스윙폭상한] <= 0) || (s->cur[YL_F_스윙전체폭비] < s->cur[YL_F_전환스윙폭상한])))))                 {
                    s->cur[YL_F_전환직전고가] = yl_px(s, 'H', 1);
                    for (s->cur[YL_F_전환k] = (1); s->cur[YL_F_전환k] <= (s->cur[YL_F_선행직전상승기울기]); s->cur[YL_F_전환k] += 1)                         {
                            s->cur[YL_F_전환직전고가] = yl_max(s->cur[YL_F_전환직전고가], yl_px(s, 'H', s->cur[YL_F_전환k]));
                        }
                    if ((s->cur[YL_F_전환직전고가] < s->cur[YL_F_핵심지난상승최고가])) 
                        s->cur[YL_F_전환매도] = 1;
                }
            if ((((s->cur[YL_F_전환매도] == 1) && (s->cur[YL_F_본장전구간] == 1)) && (s->cur[YL_F_통합상태] != (-100)))) 
                s->cur[YL_F_전환매도] = 0;
            if ((((s->cur[YL_F_전환매도] == 1) && (s->cur[YL_F_전환두구간상한] > 0)) && (s->cur[YL_F_S두구간비율] > s->cur[YL_F_전환두구간상한]))) 
                s->cur[YL_F_전환매도] = 0;
            s->cur[YL_F_전환직전하락초과] = 0;
            if (((s->cur[YL_F_전환제외사용] == 1) && (s->cur[YL_F_전환매도] == 1)))                 {
                    for (s->cur[YL_F_전환j] = (0); s->cur[YL_F_전환j] <= ((s->cur[YL_F_전환직전상승기간] - 1)); s->cur[YL_F_전환j] += 1)                         {
                            if ((yl_lag(s, YL_F_선행직전하락기울기, s->cur[YL_F_전환j]) > s->cur[YL_F_전환직전상승제한])) 
                                s->cur[YL_F_전환직전하락초과] = 1;
                        }
                    if ((((s->cur[YL_F_통합상태] > 0) || (s->cur[YL_F_압축지속봉수] >= s->cur[YL_F_전환압축봉수제한])) || (s->cur[YL_F_전환직전하락초과] == 1))) 
                        s->cur[YL_F_전환매도] = 0;
                }
            if (((s->cur[YL_F_전환삼선회색금지] == 1) && (s->cur[YL_F_S삼선회색] == 1)))                 {
                    s->cur[YL_F_전환매도] = 0;
                }
            if ((((s->cur[YL_F_전환압축색봉수] > 0) && (s->cur[YL_F_전환매도] == 1)) && ((s->cur[YL_F_전환압축빨강수] * 2) > s->cur[YL_F_전환압축색봉수]))) 
                s->cur[YL_F_전환매도] = 0;
            if ((((s->cur[YL_F_전환반대금지봉수] > 0) && (s->cur[YL_F_전환매도] == 1)) && (s->cur[YL_F_전환매수경과] <= s->cur[YL_F_전환반대금지봉수]))) 
                s->cur[YL_F_전환매도] = 0;
            if ((((s->cur[YL_F_전환매도통합기준] == 1) && (s->cur[YL_F_전환매도] == 1)) && (s->cur[YL_F_통합상태] != (-50)))) 
                s->cur[YL_F_전환매도] = 0;
            if (((s->cur[YL_F_전환매도직전100봉수] > 0) && (s->cur[YL_F_전환매도] == 1)))                 {
                    for (s->cur[YL_F_전환k] = (1); s->cur[YL_F_전환k] <= (s->cur[YL_F_전환매도직전100봉수]); s->cur[YL_F_전환k] += 1)                         {
                            if ((yl_lag(s, YL_F_통합상태, s->cur[YL_F_전환k]) == (-100))) 
                                s->cur[YL_F_전환매도] = 0;
                        }
                }
            if (((s->cur[YL_F_전환매수직전100봉수] > 0) && (s->cur[YL_F_전환매수] == 1)))                 {
                    for (s->cur[YL_F_전환k] = (1); s->cur[YL_F_전환k] <= (s->cur[YL_F_전환매수직전100봉수]); s->cur[YL_F_전환k] += 1)                         {
                            if (((yl_lag(s, YL_F_통합상태, s->cur[YL_F_전환k]) == 100) || (yl_lag(s, YL_F_통합상태, s->cur[YL_F_전환k]) == (-100)))) 
                                s->cur[YL_F_전환매수] = 0;
                        }
                }
            if ((s->cur[YL_F_전환매수] == 1)) 
                s->cur[YL_F_전환매수경과] = 0;
            if ((s->cur[YL_F_전환매도] == 1)) 
                s->cur[YL_F_전환매도경과] = 0;
            s->cur[YL_F_신호방향] = 0;
            s->cur[YL_F_신호종류] = 0;
            if (((s->cur[YL_F_조정표시] == 1) && (s->cur[YL_F_조정신호방향] != 0)))                 {
                    s->cur[YL_F_신호방향] = s->cur[YL_F_조정신호방향];
                    s->cur[YL_F_신호종류] = 4;
                }
            if ((s->cur[YL_F_전환매수] == 1))                 {
                    s->cur[YL_F_신호방향] = 1;
                    s->cur[YL_F_신호종류] = 5;
                }
            else if ((s->cur[YL_F_전환매도] == 1))                 {
                    s->cur[YL_F_신호방향] = (-1);
                    s->cur[YL_F_신호종류] = 5;
                }
            if ((s->cur[YL_F_선행방향] != 0))                 {
                    s->cur[YL_F_신호방향] = s->cur[YL_F_선행방향];
                    s->cur[YL_F_신호종류] = 3;
                }
            if ((s->cur[YL_F_구조돌파방향] != 0))                 {
                    s->cur[YL_F_신호방향] = s->cur[YL_F_구조돌파방향];
                    s->cur[YL_F_신호종류] = 2;
                }
            if ((s->cur[YL_F_후보방향] != 0))                 {
                    s->cur[YL_F_신호방향] = s->cur[YL_F_후보방향];
                    s->cur[YL_F_신호종류] = 1;
                }
            if (((((s->cur[YL_F_본장초반금지분] > 0) && (s->cur[YL_F_신호방향] != 0)) && (s->cur[YL_F_봉시작분] >= s->cur[YL_F_본장시작분])) && (s->cur[YL_F_봉시작분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_본장초반금지분]))))                 {
                    if ((s->cur[YL_F_신호종류] == 5)) 
                        s->cur[YL_F_신호방향] = 0;
                    if (((((s->cur[YL_F_신호종류] == 4) && (s->cur[YL_F_신호방향] == 1)) && (s->cur[YL_F_조정신호종류] == 2)) && (((s->cur[YL_F_조정신호등급] == 1) || (s->cur[YL_F_조정신호등급] == 2))))) 
                        s->cur[YL_F_신호방향] = 0;
                }
            s->cur[YL_F_매매시간] = 0;
            if ((s->cur[YL_F_본장기준매매] == 1))                 {
                    if (((s->cur[YL_F_본장기준봉분] >= (s->cur[YL_F_본장시작분] - s->cur[YL_F_매매시작본장전분])) && (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_매매종료본장후분])))) 
                        s->cur[YL_F_매매시간] = 1;
                }
            else if ((s->cur[YL_F_매매시작시각] > s->cur[YL_F_매매종료시각]))                 {
                    if (((b->time >= s->cur[YL_F_매매시작시각]) || (b->time < s->cur[YL_F_매매종료시각]))) 
                        s->cur[YL_F_매매시간] = 1;
                }
            else                 {
                    if (((b->time >= s->cur[YL_F_매매시작시각]) && (b->time < s->cur[YL_F_매매종료시각]))) 
                        s->cur[YL_F_매매시간] = 1;
                }
            s->cur[YL_F_가상시작] = s->cur[YL_F_가상포지션];
            s->cur[YL_F_가상매수진입] = 0;
            s->cur[YL_F_가상매도진입] = 0;
            if ((s->cur[YL_F_가상포지션] != 0))                 {
                    s->cur[YL_F_가상최고가] = yl_max(s->cur[YL_F_가상최고가], b->high);
                    s->cur[YL_F_가상최저가] = yl_min(s->cur[YL_F_가상최저가], b->low);
                    if (((s->cur[YL_F_마감후기록보정] == 0) || (s->cur[YL_F_매매시간] == 1)))                         {
                            s->cur[YL_F_장상태최고가] = yl_max(s->cur[YL_F_장상태최고가], b->high);
                            s->cur[YL_F_장상태최저가] = yl_min(s->cur[YL_F_장상태최저가], b->low);
                        }
                }
            if ((((b->market == 1) && (s->cur[YL_F_색방향] == (-1))) && (yl_lag(s, YL_F_색방향, 1) != (-1)))) 
                yl_order(s, TR_YL_EXIT_LONG, TR_YL_ONCLOSE, 0, (int)(0), "색청산매수");
            if ((((b->market == (-1)) && (s->cur[YL_F_색방향] == 1)) && (yl_lag(s, YL_F_색방향, 1) != 1))) 
                yl_order(s, TR_YL_EXIT_SHORT, TR_YL_ONCLOSE, 0, (int)(0), "색청산매도");
            s->cur[YL_F_시간청산조건] = 0;
            if (((s->cur[YL_F_매매종료시간청산] == 1) && (s->cur[YL_F_매매시간] == 0))) 
                s->cur[YL_F_시간청산조건] = 1;
            if ((((s->cur[YL_F_장마감청산본장후분] > 0) && (s->cur[YL_F_매매시간] == 0)) && (s->cur[YL_F_본장기준봉분] >= (s->cur[YL_F_본장시작분] + s->cur[YL_F_장마감청산본장후분])))) 
                s->cur[YL_F_시간청산조건] = 1;
            if ((s->cur[YL_F_매매시간] == 1)) 
                s->cur[YL_F_마감기준가] = 0;
            else if ((((s->bar_n > 1) && (yl_lag(s, YL_F_매매시간, 1) == 1)) && (s->cur[YL_F_가상시작] != 0)))                 {
                    if ((s->cur[YL_F_가상시작] == 1)) 
                        s->cur[YL_F_마감기준가] = yl_max(s->cur[YL_F_가상진입가], b->close);
                    else 
                        s->cur[YL_F_마감기준가] = yl_min(s->cur[YL_F_가상진입가], b->close);
                }
            if (((((s->cur[YL_F_마감후본전청산] == 1) && (s->cur[YL_F_매매시간] == 0)) && (s->cur[YL_F_가상시작] == 1)) && (b->close < ((s->cur[YL_F_마감기준가] > 0) ? s->cur[YL_F_마감기준가] : s->cur[YL_F_가상진입가])))) 
                s->cur[YL_F_시간청산조건] = 1;
            if (((((s->cur[YL_F_마감후본전청산] == 1) && (s->cur[YL_F_매매시간] == 0)) && (s->cur[YL_F_가상시작] == (-1))) && (b->close > ((s->cur[YL_F_마감기준가] > 0) ? s->cur[YL_F_마감기준가] : s->cur[YL_F_가상진입가])))) 
                s->cur[YL_F_시간청산조건] = 1;
            if ((s->cur[YL_F_가상시작] == 0)) 
                s->cur[YL_F_보유중압축해제] = 0;
            else if (((s->bar_n > s->cur[YL_F_가상진입봉]) && (s->cur[YL_F_압축지속봉수] == 0))) 
                s->cur[YL_F_보유중압축해제] = 1;
            s->cur[YL_F_현재전환수] = s->cur[YL_F_수익전환수];
            if (((((s->cur[YL_F_가상시작] != 0) && (s->bar_n > s->cur[YL_F_가상진입봉])) && ((((b->close - s->cur[YL_F_가상진입가])) * s->cur[YL_F_가상시작]) > 0)) && (s->cur[YL_F_직전봉평가손익] <= 0))) 
                s->cur[YL_F_현재전환수] = (s->cur[YL_F_수익전환수] + 1);
            s->cur[YL_F_압축재개청산] = 0;
            if ((((((((((s->cur[YL_F_압축재개청산전환수] > 0) && (s->cur[YL_F_가상시작] == 1)) && (s->cur[YL_F_가상진입번호] >= 7)) && (s->cur[YL_F_가상진입번호] <= 9)) && ((s->cur[YL_F_최종청산손익_매수] + s->cur[YL_F_최종청산손익_매도]) > 0)) && (s->cur[YL_F_보유중압축해제] == 1)) && (s->cur[YL_F_압축지속봉수] > 0)) && (s->cur[YL_F_현재전환수] >= s->cur[YL_F_압축재개청산전환수])) && (b->close > s->cur[YL_F_가상진입가]))) 
                s->cur[YL_F_압축재개청산] = 1;
            if (((((((((((s->cur[YL_F_압축재개청산전환수] > 0) && (s->cur[YL_F_압축재개매도사용] == 1)) && (s->cur[YL_F_가상시작] == (-1))) && (s->cur[YL_F_가상진입번호] >= 10)) && (s->cur[YL_F_가상진입번호] <= 12)) && ((s->cur[YL_F_최종청산손익_매수] + s->cur[YL_F_최종청산손익_매도]) > 0)) && (s->cur[YL_F_보유중압축해제] == 1)) && (s->cur[YL_F_압축지속봉수] > 0)) && (s->cur[YL_F_현재전환수] >= s->cur[YL_F_압축재개청산전환수])) && (b->close < s->cur[YL_F_가상진입가]))) 
                s->cur[YL_F_압축재개청산] = (-1);
            if (((s->cur[YL_F_시간청산조건] == 1) && (b->market == 1))) 
                yl_order(s, TR_YL_EXIT_LONG, TR_YL_ONCLOSE, 0, (int)(0), "시간청산매수");
            if (((s->cur[YL_F_시간청산조건] == 1) && (b->market == (-1)))) 
                yl_order(s, TR_YL_EXIT_SHORT, TR_YL_ONCLOSE, 0, (int)(0), "시간청산매도");
            s->cur[YL_F_수익보호조건] = 0;
            if (((((((s->cur[YL_F_수익보호기준틱] > 0) && (s->cur[YL_F_수익보호선틱] > 0)) && (s->cur[YL_F_가상시작] == 1)) && (b->market == 1)) && (s->cur[YL_F_가상최고가] >= (s->cur[YL_F_가상진입가] + (s->cur[YL_F_수익보호기준틱] * b->price_scale)))) && (b->close <= (s->cur[YL_F_가상진입가] + (s->cur[YL_F_수익보호선틱] * b->price_scale))))) 
                s->cur[YL_F_수익보호조건] = 1;
            if (((((((s->cur[YL_F_수익보호기준틱] > 0) && (s->cur[YL_F_수익보호선틱] > 0)) && (s->cur[YL_F_가상시작] == (-1))) && (b->market == (-1))) && (s->cur[YL_F_가상최저가] <= (s->cur[YL_F_가상진입가] - (s->cur[YL_F_수익보호기준틱] * b->price_scale)))) && (b->close >= (s->cur[YL_F_가상진입가] - (s->cur[YL_F_수익보호선틱] * b->price_scale))))) 
                s->cur[YL_F_수익보호조건] = 1;
            if (((((((((s->cur[YL_F_수익보호스톱] == 1) && (s->cur[YL_F_수익보호기준틱] > 0)) && (s->cur[YL_F_수익보호선틱] > 0)) && (s->cur[YL_F_가상시작] == 1)) && (s->bar_n > s->cur[YL_F_가상진입봉])) && (((b->market == 1) || (yl_lag(s, YL_F_MarketPosition, 1) == 1)))) && (yl_lag(s, YL_F_가상최고가, 1) >= (s->cur[YL_F_가상진입가] + (s->cur[YL_F_수익보호기준틱] * b->price_scale)))) && (b->low <= (s->cur[YL_F_가상진입가] + (s->cur[YL_F_수익보호선틱] * b->price_scale))))) 
                s->cur[YL_F_수익보호조건] = 1;
            if (((((((((s->cur[YL_F_수익보호스톱] == 1) && (s->cur[YL_F_수익보호기준틱] > 0)) && (s->cur[YL_F_수익보호선틱] > 0)) && (s->cur[YL_F_가상시작] == (-1))) && (s->bar_n > s->cur[YL_F_가상진입봉])) && (((b->market == (-1)) || (yl_lag(s, YL_F_MarketPosition, 1) == (-1))))) && (yl_lag(s, YL_F_가상최저가, 1) <= (s->cur[YL_F_가상진입가] - (s->cur[YL_F_수익보호기준틱] * b->price_scale)))) && (b->high >= (s->cur[YL_F_가상진입가] - (s->cur[YL_F_수익보호선틱] * b->price_scale))))) 
                s->cur[YL_F_수익보호조건] = 1;
            if ((s->cur[YL_F_가상시작] == 0))                 {
                    s->cur[YL_F_일차도달] = 0;
                    s->cur[YL_F_일차도달틱] = 0;
                }
            if ((((((s->cur[YL_F_가상시작] == 1) && (s->bar_n > s->cur[YL_F_가상진입봉])) && (s->cur[YL_F_일차도달] == 0)) && (yl_lag(s, YL_F_현재1차틱, 1) > 0)) && (b->high >= (s->cur[YL_F_가상진입가] + (yl_lag(s, YL_F_현재1차틱, 1) * b->price_scale)))))                 {
                    s->cur[YL_F_일차도달] = 1;
                    s->cur[YL_F_일차도달틱] = yl_lag(s, YL_F_현재1차틱, 1);
                }
            if ((((((s->cur[YL_F_가상시작] == (-1)) && (s->bar_n > s->cur[YL_F_가상진입봉])) && (s->cur[YL_F_일차도달] == 0)) && (yl_lag(s, YL_F_현재1차틱, 1) > 0)) && (b->low <= (s->cur[YL_F_가상진입가] - (yl_lag(s, YL_F_현재1차틱, 1) * b->price_scale)))))                 {
                    s->cur[YL_F_일차도달] = 1;
                    s->cur[YL_F_일차도달틱] = yl_lag(s, YL_F_현재1차틱, 1);
                }
            if ((((((((s->cur[YL_F_일차후본전보장] == 1) && (yl_lag(s, YL_F_일차도달, 1) == 1)) && (yl_lag(s, YL_F_일차도달틱, 1) > 0)) && (s->cur[YL_F_가상시작] == 1)) && (s->bar_n > s->cur[YL_F_가상진입봉])) && (((b->market == 1) || (yl_lag(s, YL_F_MarketPosition, 1) == 1)))) && (b->low <= (s->cur[YL_F_가상진입가] - (((yl_lag(s, YL_F_일차도달틱, 1) * b->price_scale) - s->cur[YL_F_일차후본전여유pt])))))) 
                s->cur[YL_F_수익보호조건] = 1;
            if ((((((((s->cur[YL_F_일차후본전보장] == 1) && (yl_lag(s, YL_F_일차도달, 1) == 1)) && (yl_lag(s, YL_F_일차도달틱, 1) > 0)) && (s->cur[YL_F_가상시작] == (-1))) && (s->bar_n > s->cur[YL_F_가상진입봉])) && (((b->market == (-1)) || (yl_lag(s, YL_F_MarketPosition, 1) == (-1))))) && (b->high >= (s->cur[YL_F_가상진입가] + (((yl_lag(s, YL_F_일차도달틱, 1) * b->price_scale) - s->cur[YL_F_일차후본전여유pt])))))) 
                s->cur[YL_F_수익보호조건] = 1;
            if (((s->cur[YL_F_수익보호조건] == 1) && (b->market == 1))) 
                yl_order(s, TR_YL_EXIT_LONG, TR_YL_ONCLOSE, 0, (int)(0), "수익보호청산매수");
            if (((s->cur[YL_F_수익보호조건] == 1) && (b->market == (-1)))) 
                yl_order(s, TR_YL_EXIT_SHORT, TR_YL_ONCLOSE, 0, (int)(0), "수익보호청산매도");
            s->cur[YL_F_재진입청산조건] = 0;
            if ((((((((s->cur[YL_F_재진입표시] == 1) && (s->cur[YL_F_가상시작] == (-1))) && (b->market == (-1))) && (s->bar_n > s->cur[YL_F_가상진입봉])) && (s->cur[YL_F_가상최저가] <= (s->cur[YL_F_가상진입가] - (4 * b->price_scale)))) && (s->cur[YL_F_DI방향] > 0)) && (b->close > s->cur[YL_F_가상진입가]))) 
                s->cur[YL_F_재진입청산조건] = 1;
            if ((((((((s->cur[YL_F_재진입표시] == 1) && (s->cur[YL_F_가상시작] == 1)) && (b->market == 1)) && (s->bar_n > s->cur[YL_F_가상진입봉])) && (s->cur[YL_F_가상최고가] >= (s->cur[YL_F_가상진입가] + (4 * b->price_scale)))) && (s->cur[YL_F_DI방향] < 0)) && (b->close < s->cur[YL_F_가상진입가]))) 
                s->cur[YL_F_재진입청산조건] = 1;
            if (((s->cur[YL_F_재진입청산조건] == 1) && (b->market == 1))) 
                yl_order(s, TR_YL_EXIT_LONG, TR_YL_ONCLOSE, 0, (int)(0), "재진입청산매수");
            if (((s->cur[YL_F_재진입청산조건] == 1) && (b->market == (-1)))) 
                yl_order(s, TR_YL_EXIT_SHORT, TR_YL_ONCLOSE, 0, (int)(0), "재진입청산매도");
            s->cur[YL_F_이차보존조건] = 0;
            if ((((((((s->cur[YL_F_이차보존시작pt] > 0) && (s->cur[YL_F_가상진입번호] == 43)) && (s->cur[YL_F_가상시작] == 1)) && (s->bar_n > s->cur[YL_F_가상진입봉])) && (((b->market == 1) || (yl_lag(s, YL_F_MarketPosition, 1) == 1)))) && ((yl_lag(s, YL_F_가상최고가, 1) - s->cur[YL_F_가상진입가]) >= s->cur[YL_F_이차보존시작pt])) && (b->low <= (yl_lag(s, YL_F_가상최고가, 1) - s->cur[YL_F_이차추적pt])))) 
                s->cur[YL_F_이차보존조건] = 1;
            s->cur[YL_F_하락보존조건] = 0;
            if ((((((((s->cur[YL_F_이차보존시작pt] > 0) && (s->cur[YL_F_가상진입번호] == 46)) && (s->cur[YL_F_가상시작] == (-1))) && (s->bar_n > s->cur[YL_F_가상진입봉])) && (((b->market == (-1)) || (yl_lag(s, YL_F_MarketPosition, 1) == (-1))))) && ((s->cur[YL_F_가상진입가] - yl_lag(s, YL_F_가상최저가, 1)) >= s->cur[YL_F_이차보존시작pt])) && (b->high >= (yl_lag(s, YL_F_가상최저가, 1) + s->cur[YL_F_이차추적pt])))) 
                s->cur[YL_F_하락보존조건] = 1;
            s->cur[YL_F_장대연속청산조건] = 0;
            if ((((((s->cur[YL_F_장대연속청산틱] > 0) && (s->cur[YL_F_장대연속봉틱] > 0)) && (s->cur[YL_F_장대연속경과봉] > 0)) && (s->cur[YL_F_가상시작] != 0)) && (b->market != 0)))                 {
                    if ((((((s->bar_n - 1) > s->cur[YL_F_가상진입봉]) && ((s->bar_n - s->cur[YL_F_가상진입봉]) <= s->cur[YL_F_장대연속경과봉])) && ((b->high - b->low) >= (s->cur[YL_F_장대연속봉틱] * b->price_scale))) && ((yl_px(s, 'H', 1) - yl_px(s, 'L', 1)) >= (s->cur[YL_F_장대연속봉틱] * b->price_scale)))) 
                        s->cur[YL_F_장대연속발생] = 1;
                    if (((s->cur[YL_F_장대연속발생] == 1) && ((((b->close - s->cur[YL_F_가상진입가])) * s->cur[YL_F_가상시작]) >= (s->cur[YL_F_장대연속청산틱] * b->price_scale)))) 
                        s->cur[YL_F_장대연속청산조건] = 1;
                }
            if (((s->cur[YL_F_장대연속청산조건] == 1) && (b->market == 1))) 
                yl_order(s, TR_YL_EXIT_LONG, TR_YL_ONCLOSE, 0, (int)(0), "장대연속청산매수");
            if (((s->cur[YL_F_장대연속청산조건] == 1) && (b->market == (-1)))) 
                yl_order(s, TR_YL_EXIT_SHORT, TR_YL_ONCLOSE, 0, (int)(0), "장대연속청산매도");
            s->cur[YL_F_본장시가아래손절조건] = 0;
            if (((((((((((((s->cur[YL_F_본장시가아래손절] == 1) && (s->cur[YL_F_가상시작] == 1)) && (b->market == 1)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_본장시가] > 0)) && (b->high < s->cur[YL_F_본장시가])) && (s->cur[YL_F_가상진입번호] != 67)) && (((s->cur[YL_F_가상진입번호] != 94) || (s->cur[YL_F_가상진입종류30] != 2)))) && (s->cur[YL_F_선행유효] == 1)) && (s->cur[YL_F_선행위치Cnt] < 0)) && (s->cur[YL_F_DI방향] < 0)) && (b->close < s->cur[YL_F_가상진입가]))) 
                s->cur[YL_F_본장시가아래손절조건] = 1;
            if ((s->cur[YL_F_본장시가아래손절조건] == 1)) 
                yl_order(s, TR_YL_EXIT_LONG, TR_YL_ONCLOSE, 0, (int)(0), "본장시가아래손절매수");
            s->cur[YL_F_진폭역행청산조건] = 0;
            if (((((((((s->cur[YL_F_가상시작] == 1) && (b->market == 1)) && (s->cur[YL_F_본장중] == 1)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 30)) && (s->cur[YL_F_본장진폭율] >= 150)) && ((s->cur[YL_F_가상최고가] - s->cur[YL_F_가상진입가]) >= (40 * b->price_scale))) && (s->cur[YL_F_선행위치Cnt] < 0)) && (b->close < s->cur[YL_F_가상진입가]))) 
                s->cur[YL_F_진폭역행청산조건] = 1;
            if ((s->cur[YL_F_진폭역행청산조건] == 1)) 
                yl_order(s, TR_YL_EXIT_LONG, TR_YL_ONCLOSE, 0, (int)(0), "진폭역행청산매수");
            s->cur[YL_F_본장시가복귀청산조건] = 0;
            if ((((((((((s->cur[YL_F_본장시가복귀경과분] > 0) && (s->cur[YL_F_가상시작] == 1)) && (b->market == 1)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_본장시가] > 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= s->cur[YL_F_본장시가복귀경과분])) && (s->cur[YL_F_가상최고가] > s->cur[YL_F_본장시가])) && (b->close < s->cur[YL_F_본장시가])) && (b->close > s->cur[YL_F_가상진입가]))) 
                s->cur[YL_F_본장시가복귀청산조건] = 1;
            if ((s->cur[YL_F_본장시가복귀청산조건] == 1)) 
                yl_order(s, TR_YL_EXIT_LONG, TR_YL_ONCLOSE, 0, (int)(0), "본장시가복귀청산매수");
            s->cur[YL_F_장대역행청산조건] = 0;
            if ((((((s->cur[YL_F_장대역행청산틱] > 0) && (s->cur[YL_F_가상시작] != 0)) && (b->market == s->cur[YL_F_가상시작])) && (s->bar_n > s->cur[YL_F_가상진입봉])) && ((b->high - b->low) >= (s->cur[YL_F_장대역행청산틱] * b->price_scale))))                 {
                    if (((((s->cur[YL_F_가상시작] == (-1)) && (b->close > b->open)) && (s->cur[YL_F_DI방향] > 0)) && (b->close < s->cur[YL_F_가상진입가]))) 
                        s->cur[YL_F_장대역행청산조건] = 1;
                    if (((((s->cur[YL_F_가상시작] == 1) && (b->close < b->open)) && (s->cur[YL_F_DI방향] < 0)) && (b->close > s->cur[YL_F_가상진입가]))) 
                        s->cur[YL_F_장대역행청산조건] = 1;
                }
            if (((s->cur[YL_F_장대역행청산조건] == 1) && (b->market == 1))) 
                yl_order(s, TR_YL_EXIT_LONG, TR_YL_ONCLOSE, 0, (int)(0), "장대역행청산매수");
            if (((s->cur[YL_F_장대역행청산조건] == 1) && (b->market == (-1)))) 
                yl_order(s, TR_YL_EXIT_SHORT, TR_YL_ONCLOSE, 0, (int)(0), "장대역행청산매도");
            s->cur[YL_F_가상청산] = 0;
            if (((s->cur[YL_F_가상시작] == 1) && (((((s->cur[YL_F_색방향] == (-1)) && (yl_lag(s, YL_F_색방향, 1) != (-1)))) || (s->cur[YL_F_시간청산조건] == 1))))) 
                s->cur[YL_F_가상청산] = 1;
            if (((s->cur[YL_F_가상시작] == (-1)) && (((((s->cur[YL_F_색방향] == 1) && (yl_lag(s, YL_F_색방향, 1) != 1))) || (s->cur[YL_F_시간청산조건] == 1))))) 
                s->cur[YL_F_가상청산] = 1;
            if ((s->cur[YL_F_수익보호조건] == 1)) 
                s->cur[YL_F_가상청산] = 1;
            if ((s->cur[YL_F_재진입청산조건] == 1)) 
                s->cur[YL_F_가상청산] = 1;
            if ((s->cur[YL_F_하락보존조건] == 1)) 
                s->cur[YL_F_가상청산] = 1;
            if ((s->cur[YL_F_이차보존조건] == 1)) 
                s->cur[YL_F_가상청산] = 1;
            if ((s->cur[YL_F_장대연속청산조건] == 1)) 
                s->cur[YL_F_가상청산] = 1;
            if ((s->cur[YL_F_본장시가아래손절조건] == 1)) 
                s->cur[YL_F_가상청산] = 1;
            if ((s->cur[YL_F_진폭역행청산조건] == 1)) 
                s->cur[YL_F_가상청산] = 1;
            if ((s->cur[YL_F_본장시가복귀청산조건] == 1)) 
                s->cur[YL_F_가상청산] = 1;
            if ((s->cur[YL_F_장대역행청산조건] == 1)) 
                s->cur[YL_F_가상청산] = 1;
            s->cur[YL_F_비상손절조건] = 0;
            if ((((((s->cur[YL_F_비상손절pt] > 0) && (s->cur[YL_F_가상시작] == 1)) && (s->bar_n > s->cur[YL_F_가상진입봉])) && (((b->market == 1) || (yl_lag(s, YL_F_MarketPosition, 1) == 1)))) && (b->low <= (s->cur[YL_F_가상진입가] - s->cur[YL_F_비상손절pt])))) 
                s->cur[YL_F_비상손절조건] = 1;
            if ((((((s->cur[YL_F_비상손절pt] > 0) && (s->cur[YL_F_가상시작] == (-1))) && (s->bar_n > s->cur[YL_F_가상진입봉])) && (((b->market == (-1)) || (yl_lag(s, YL_F_MarketPosition, 1) == (-1))))) && (b->high >= (s->cur[YL_F_가상진입가] + s->cur[YL_F_비상손절pt])))) 
                s->cur[YL_F_비상손절조건] = 1;
            if ((s->cur[YL_F_비상손절조건] == 1)) 
                s->cur[YL_F_가상청산] = 1;
            if (((((s->cur[YL_F_마감후기록보정] == 1) && (s->cur[YL_F_가상시작] != 0)) && (s->cur[YL_F_매매시간] == 0)) && (b->market == 0))) 
                s->cur[YL_F_가상청산] = 1;
            if ((((s->cur[YL_F_가상시작] == 1) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_신호방향] == (-1)))) 
                s->cur[YL_F_가상청산] = 1;
            if ((((s->cur[YL_F_가상시작] == (-1)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_신호방향] == 1))) 
                s->cur[YL_F_가상청산] = 1;
            if ((s->cur[YL_F_FX초기화] == 1))                 {
                    s->cur[YL_F_최종청산손익_매수] = 0;
                    s->cur[YL_F_최종청산손익_매도] = 0;
                }
            if ((s->cur[YL_F_가상청산] == 1))                 {
                    s->cur[YL_F_가상1차체결] = 0;
                    if (((s->cur[YL_F_현재1차틱] > 0) && (s->cur[YL_F_진입계약수] > 1)))                         {
                            if (((s->cur[YL_F_가상시작] == 1) && (s->cur[YL_F_가상최고가] >= (s->cur[YL_F_가상진입가] + (s->cur[YL_F_현재1차틱] * b->price_scale))))) 
                                s->cur[YL_F_가상1차체결] = 1;
                            if (((s->cur[YL_F_가상시작] == (-1)) && (s->cur[YL_F_가상최저가] <= (s->cur[YL_F_가상진입가] - (s->cur[YL_F_현재1차틱] * b->price_scale))))) 
                                s->cur[YL_F_가상1차체결] = 1;
                        }
                    s->cur[YL_F_가상10틱도달] = 0;
                    if (((s->cur[YL_F_가상시작] == 1) && (s->cur[YL_F_장상태최고가] >= (s->cur[YL_F_가상진입가] + (s->cur[YL_F_장상태도달틱] * b->price_scale))))) 
                        s->cur[YL_F_가상10틱도달] = 1;
                    if (((s->cur[YL_F_가상시작] == (-1)) && (s->cur[YL_F_장상태최저가] <= (s->cur[YL_F_가상진입가] - (s->cur[YL_F_장상태도달틱] * b->price_scale))))) 
                        s->cur[YL_F_가상10틱도달] = 1;
                    if ((s->cur[YL_F_가상1차체결] == 1)) 
                        s->cur[YL_F_가상거래손익] = ((s->cur[YL_F_현재1차틱] * b->price_scale) + ((((b->close - s->cur[YL_F_가상진입가])) * s->cur[YL_F_가상시작]) * ((s->cur[YL_F_진입계약수] - 1))));
                    else 
                        s->cur[YL_F_가상거래손익] = ((((b->close - s->cur[YL_F_가상진입가])) * s->cur[YL_F_가상시작]) * s->cur[YL_F_진입계약수]);
                    s->cur[YL_F_기록손익] = s->cur[YL_F_가상거래손익];
                    if ((s->cur[YL_F_수익보호조건] == 1)) 
                        s->cur[YL_F_기록손익] = yl_max(s->cur[YL_F_가상거래손익], s->cur[YL_F_일차후본전여유pt]);
                    if ((s->cur[YL_F_가상진입번호] == 43))                         {
                            if ((s->cur[YL_F_기록손익] < 0)) 
                                s->cur[YL_F_이차직전손실] = 1;
                            else 
                                s->cur[YL_F_이차직전손실] = 0;
                        }
                    if ((s->cur[YL_F_가상진입번호] == 46))                         {
                            if ((s->cur[YL_F_기록손익] < 0)) 
                                s->cur[YL_F_하락직전손실] = 1;
                            else 
                                s->cur[YL_F_하락직전손실] = 0;
                        }
                    if ((s->cur[YL_F_가상그림자] == 0)) 
                        s->cur[YL_F_실직전손익] = s->cur[YL_F_기록손익];
                    if ((s->cur[YL_F_가상그림자] == 0)) 
                        s->cur[YL_F_실세션손익] = (s->cur[YL_F_실세션손익] + s->cur[YL_F_기록손익]);
                    if ((s->cur[YL_F_가상그림자] == 0)) 
                        s->cur[YL_F_실직전방향] = s->cur[YL_F_가상시작];
                    if ((s->cur[YL_F_가상그림자] == 0)) 
                        s->cur[YL_F_실직전최대틱] = (((s->cur[YL_F_가상시작] == 1) ? (s->cur[YL_F_가상최고가] - s->cur[YL_F_가상진입가]) : (s->cur[YL_F_가상진입가] - s->cur[YL_F_가상최저가])) / b->price_scale);
                    if ((s->cur[YL_F_가상그림자] == 0)) 
                        s->cur[YL_F_실직전수익] = (((s->cur[YL_F_가상거래손익] > 0) || (s->cur[YL_F_수익보호조건] == 1)) ? 1 : 0);
                    s->cur[YL_F_기록제외거래] = 0;
                    if (((s->cur[YL_F_본장압축해제기록분리] == 1) && (((((((((((((((((((s->cur[YL_F_가상진입번호] == 31) || (s->cur[YL_F_가상진입번호] == 34)) || (s->cur[YL_F_가상진입번호] == 37)) || (s->cur[YL_F_가상진입번호] == 40)) || (s->cur[YL_F_가상진입번호] == 43)) || (s->cur[YL_F_가상진입번호] == 46)) || (s->cur[YL_F_가상진입번호] == 49)) || (s->cur[YL_F_가상진입번호] == 52)) || (s->cur[YL_F_가상진입번호] == 55)) || (s->cur[YL_F_가상진입번호] == 58)) || (s->cur[YL_F_가상진입번호] == 61)) || (s->cur[YL_F_가상진입번호] == 64)) || (s->cur[YL_F_가상진입번호] == 67)) || (s->cur[YL_F_가상진입번호] == 79)) || (s->cur[YL_F_가상진입번호] == 82)) || (s->cur[YL_F_가상진입번호] == 85)) || (s->cur[YL_F_가상진입번호] == 88)) || (s->cur[YL_F_가상진입번호] == 94))))) 
                        s->cur[YL_F_기록제외거래] = 1;
                    if ((s->cur[YL_F_기록제외거래] == 0))                         {
                            if ((s->cur[YL_F_가상시작] == 1)) 
                                s->cur[YL_F_최종청산손익_매수] = (s->cur[YL_F_최종청산손익_매수] + s->cur[YL_F_가상거래손익]);
                            else 
                                s->cur[YL_F_최종청산손익_매도] = (s->cur[YL_F_최종청산손익_매도] + s->cur[YL_F_가상거래손익]);
                            s->cur[YL_F_직전거래방향] = s->cur[YL_F_가상시작];
                            s->cur[YL_F_직전거래손익] = s->cur[YL_F_가상거래손익];
                            if (((s->cur[YL_F_본장중] == 1) && (s->cur[YL_F_가상거래손익] > 0))) 
                                s->cur[YL_F_본장수익있음] = 1;
                            s->cur[YL_F_직전청산봉] = s->bar_n;
                            s->cur[YL_F_직전본장손절] = s->cur[YL_F_본장시가아래손절조건];
                            s->cur[YL_F_직전거래이동틱] = ((((b->close - s->cur[YL_F_가상진입가])) * s->cur[YL_F_가상시작]) / b->price_scale);
                            s->cur[YL_F_도달기록위치] = fmod(s->cur[YL_F_완료거래수], 50);
                            yl_arr_set(s, YL_A_도달기록, (int)(s->cur[YL_F_도달기록위치]), s->cur[YL_F_가상10틱도달]);
                            s->cur[YL_F_완료거래수] = (s->cur[YL_F_완료거래수] + 1);
                            if (((s->cur[YL_F_장상태거래수] > 0) && (s->cur[YL_F_완료거래수] >= s->cur[YL_F_장상태거래수])))                                 {
                                    s->cur[YL_F_도달합] = 0;
                                    for (s->cur[YL_F_장k] = (0); s->cur[YL_F_장k] <= ((yl_min(s->cur[YL_F_장상태거래수], 50) - 1)); s->cur[YL_F_장k] += 1)                                         {
                                            s->cur[YL_F_도달합] = (s->cur[YL_F_도달합] + yl_arr(s, YL_A_도달기록, (int)(fmod(((s->cur[YL_F_완료거래수] - 1) - s->cur[YL_F_장k]), 50))));
                                        }
                                    s->cur[YL_F_최근도달률] = ((s->cur[YL_F_도달합] / yl_min(s->cur[YL_F_장상태거래수], 50)) * 100);
                                }
                            s->cur[YL_F_직전색청산] = 0;
                            if (((s->cur[YL_F_마감후기록보정] == 0) || (s->cur[YL_F_매매시간] == 1)))                                 {
                                    if ((((s->cur[YL_F_가상시작] == 1) && (s->cur[YL_F_색방향] == (-1))) && (yl_lag(s, YL_F_색방향, 1) != (-1)))) 
                                        s->cur[YL_F_직전색청산] = 1;
                                    if ((((s->cur[YL_F_가상시작] == (-1)) && (s->cur[YL_F_색방향] == 1)) && (yl_lag(s, YL_F_색방향, 1) != 1))) 
                                        s->cur[YL_F_직전색청산] = 1;
                                    if ((s->cur[YL_F_본장시가아래손절조건] == 1)) 
                                        s->cur[YL_F_직전색청산] = 1;
                                    if ((s->cur[YL_F_장대역행청산조건] == 1)) 
                                        s->cur[YL_F_직전색청산] = 1;
                                }
                        }
                    s->cur[YL_F_가상포지션] = 0;
                }
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_세션진입수] = 0;
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_본장압축해제횟수] = 0;
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_반전돌파횟수] = 0;
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_반전돌파매도횟수] = 0;
            s->cur[YL_F_반대방향진입] = 0;
            if (((((s->cur[YL_F_세션진입수] == 0) && (s->cur[YL_F_마지막진입방향] != 0)) && (s->cur[YL_F_신호방향] != 0)) && (s->cur[YL_F_신호방향] != s->cur[YL_F_마지막진입방향]))) 
                s->cur[YL_F_반대방향진입] = 1;
            if (((((((s->cur[YL_F_돌파반대본장전금지] == 1) && (s->cur[YL_F_반대방향진입] == 1)) && (s->cur[YL_F_신호종류] == 1)) && (s->cur[YL_F_후보등급] == 8)) && (b->time >= 70000)) && (s->cur[YL_F_봉시작분] < s->cur[YL_F_본장시작분])))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                }
            if ((((((((s->cur[YL_F_후보A매도반대금지분] > 0) && (s->cur[YL_F_반대방향진입] == 1)) && (s->cur[YL_F_신호종류] == 1)) && (s->cur[YL_F_후보등급] == 3)) && (s->cur[YL_F_신호방향] == (-1))) && (b->time >= 70000)) && (s->cur[YL_F_봉시작분] < (s->cur[YL_F_본장시작분] - s->cur[YL_F_후보A매도반대금지분]))))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                }
            if ((((((((s->cur[YL_F_돌파매수반대자정금지] == 1) && (s->cur[YL_F_반대방향진입] == 1)) && (s->cur[YL_F_신호종류] == 1)) && (s->cur[YL_F_신호방향] == 1)) && (s->cur[YL_F_후보등급] == 8)) && (s->cur[YL_F_본장기준봉분] >= 1440)) && (s->cur[YL_F_본장기준봉분] < 1860)))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                }
            if (((((((s->cur[YL_F_돌파매수장초반금지분] > 0) && (s->cur[YL_F_신호종류] == 1)) && (s->cur[YL_F_신호방향] == 1)) && (s->cur[YL_F_후보등급] == 8)) && (s->cur[YL_F_본장기준봉분] >= s->cur[YL_F_본장시작분])) && (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_돌파매수장초반금지분]))))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                }
            if (((((s->cur[YL_F_지표발표매수금지] == 1) && (s->cur[YL_F_신호방향] == 1)) && (s->cur[YL_F_본장기준봉분] >= (s->cur[YL_F_본장시작분] - 60))) && (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] - 45))))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                }
            if (((((((s->cur[YL_F_후보T매도늦은금지분] > 0) && (s->cur[YL_F_신호종류] == 1)) && (s->cur[YL_F_신호방향] == (-1))) && (s->cur[YL_F_후보등급] == 7)) && (s->cur[YL_F_본장기준봉분] >= (s->cur[YL_F_본장시작분] + s->cur[YL_F_후보T매도늦은금지분]))) && (s->cur[YL_F_본장기준봉분] < 1860)))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                }
            if ((((((s->cur[YL_F_역행기울기기준] > 0) && (s->cur[YL_F_선행유효] == 1)) && (s->cur[YL_F_신호방향] == 1)) && (s->cur[YL_F_선행기울기Cnt] > s->cur[YL_F_역행기울기기준])) && (s->cur[YL_F_선행위치Cnt] < 0)))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                }
            if ((((((s->cur[YL_F_역행기울기기준] > 0) && (s->cur[YL_F_선행유효] == 1)) && (s->cur[YL_F_신호방향] == (-1))) && (s->cur[YL_F_선행기울기Cnt] < (-s->cur[YL_F_역행기울기기준]))) && (s->cur[YL_F_선행위치Cnt] > 0)))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                }
            if (((((((((s->cur[YL_F_조정B약매수수익후금지] == 1) && (s->cur[YL_F_신호종류] == 4)) && (s->cur[YL_F_신호방향] == 1)) && (s->cur[YL_F_조정신호종류] == 2)) && (s->cur[YL_F_조정신호등급] == 1)) && (s->cur[YL_F_본장기준봉분] >= (s->cur[YL_F_본장시작분] + 180))) && (s->cur[YL_F_본장기준봉분] < 1860)) && (s->cur[YL_F_최종청산손익_매수] > 0)))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                }
            if (((((((((s->cur[YL_F_조정B중무매매금지] == 1) && (s->cur[YL_F_신호종류] == 4)) && (s->cur[YL_F_신호방향] == 1)) && (s->cur[YL_F_조정신호종류] == 2)) && (s->cur[YL_F_조정신호등급] == 2)) && (s->cur[YL_F_본장기준봉분] >= (s->cur[YL_F_본장시작분] + 150))) && (s->cur[YL_F_본장기준봉분] < 1860)) && (s->cur[YL_F_세션진입수] == 0)))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                }
            if (((((s->cur[YL_F_반대방향01시금지] == 1) && (s->cur[YL_F_반대방향진입] == 1)) && (s->cur[YL_F_본장기준봉분] >= (s->cur[YL_F_본장시작분] + 150))) && (s->cur[YL_F_본장기준봉분] < 1860)))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                }
            s->cur[YL_F_직전실패진입] = 0;
            if ((((s->cur[YL_F_신호방향] == 1) && (s->cur[YL_F_직전거래방향] == 1)) && (s->cur[YL_F_직전색청산] == 1))) 
                s->cur[YL_F_직전실패진입] = 1;
            if ((((((((s->cur[YL_F_직전실패손실기준] > 0) && (s->cur[YL_F_직전실패진입] == 1)) && (s->cur[YL_F_직전거래손익] <= (-s->cur[YL_F_직전실패손실기준]))) && (s->cur[YL_F_신호종류] == 4)) && (s->cur[YL_F_신호방향] == 1)) && (((s->cur[YL_F_조정신호종류] == 1) || (s->cur[YL_F_조정신호종류] == 2)))) && (s->cur[YL_F_조정신호등급] == 1)))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                    s->cur[YL_F_직전실패진입] = 0;
                }
            s->cur[YL_F_전일실패진입] = 0;
            if (((((s->cur[YL_F_신호방향] == (-1)) && (s->cur[YL_F_세션진입수] == 0)) && (s->cur[YL_F_직전거래방향] == (-1))) && (s->cur[YL_F_직전색청산] == 1))) 
                s->cur[YL_F_전일실패진입] = 1;
            if (((((s->cur[YL_F_전일실패매도금지] == 1) && (s->cur[YL_F_전일실패진입] == 1)) && (s->cur[YL_F_신호종류] == 1)) && (((s->cur[YL_F_후보등급] == 3) || (s->cur[YL_F_후보등급] == 8)))))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                    s->cur[YL_F_전일실패진입] = 0;
                }
            s->cur[YL_F_직전매수실패매도] = 0;
            if ((((s->cur[YL_F_신호방향] == (-1)) && (s->cur[YL_F_직전거래방향] == 1)) && (s->cur[YL_F_직전색청산] == 1))) 
                s->cur[YL_F_직전매수실패매도] = 1;
            if (((((s->cur[YL_F_직전매수실패S매도금지] == 1) && (s->cur[YL_F_직전매수실패매도] == 1)) && (s->cur[YL_F_신호종류] == 1)) && (s->cur[YL_F_후보등급] == 5)))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                    s->cur[YL_F_직전매수실패매도] = 0;
                }
            s->cur[YL_F_장약세진입] = 0;
            if ((((s->cur[YL_F_장상태거래수] > 0) && (s->cur[YL_F_완료거래수] >= s->cur[YL_F_장상태거래수])) && (s->cur[YL_F_최근도달률] < s->cur[YL_F_장상태도달률]))) 
                s->cur[YL_F_장약세진입] = 1;
            if ((((s->cur[YL_F_장약세3개금지] == 1) && (s->cur[YL_F_장약세진입] == 1)) && ((((((((s->cur[YL_F_신호종류] == 4) && (s->cur[YL_F_신호방향] == 1)) && (s->cur[YL_F_조정신호종류] == 1)) && (s->cur[YL_F_조정신호등급] == 1))) || (((s->cur[YL_F_신호종류] == 5) && (s->cur[YL_F_신호방향] == (-1))))) || ((((s->cur[YL_F_신호종류] == 1) && (s->cur[YL_F_신호방향] == 1)) && (s->cur[YL_F_후보등급] == 7)))))))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                    s->cur[YL_F_장약세진입] = 0;
                }
            if ((((((s->cur[YL_F_장약세후보매도금지] == 1) && (s->cur[YL_F_장약세진입] == 1)) && (s->cur[YL_F_신호종류] == 1)) && (s->cur[YL_F_신호방향] == (-1))) && ((((s->cur[YL_F_후보등급] == 7) || (s->cur[YL_F_후보등급] == 6)) || (s->cur[YL_F_후보등급] == 5)))))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                    s->cur[YL_F_장약세진입] = 0;
                }
            if ((((s->cur[YL_F_장약세2개금지] == 1) && (s->cur[YL_F_장약세진입] == 1)) && (((((((s->cur[YL_F_신호종류] == 4) && (s->cur[YL_F_신호방향] == 1)) && (s->cur[YL_F_조정신호종류] == 2)) && (s->cur[YL_F_조정신호등급] == 2))) || ((((s->cur[YL_F_신호종류] == 1) && (s->cur[YL_F_신호방향] == (-1))) && (s->cur[YL_F_후보등급] == 4)))))))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                    s->cur[YL_F_장약세진입] = 0;
                }
            if ((((((s->cur[YL_F_장약세매수2개금지] == 1) && (s->cur[YL_F_장약세진입] == 1)) && (s->cur[YL_F_신호종류] == 1)) && (s->cur[YL_F_신호방향] == 1)) && (((s->cur[YL_F_후보등급] == 6) || (s->cur[YL_F_후보등급] == 8)))))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                    s->cur[YL_F_장약세진입] = 0;
                }
            if ((((((((((s->cur[YL_F_전환매도하락감싸기금지] == 1) && (s->cur[YL_F_신호종류] == 5)) && (s->cur[YL_F_신호방향] == (-1))) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (s->cur[YL_F_핵심지난상승최저가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] > 0)) && (s->cur[YL_F_핵심지난하락최저가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] >= s->cur[YL_F_핵심지난상승최고가])) && (s->cur[YL_F_핵심지난하락최저가] <= s->cur[YL_F_핵심지난상승최저가])))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                    s->cur[YL_F_장약세진입] = 0;
                }
            if (((((((s->cur[YL_F_전환매도자정수익금지] == 1) && (s->cur[YL_F_신호종류] == 5)) && (s->cur[YL_F_신호방향] == (-1))) && (s->cur[YL_F_본장기준봉분] >= 1440)) && (s->cur[YL_F_본장기준봉분] < 1860)) && ((s->cur[YL_F_최종청산손익_매수] + s->cur[YL_F_최종청산손익_매도]) > 0)))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                    s->cur[YL_F_장약세진입] = 0;
                }
            if ((((((s->cur[YL_F_전환매수반대금지] == 1) && (s->cur[YL_F_반대방향진입] == 1)) && (s->cur[YL_F_장약세진입] == 0)) && (s->cur[YL_F_신호종류] == 5)) && (s->cur[YL_F_신호방향] == 1)))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                }
            if ((((((s->cur[YL_F_전환매도반대금지] == 1) && (s->cur[YL_F_반대방향진입] == 1)) && (s->cur[YL_F_장약세진입] == 0)) && (s->cur[YL_F_신호종류] == 5)) && (s->cur[YL_F_신호방향] == (-1))))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                }
            if (((((((s->cur[YL_F_매수수익후매도금지틱] > 0) && (s->cur[YL_F_신호방향] == (-1))) && (s->cur[YL_F_직전거래방향] == 1)) && (s->cur[YL_F_직전거래이동틱] >= s->cur[YL_F_매수수익후매도금지틱])) && ((s->bar_n - s->cur[YL_F_직전청산봉]) <= s->cur[YL_F_매수수익후매도금지봉수])) && (((((((s->cur[YL_F_신호종류] == 1) && ((((s->cur[YL_F_후보등급] == 8) || (s->cur[YL_F_후보등급] == 7)) || (s->cur[YL_F_후보등급] == 4))))) || (s->cur[YL_F_신호종류] == 2)) || (s->cur[YL_F_신호종류] == 3)) || (s->cur[YL_F_신호종류] == 4)))))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                    s->cur[YL_F_장약세진입] = 0;
                }
            if (((((((((((((s->cur[YL_F_매도수익후압축매수통합카운트] > 0) && (s->cur[YL_F_신호종류] == 1)) && (s->cur[YL_F_신호방향] == 1)) && (s->cur[YL_F_후보등급] != 8)) && (s->cur[YL_F_후보등급] != 7)) && (s->cur[YL_F_후보등급] != 6)) && (s->cur[YL_F_후보등급] != 4)) && (s->cur[YL_F_직전거래방향] == (-1))) && (s->cur[YL_F_직전거래손익] > 0)) && ((s->bar_n - s->cur[YL_F_직전청산봉]) <= s->cur[YL_F_매도수익후압축매수봉수])) && (s->cur[YL_F_압축지속봉수] > yl_lag(s, YL_F_압축지속봉수, 1))) && (s->cur[YL_F_통합카운트] >= s->cur[YL_F_매도수익후압축매수통합카운트])))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                    s->cur[YL_F_장약세진입] = 0;
                }
            if (((((s->cur[YL_F_반전매수DI음수금지] == 1) && (s->cur[YL_F_신호방향] == 1)) && (s->cur[YL_F_DI방향] < 0)) && (((((s->cur[YL_F_신호종류] == 1) && (s->cur[YL_F_후보등급] == 6))) || (s->cur[YL_F_신호종류] == 5)))))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                    s->cur[YL_F_장약세진입] = 0;
                }
            if (((((s->cur[YL_F_반전매도DI양수금지] == 1) && (s->cur[YL_F_신호방향] == (-1))) && (s->cur[YL_F_DI방향] > 0)) && (((((s->cur[YL_F_신호종류] == 1) && (s->cur[YL_F_후보등급] == 6))) || (s->cur[YL_F_신호종류] == 5)))))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                    s->cur[YL_F_장약세진입] = 0;
                }
            if (((((((s->cur[YL_F_압축중조정매수금지봉수] > 0) && (s->cur[YL_F_신호종류] == 4)) && (s->cur[YL_F_신호방향] == 1)) && ((s->cur[YL_F_최종청산손익_매수] + s->cur[YL_F_최종청산손익_매도]) > 0)) && (s->cur[YL_F_압축지속봉수] >= s->cur[YL_F_압축중조정매수금지봉수])) && (s->cur[YL_F_압축지속봉수] > yl_lag(s, YL_F_압축지속봉수, 1))))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                    s->cur[YL_F_장약세진입] = 0;
                }
            if ((((((((s->cur[YL_F_수익후직전하락위치기준] > 0) && (s->cur[YL_F_선행유효] == 1)) && ((s->cur[YL_F_최종청산손익_매수] + s->cur[YL_F_최종청산손익_매도]) > 0)) && (s->cur[YL_F_신호종류] == 4)) && (s->cur[YL_F_신호방향] == (-1))) && (s->cur[YL_F_조정신호종류] == 2)) && (s->cur[YL_F_선행직전하락기울기] > s->cur[YL_F_수익후직전하락위치기준])))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                    s->cur[YL_F_장약세진입] = 0;
                }
            if (((((((((s->cur[YL_F_돌파매도반대장초반금지분] > 0) && (s->cur[YL_F_반대방향진입] == 1)) && (s->cur[YL_F_장약세진입] == 0)) && (s->cur[YL_F_신호종류] == 1)) && (s->cur[YL_F_신호방향] == (-1))) && (s->cur[YL_F_후보등급] == 8)) && (s->cur[YL_F_본장기준봉분] >= s->cur[YL_F_본장시작분])) && (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_돌파매도반대장초반금지분]))))                 {
                    s->cur[YL_F_신호방향] = 0;
                    s->cur[YL_F_반대방향진입] = 0;
                }
            s->cur[YL_F_주문코드] = 0;
            if ((((s->cur[YL_F_매매시간] == 1) && (s->cur[YL_F_신호방향] == 1)) && (s->cur[YL_F_가상시작] != 1)))                 {
                    if ((s->cur[YL_F_신호종류] == 1))                         {
                            if ((s->cur[YL_F_후보등급] == 8))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((3 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((2 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((1 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if ((s->cur[YL_F_후보등급] == 7))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((3 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((2 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((1 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if ((s->cur[YL_F_후보등급] == 6))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((15 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((14 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((13 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if ((s->cur[YL_F_후보등급] == 5))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((9 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((8 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((7 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if ((s->cur[YL_F_후보등급] == 4))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((3 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((2 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((1 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if ((s->cur[YL_F_후보등급] == 3))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((9 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((8 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((7 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if ((s->cur[YL_F_후보등급] == 2))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((9 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((8 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((7 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if ((s->cur[YL_F_후보등급] == 1))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((9 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((8 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((7 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((9 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((8 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((7 * 10) + s->cur[YL_F_진입계약수]);
                                }
                        }
                    else if ((s->cur[YL_F_신호종류] == 2))                         {
                            if ((s->cur[YL_F_장약세진입] == 1)) 
                                s->cur[YL_F_주문코드] = ((3 * 10) + s->cur[YL_F_장약세계약수]);
                            else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                s->cur[YL_F_주문코드] = ((2 * 10) + s->cur[YL_F_진입계약수]);
                            else 
                                s->cur[YL_F_주문코드] = ((1 * 10) + s->cur[YL_F_진입계약수]);
                        }
                    else if ((s->cur[YL_F_신호종류] == 3))                         {
                            if ((s->cur[YL_F_장약세진입] == 1)) 
                                s->cur[YL_F_주문코드] = ((3 * 10) + s->cur[YL_F_장약세계약수]);
                            else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                s->cur[YL_F_주문코드] = ((2 * 10) + s->cur[YL_F_진입계약수]);
                            else 
                                s->cur[YL_F_주문코드] = ((1 * 10) + s->cur[YL_F_진입계약수]);
                        }
                    else if ((s->cur[YL_F_신호종류] == 5))                         {
                            if ((s->cur[YL_F_장약세진입] == 1)) 
                                s->cur[YL_F_주문코드] = ((15 * 10) + s->cur[YL_F_장약세계약수]);
                            else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                s->cur[YL_F_주문코드] = ((14 * 10) + s->cur[YL_F_진입계약수]);
                            else 
                                s->cur[YL_F_주문코드] = ((13 * 10) + s->cur[YL_F_진입계약수]);
                        }
                    else                         {
                            if (((s->cur[YL_F_조정신호종류] == 1) && (s->cur[YL_F_조정신호등급] == 3)))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((21 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((20 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((19 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if (((s->cur[YL_F_조정신호종류] == 1) && (s->cur[YL_F_조정신호등급] == 2)))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((21 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((20 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((19 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if (((s->cur[YL_F_조정신호종류] == 1) && (s->cur[YL_F_조정신호등급] == 1)))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((21 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_직전실패진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((19 * 10) + s->cur[YL_F_진입계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((20 * 10) + s->cur[YL_F_조정약계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((19 * 10) + s->cur[YL_F_조정약계약수]);
                                }
                            else if (((s->cur[YL_F_조정신호종류] == 2) && (s->cur[YL_F_조정신호등급] == 3)))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((21 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((20 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((19 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if (((s->cur[YL_F_조정신호종류] == 2) && (s->cur[YL_F_조정신호등급] == 2)))                                 {
                                    s->cur[YL_F_조정B중수량] = s->cur[YL_F_진입계약수];
                                    if ((((s->cur[YL_F_조정B중장전분] > 0) && (s->cur[YL_F_본장기준봉분] >= (s->cur[YL_F_본장시작분] - s->cur[YL_F_조정B중장전분]))) && (s->cur[YL_F_본장기준봉분] < s->cur[YL_F_본장시작분]))) 
                                        s->cur[YL_F_조정B중수량] = s->cur[YL_F_조정B중장전계약수];
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((21 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((20 * 10) + s->cur[YL_F_조정B중수량]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((19 * 10) + s->cur[YL_F_조정B중수량]);
                                }
                            else if (((s->cur[YL_F_조정신호종류] == 2) && (s->cur[YL_F_조정신호등급] == 1)))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((21 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_직전실패진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((19 * 10) + s->cur[YL_F_진입계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((20 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((19 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((21 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((20 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((19 * 10) + s->cur[YL_F_진입계약수]);
                                }
                        }
                    s->cur[YL_F_가상매수진입] = 1;
                }
            if ((((s->cur[YL_F_매매시간] == 1) && (s->cur[YL_F_신호방향] == (-1))) && (s->cur[YL_F_가상시작] != (-1))))                 {
                    if ((s->cur[YL_F_신호종류] == 1))                         {
                            if ((s->cur[YL_F_후보등급] == 8))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((6 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_전일실패진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((4 * 10) + s->cur[YL_F_진입계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((5 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((4 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if ((s->cur[YL_F_후보등급] == 7))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((6 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((5 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((4 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if ((s->cur[YL_F_후보등급] == 6))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((18 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((17 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((16 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if ((s->cur[YL_F_후보등급] == 5))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((12 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_직전매수실패매도] == 1)) 
                                        s->cur[YL_F_주문코드] = ((10 * 10) + s->cur[YL_F_진입계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((11 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((10 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if ((s->cur[YL_F_후보등급] == 4))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((6 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((5 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((4 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if ((s->cur[YL_F_후보등급] == 3))                                 {
                                    s->cur[YL_F_후보A매도수량] = s->cur[YL_F_진입계약수];
                                    if ((((s->cur[YL_F_후보A매도장초반분] > 0) && (s->cur[YL_F_본장기준봉분] >= s->cur[YL_F_본장시작분])) && (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_후보A매도장초반분])))) 
                                        s->cur[YL_F_후보A매도수량] = s->cur[YL_F_후보A매도장초반계약수];
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((12 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_전일실패진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((10 * 10) + s->cur[YL_F_진입계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((11 * 10) + s->cur[YL_F_후보A매도수량]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((10 * 10) + s->cur[YL_F_후보A매도수량]);
                                }
                            else if ((s->cur[YL_F_후보등급] == 2))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((12 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((11 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((10 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if ((s->cur[YL_F_후보등급] == 1))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((12 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((11 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((10 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((12 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((11 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((10 * 10) + s->cur[YL_F_진입계약수]);
                                }
                        }
                    else if ((s->cur[YL_F_신호종류] == 2))                         {
                            if ((s->cur[YL_F_장약세진입] == 1)) 
                                s->cur[YL_F_주문코드] = ((6 * 10) + s->cur[YL_F_장약세계약수]);
                            else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                s->cur[YL_F_주문코드] = ((5 * 10) + s->cur[YL_F_진입계약수]);
                            else 
                                s->cur[YL_F_주문코드] = ((4 * 10) + s->cur[YL_F_진입계약수]);
                        }
                    else if ((s->cur[YL_F_신호종류] == 3))                         {
                            if ((s->cur[YL_F_장약세진입] == 1)) 
                                s->cur[YL_F_주문코드] = ((6 * 10) + s->cur[YL_F_장약세계약수]);
                            else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                s->cur[YL_F_주문코드] = ((5 * 10) + s->cur[YL_F_진입계약수]);
                            else 
                                s->cur[YL_F_주문코드] = ((4 * 10) + s->cur[YL_F_진입계약수]);
                        }
                    else if ((s->cur[YL_F_신호종류] == 5))                         {
                            if ((s->cur[YL_F_장약세진입] == 1)) 
                                s->cur[YL_F_주문코드] = ((18 * 10) + s->cur[YL_F_장약세계약수]);
                            else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                s->cur[YL_F_주문코드] = ((17 * 10) + s->cur[YL_F_진입계약수]);
                            else 
                                s->cur[YL_F_주문코드] = ((16 * 10) + s->cur[YL_F_진입계약수]);
                        }
                    else                         {
                            if (((s->cur[YL_F_조정신호종류] == 1) && (s->cur[YL_F_조정신호등급] == 3)))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((24 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((23 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((22 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if (((s->cur[YL_F_조정신호종류] == 1) && (s->cur[YL_F_조정신호등급] == 2)))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((24 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((23 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((22 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if (((s->cur[YL_F_조정신호종류] == 1) && (s->cur[YL_F_조정신호등급] == 1)))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((24 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((23 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((22 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if (((s->cur[YL_F_조정신호종류] == 2) && (s->cur[YL_F_조정신호등급] == 3)))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((24 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((23 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((22 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if (((s->cur[YL_F_조정신호종류] == 2) && (s->cur[YL_F_조정신호등급] == 2)))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((24 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((23 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((22 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else if (((s->cur[YL_F_조정신호종류] == 2) && (s->cur[YL_F_조정신호등급] == 1)))                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((24 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((23 * 10) + s->cur[YL_F_조정약계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((22 * 10) + s->cur[YL_F_진입계약수]);
                                }
                            else                                 {
                                    if ((s->cur[YL_F_장약세진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((24 * 10) + s->cur[YL_F_장약세계약수]);
                                    else if ((s->cur[YL_F_반대방향진입] == 1)) 
                                        s->cur[YL_F_주문코드] = ((23 * 10) + s->cur[YL_F_진입계약수]);
                                    else 
                                        s->cur[YL_F_주문코드] = ((22 * 10) + s->cur[YL_F_진입계약수]);
                                }
                        }
                    s->cur[YL_F_가상매도진입] = 1;
                }
            if (((((((s->cur[YL_F_첫진입늦은금지분] > 0) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_세션진입수] == 0)) && (s->cur[YL_F_본장기준봉분] >= (1440 + s->cur[YL_F_첫진입늦은금지분]))) && (s->cur[YL_F_본장기준봉분] < 1860)) && (((trunc((s->cur[YL_F_주문코드] / 10)) == 1) || (trunc((s->cur[YL_F_주문코드] / 10)) == 11)))))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((((((((((s->cur[YL_F_되돌림겹침압축매수금지] == 1) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_미래계산가능] == 1)) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] > 0)) && (s->cur[YL_F_핵심지난상승_382] > s->cur[YL_F_핵심지난하락최고가])) && (s->cur[YL_F_핵심지난상승최저가] > 0)) && (s->cur[YL_F_핵심지난하락_618] < s->cur[YL_F_핵심지난상승최저가])) && (s->cur[YL_F_지속저장목표4] > 0)) && (b->high < s->cur[YL_F_지속저장목표4])) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 7)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 9)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if ((((((((((((s->cur[YL_F_파랑구조조정매수반대금지] == 1) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_미래계산가능] == 1)) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (s->cur[YL_F_핵심지난상승최저가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] > 0)) && (s->cur[YL_F_핵심지난하락최저가] > 0)) && (s->cur[YL_F_핵심지난상승최고가] < s->cur[YL_F_핵심지난하락최고가])) && (s->cur[YL_F_핵심지난하락최저가] < s->cur[YL_F_핵심지난상승최저가])) && (b->close > s->cur[YL_F_핵심지난하락최고가])) && (trunc((s->cur[YL_F_주문코드] / 10)) == 20)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if ((((((((((((((s->cur[YL_F_되돌림겹침돌파매수금지] == 1) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_미래계산가능] == 1)) && ((s->cur[YL_F_최종청산손익_매수] + s->cur[YL_F_최종청산손익_매도]) > 0)) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (s->cur[YL_F_핵심지난하락최고가] > 0)) && (s->cur[YL_F_핵심지난상승_382] > s->cur[YL_F_핵심지난하락최고가])) && (s->cur[YL_F_핵심지난상승최저가] > 0)) && (s->cur[YL_F_핵심지난하락_618] < s->cur[YL_F_핵심지난상승최저가])) && (s->cur[YL_F_지속저장목표4] > 0)) && (b->high < s->cur[YL_F_지속저장목표4])) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 1)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 3)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if ((((((((((((s->cur[YL_F_지속상승압축매도반대금지] == 1) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_미래계산가능] == 1)) && (s->cur[YL_F_지속목표방향] == 1)) && (s->cur[YL_F_지속저장목표1] > 0)) && (b->high < s->cur[YL_F_지속저장목표1])) && (s->cur[YL_F_S삼선비율] > s->cur[YL_F_지속상승삼선기준])) && (s->cur[YL_F_S두구간비율] > s->cur[YL_F_지속상승두구간기준])) && (s->cur[YL_F_선행진폭율] > s->cur[YL_F_지속상승진폭율기준])) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 10)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 12)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if ((((((((((((s->cur[YL_F_지속하락압축매수금지] == 1) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_미래계산가능] == 1)) && (s->cur[YL_F_지속목표방향] == (-1))) && (s->cur[YL_F_지속저장목표1] > 0)) && (b->low > s->cur[YL_F_지속저장목표1])) && (s->cur[YL_F_S삼선비율] > s->cur[YL_F_지속상승삼선기준])) && (s->cur[YL_F_S두구간비율] > s->cur[YL_F_지속상승두구간기준])) && (s->cur[YL_F_선행진폭율] > s->cur[YL_F_지속상승진폭율기준])) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 7)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 9)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if ((((((s->cur[YL_F_반전매수통합카운트기준] > 0) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_통합카운트] >= s->cur[YL_F_반전매수통합카운트기준])) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 13)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 15)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((((((((s->cur[YL_F_매수수익후하락추세반전매수금지] == 1) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_미래계산가능] == 1)) && (s->cur[YL_F_최종청산손익_매수] > 0)) && (s->cur[YL_F_하락추세구조] == 1)) && (s->cur[YL_F_빨강구조] == 0)) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (b->high > s->cur[YL_F_핵심지난상승최고가])) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 13)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 15)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if ((((((s->cur[YL_F_반전매도통합카운트기준] > 0) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_통합카운트] <= (-s->cur[YL_F_반전매도통합카운트기준]))) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 16)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 18)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((((((((s->cur[YL_F_매도수익후상승추세반전매도금지] == 1) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_미래계산가능] == 1)) && (s->cur[YL_F_최종청산손익_매도] > 0)) && (s->cur[YL_F_상승추세구조] == 1)) && (s->cur[YL_F_파랑구조] == 0)) && (s->cur[YL_F_핵심지난하락최저가] > 0)) && (b->low < s->cur[YL_F_핵심지난하락최저가])) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 16)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 18)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((((((s->cur[YL_F_늦은조정매수삼선기준] > 0) && (s->cur[YL_F_늦은조정매수금지분] > 0)) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_S삼선비율] >= s->cur[YL_F_늦은조정매수삼선기준])) && (s->cur[YL_F_본장기준봉분] >= (1440 + s->cur[YL_F_늦은조정매수금지분]))) && (s->cur[YL_F_본장기준봉분] < 1860)) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 19)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 21)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((((((s->cur[YL_F_본장초반매도금지분] > 0) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_본장시가] > 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < s->cur[YL_F_본장초반매도금지분])) && (b->close >= s->cur[YL_F_본장시가])) && (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) >= 3)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((((((((s->cur[YL_F_장전장대틱] > 0) && (s->cur[YL_F_장전장대감시분] > 0)) && (s->cur[YL_F_장전장대본장초반분] > 0)) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_장전장대고점] > 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < s->cur[YL_F_장전장대본장초반분])) && (b->close < s->cur[YL_F_장전장대고점])) && (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) < 3)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((((((s->cur[YL_F_본장초반런던중심압축매수반대금지분] > 0) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_런던중심가] > 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < s->cur[YL_F_본장초반런던중심압축매수반대금지분])) && ((((b->high + b->low)) / 2) < s->cur[YL_F_런던중심가])) && (trunc((s->cur[YL_F_주문코드] / 10)) == 8)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((s->cur[YL_F_런던고가] > 0) && (s->cur[YL_F_런던저가] > 0))) 
                s->cur[YL_F_런던75] = (s->cur[YL_F_런던저가] + (((s->cur[YL_F_런던고가] - s->cur[YL_F_런던저가])) * 0.75));
            if ((((((((((s->cur[YL_F_런던75압축매수금지] == 1) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_본장중] == 0)) && (s->cur[YL_F_본장기준봉분] < s->cur[YL_F_본장시작분])) && (s->cur[YL_F_런던시가] > 0)) && (s->cur[YL_F_런던75] > 0)) && (s->cur[YL_F_런던75] <= (s->cur[YL_F_런던시가] + (s->cur[YL_F_런던75근접틱] * b->price_scale)))) && (b->close < s->cur[YL_F_런던75])) && (trunc((s->cur[YL_F_주문코드] / 10)) == 7)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if ((((((((s->cur[YL_F_런던시가위매도금지] == 1) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_본장중] == 0)) && (s->cur[YL_F_본장기준봉분] < s->cur[YL_F_본장시작분])) && (s->cur[YL_F_런던시가] > 0)) && (b->low > s->cur[YL_F_런던시가])) && (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) >= 3)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((((((((s->cur[YL_F_런던시가아래빨강구조압축매수반대금지] == 1) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_본장중] == 0)) && (s->cur[YL_F_본장기준봉분] < s->cur[YL_F_본장시작분])) && (s->cur[YL_F_런던시가] > 0)) && (b->high < s->cur[YL_F_런던시가])) && (s->cur[YL_F_빨강구조] == 1)) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (b->close < s->cur[YL_F_핵심지난상승최고가])) && (trunc((s->cur[YL_F_주문코드] / 10)) == 8)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if ((((((((((s->cur[YL_F_매수수익후예측2목표5금지봉수] > 0) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_직전거래방향] == 1)) && (s->cur[YL_F_직전거래손익] > 0)) && ((s->bar_n - s->cur[YL_F_직전청산봉]) <= s->cur[YL_F_매수수익후예측2목표5금지봉수])) && (s->cur[YL_F_지속목표방향] == 1)) && (s->cur[YL_F_지속저장목표5] > 0)) && (b->close > s->cur[YL_F_지속저장목표5])) && (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) < 3)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if ((((((((((s->cur[YL_F_목표5위빨강구조고점아래매수금지] == 1) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_지속저장목표5] > 0)) && (fabs((b->high - s->cur[YL_F_지속저장목표5])) <= (s->cur[YL_F_목표5근접틱] * b->price_scale))) && (s->cur[YL_F_지속목표방향] == 1)) && (s->cur[YL_F_빨강구조] == 1)) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (b->high < s->cur[YL_F_핵심지난상승최고가])) && (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) < 3)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((((s->cur[YL_F_FX초기화] == 1) || (s->cur[YL_F_지속저장목표1] != yl_lag(s, YL_F_지속저장목표1, 1))) || (s->cur[YL_F_지속저장목표2] != yl_lag(s, YL_F_지속저장목표2, 1))) || (s->cur[YL_F_지속저장목표3] != yl_lag(s, YL_F_지속저장목표3, 1))) || (s->cur[YL_F_지속저장목표4] != yl_lag(s, YL_F_지속저장목표4, 1))) || (s->cur[YL_F_지속저장목표5] != yl_lag(s, YL_F_지속저장목표5, 1))))                 {
                    s->cur[YL_F_목표유지시작봉] = s->bar_n;
                    s->cur[YL_F_목표5돌파봉] = 0;
                    s->cur[YL_F_목표5하향돌파봉] = 0;
                }
            if (((((((((s->cur[YL_F_연속목표5이탈매수금지봉수] > 0) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_지속저장목표5] > 0)) && ((s->bar_n - s->cur[YL_F_목표유지시작봉]) > s->cur[YL_F_연속목표5이탈매수금지봉수])) && (s->cur[YL_F_목표5돌파봉] > 0)) && (b->high < s->cur[YL_F_지속저장목표5])) && (s->cur[YL_F_S삼선비율] >= 100)) && (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) < 3)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((((((s->cur[YL_F_연속목표5이탈매도금지봉수] > 0) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_지속저장목표5] > 0)) && ((s->bar_n - s->cur[YL_F_목표유지시작봉]) > s->cur[YL_F_연속목표5이탈매도금지봉수])) && (s->cur[YL_F_목표5하향돌파봉] > 0)) && (b->low > s->cur[YL_F_지속저장목표5])) && (s->cur[YL_F_S삼선비율] >= 100)) && (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) >= 3)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((s->cur[YL_F_지속저장목표5] > 0) && (b->high > s->cur[YL_F_지속저장목표5]))) 
                s->cur[YL_F_목표5돌파봉] = s->bar_n;
            if (((s->cur[YL_F_지속저장목표5] > 0) && (b->low < s->cur[YL_F_지속저장목표5]))) 
                s->cur[YL_F_목표5하향돌파봉] = s->bar_n;
            if ((((((((((s->cur[YL_F_본장장대돌파틱] > 0) && (s->cur[YL_F_장대돌파확인봉수] > 0)) && (s->cur[YL_F_주문코드] > 0)) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 1)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 3)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_본장시가] > 0)) && (s->cur[YL_F_선행진폭율] >= s->cur[YL_F_장대돌파진폭율기준])) && (s->bar_n > s->cur[YL_F_장대돌파확인봉수])))                 {
                    s->cur[YL_F_본장장대양봉] = 0;
                    for (s->cur[YL_F_장대돌파j] = (0); s->cur[YL_F_장대돌파j] <= ((s->cur[YL_F_장대돌파확인봉수] - 1)); s->cur[YL_F_장대돌파j] += 1)                         {
                            if ((((yl_px(s, 'C', s->cur[YL_F_장대돌파j]) > yl_px(s, 'O', s->cur[YL_F_장대돌파j])) && (yl_px(s, 'O', s->cur[YL_F_장대돌파j]) < s->cur[YL_F_본장시가])) && ((yl_px(s, 'H', s->cur[YL_F_장대돌파j]) - yl_px(s, 'L', s->cur[YL_F_장대돌파j])) >= (s->cur[YL_F_본장장대돌파틱] * b->price_scale)))) 
                                s->cur[YL_F_본장장대양봉] = 1;
                        }
                    if ((s->cur[YL_F_본장장대양봉] == 1))                         {
                            s->cur[YL_F_주문코드] = 0;
                            s->cur[YL_F_가상매수진입] = 0;
                            s->cur[YL_F_가상매도진입] = 0;
                        }
                }
            if ((((((((((s->cur[YL_F_손실후매수금지] == 1) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_직전거래손익] < 0)) && (s->cur[YL_F_스윙전체폭비] > s->cur[YL_F_손실후매수스윙폭비])) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_당일본장고가] > 0)) && (b->close < (((s->cur[YL_F_당일본장고가] + s->cur[YL_F_당일본장저가])) / 2))) && (s->cur[YL_F_선행진폭율] > s->cur[YL_F_손실후매수진폭율])) && (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) < 3)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((((((((((s->cur[YL_F_런던전일저가아래압축매수금지] == 1) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_본장중] == 0)) && (s->cur[YL_F_본장기준봉분] < s->cur[YL_F_본장시작분])) && (s->cur[YL_F_런던시가] > 0)) && (s->cur[YL_F_전일런던저가] > 0)) && (b->close < s->cur[YL_F_런던시가])) && (b->close < s->cur[YL_F_전일런던저가])) && (s->cur[YL_F_통합카운트] > s->cur[YL_F_런던전일저가통합기준])) && (s->cur[YL_F_압축지속봉수] > 0)) && (s->cur[YL_F_압축지속봉수] < s->cur[YL_F_런던전일저가압축봉수])) && (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) < 3)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((((((((((s->cur[YL_F_런던전일저가아래압축매수금지] == 1) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_본장중] == 0)) && (s->cur[YL_F_본장기준봉분] < s->cur[YL_F_본장시작분])) && (s->cur[YL_F_런던시가] > 0)) && (s->cur[YL_F_전일런던고가] > 0)) && (b->close > s->cur[YL_F_런던시가])) && (b->close > s->cur[YL_F_전일런던고가])) && (s->cur[YL_F_통합카운트] < (-s->cur[YL_F_런던전일저가통합기준]))) && (s->cur[YL_F_압축지속봉수] > 0)) && (s->cur[YL_F_압축지속봉수] < s->cur[YL_F_런던전일저가압축봉수])) && (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) >= 3)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((((((s->cur[YL_F_통합100이탈매수금지] == 1) && (s->cur[YL_F_주문코드] > 0)) && (s->bar_n > (s->cur[YL_F_봉길이평균봉수] + 1))) && (yl_lag(s, YL_F_봉길이평균, 1) > 0)) && (s->cur[YL_F_봉길이] < yl_lag(s, YL_F_봉길이평균, 1))) && (s->cur[YL_F_통합100발생] == 1)) && (s->cur[YL_F_통합상태] < 100)) && (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) < 3)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((((((s->cur[YL_F_종료전수익진입금지분] > 0) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_최종청산손익_매수] > 0)) && (s->cur[YL_F_전일본장고가] > 0)) && (b->low > s->cur[YL_F_전일본장고가])) && (s->cur[YL_F_봉시작분] >= (((trunc((s->cur[YL_F_매매종료시각] / 10000)) * 60) + trunc((((s->cur[YL_F_매매종료시각] - (trunc((s->cur[YL_F_매매종료시각] / 10000)) * 10000))) / 100))) - s->cur[YL_F_종료전수익진입금지분]))) && (s->cur[YL_F_봉시작분] < ((trunc((s->cur[YL_F_매매종료시각] / 10000)) * 60) + trunc((((s->cur[YL_F_매매종료시각] - (trunc((s->cur[YL_F_매매종료시각] / 10000)) * 10000))) / 100))))) && (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) >= 3)))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((((((((s->cur[YL_F_시가돌파진입] == 1) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_본장시가] > 0)) && (s->cur[YL_F_선행유효] == 1)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= s->cur[YL_F_시가돌파경과분])) && ((b->high - b->low) >= (s->cur[YL_F_시가돌파장대틱] * b->price_scale))) && (s->cur[YL_F_S삼선비율] < s->cur[YL_F_시가돌파삼선기준])) && (s->cur[YL_F_스윙전체폭비] < s->cur[YL_F_시가돌파스윙폭기준])))                 {
                    if ((((((((((s->cur[YL_F_가상시작] != 1) && (b->close > b->open)) && (b->open < s->cur[YL_F_본장시가])) && (b->close > s->cur[YL_F_본장시가])) && (s->cur[YL_F_DI방향] > 0)) && (s->cur[YL_F_선행위치Cnt] > 0)) && (s->cur[YL_F_선행위치Cnt] < s->cur[YL_F_시가돌파위치기준])) && (s->cur[YL_F_선행기울기Cnt] > 0)) && (s->cur[YL_F_선행기울기Cnt] < s->cur[YL_F_시가돌파기울기기준])))                         {
                            s->cur[YL_F_주문코드] = ((25 * 10) + s->cur[YL_F_진입계약수]);
                            s->cur[YL_F_가상매수진입] = 1;
                            s->cur[YL_F_가상매도진입] = 0;
                        }
                    else if ((((((((((s->cur[YL_F_가상시작] != (-1)) && (b->close < b->open)) && (b->open > s->cur[YL_F_본장시가])) && (b->close < s->cur[YL_F_본장시가])) && (s->cur[YL_F_DI방향] < 0)) && (s->cur[YL_F_선행위치Cnt] < 0)) && (s->cur[YL_F_선행위치Cnt] > (-s->cur[YL_F_시가돌파위치기준]))) && (s->cur[YL_F_선행기울기Cnt] < 0)) && (s->cur[YL_F_선행기울기Cnt] > (-s->cur[YL_F_시가돌파기울기기준]))))                         {
                            s->cur[YL_F_주문코드] = ((28 * 10) + s->cur[YL_F_진입계약수]);
                            s->cur[YL_F_가상매도진입] = 1;
                            s->cur[YL_F_가상매수진입] = 0;
                        }
                }
            s->cur[YL_F_본장해제방향] = 0;
            if (((s->cur[YL_F_압축해제] == 1) && (s->cur[YL_F_해제방향] != 0))) 
                s->cur[YL_F_본장해제방향] = s->cur[YL_F_해제방향];
            else if ((s->cur[YL_F_지연확정방향] != 0)) 
                s->cur[YL_F_본장해제방향] = s->cur[YL_F_지연확정방향];
            if (((((((((((s->cur[YL_F_본장압축해제진입] == 1) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_세션진입수] == 0)) && (s->cur[YL_F_본장압축해제횟수] == 0)) && (s->cur[YL_F_본장해제방향] != 0)) && (((s->cur[YL_F_본장압축해제통합한도] <= 0) || (fabs(s->cur[YL_F_통합카운트]) < s->cur[YL_F_본장압축해제통합한도])))) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= s->cur[YL_F_본장압축해제초반제외분])) && ((((s->cur[YL_F_본장압축해제제외끝분] <= s->cur[YL_F_본장압축해제제외시작분]) || ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < s->cur[YL_F_본장압축해제제외시작분])) || ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= s->cur[YL_F_본장압축해제제외끝분])))))                 {
                    if (((((((s->cur[YL_F_본장해제방향] == 1) && (s->cur[YL_F_가상시작] != 1)) && (b->close >= s->cur[YL_F_본장시가])) && (((((s->cur[YL_F_본장압축해제감싸기제외] == 0) || (s->cur[YL_F_빨강구조] == 0)) || (s->cur[YL_F_핵심지난상승최고가] <= 0)) || (b->high >= s->cur[YL_F_핵심지난상승최고가])))) && (((((s->cur[YL_F_본장압축해제매수런던고점제외] == 0) || (s->cur[YL_F_핵심지난상승최고가] <= 0)) || (s->cur[YL_F_런던시가] <= 0)) || (s->cur[YL_F_핵심지난상승최고가] >= s->cur[YL_F_런던시가])))) && (((s->cur[YL_F_본장압축해제직전구간기준] <= 0) || (s->cur[YL_F_직전주요통합방향] != 1)))))                         {
                            s->cur[YL_F_주문코드] = ((31 * 10) + s->cur[YL_F_진입계약수]);
                            s->cur[YL_F_가상매수진입] = 1;
                            s->cur[YL_F_가상매도진입] = 0;
                        }
                    else if ((((((((s->cur[YL_F_본장해제방향] == (-1)) && (s->cur[YL_F_가상시작] != (-1))) && (b->close <= s->cur[YL_F_본장시가])) && (((s->cur[YL_F_본장압축해제매도삼선한도] <= 0) || (s->cur[YL_F_S삼선비율] <= s->cur[YL_F_본장압축해제매도삼선한도])))) && (((s->cur[YL_F_본장압축해제매도마감분] <= 0) || ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < s->cur[YL_F_본장압축해제매도마감분])))) && (((((s->cur[YL_F_본장압축해제매도신저가제외] == 0) || (s->cur[YL_F_당일본장저가] <= 0)) || (b->low > s->cur[YL_F_당일본장저가])) || (s->cur[YL_F_런던시가] <= s->cur[YL_F_본장시가])))) && (((s->cur[YL_F_본장압축해제직전구간기준] <= 0) || (s->cur[YL_F_직전주요통합방향] != (-1))))))                         {
                            s->cur[YL_F_주문코드] = ((34 * 10) + s->cur[YL_F_진입계약수]);
                            s->cur[YL_F_가상매도진입] = 1;
                            s->cur[YL_F_가상매수진입] = 0;
                        }
                }
            if ((s->cur[YL_F_압축지속봉수] > 0)) 
                s->cur[YL_F_압축종료후봉수] = 0;
            else 
                s->cur[YL_F_압축종료후봉수] = (s->cur[YL_F_압축종료후봉수] + 1);
            if ((((((((((((((((s->cur[YL_F_반전돌파매수진입] == 1) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_반전돌파횟수] == 0)) && (s->cur[YL_F_가상시작] != 1)) && (s->cur[YL_F_직전통합카운트값] <= (-s->cur[YL_F_반전돌파직전하락카운트]))) && (((s->cur[YL_F_반전돌파진입통합한도] <= 0) || (s->cur[YL_F_통합카운트] < s->cur[YL_F_반전돌파진입통합한도])))) && (s->cur[YL_F_선행위치Cnt] > 0)) && (s->cur[YL_F_선행위치Cnt] < s->cur[YL_F_반전돌파초기한도])) && (s->cur[YL_F_선행기울기Cnt] > 0)) && (s->cur[YL_F_선행기울기Cnt] < s->cur[YL_F_반전돌파초기한도])) && (((s->cur[YL_F_반전돌파압축후봉수] <= 0) || (s->cur[YL_F_압축종료후봉수] <= s->cur[YL_F_반전돌파압축후봉수])))) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (b->close > s->cur[YL_F_핵심지난상승최고가])))                 {
                    s->cur[YL_F_주문코드] = ((37 * 10) + s->cur[YL_F_진입계약수]);
                    s->cur[YL_F_가상매수진입] = 1;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if ((((((((((((((((s->cur[YL_F_반전돌파매도진입] == 1) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_반전돌파매도횟수] == 0)) && (s->cur[YL_F_가상시작] != (-1))) && (s->cur[YL_F_직전통합카운트값] >= s->cur[YL_F_반전돌파매도직전상승카운트])) && (((s->cur[YL_F_반전돌파진입통합한도] <= 0) || (s->cur[YL_F_통합카운트] > (-s->cur[YL_F_반전돌파진입통합한도]))))) && (s->cur[YL_F_선행위치Cnt] < 0)) && (s->cur[YL_F_선행위치Cnt] > (-s->cur[YL_F_반전돌파초기한도]))) && (s->cur[YL_F_선행기울기Cnt] < 0)) && (s->cur[YL_F_선행기울기Cnt] > (-s->cur[YL_F_반전돌파초기한도]))) && (((s->cur[YL_F_반전돌파압축후봉수] <= 0) || (s->cur[YL_F_압축종료후봉수] <= s->cur[YL_F_반전돌파압축후봉수])))) && (s->cur[YL_F_핵심지난하락최저가] > 0)) && (b->close < s->cur[YL_F_핵심지난하락최저가])))                 {
                    s->cur[YL_F_주문코드] = ((40 * 10) + s->cur[YL_F_진입계약수]);
                    s->cur[YL_F_가상매도진입] = 1;
                    s->cur[YL_F_가상매수진입] = 0;
                }
            s->cur[YL_F_이차상승신호] = 0;
            if ((s->cur[YL_F_FX초기화] == 1))                 {
                    s->cur[YL_F_이차A] = 0;
                    s->cur[YL_F_이차B] = 0;
                    s->cur[YL_F_이차대기] = 0;
                    s->cur[YL_F_이차비상승저가] = b->low;
                    s->cur[YL_F_이차진행시작저가] = 0;
                    s->cur[YL_F_이차진행통합최고] = 0;
                    s->cur[YL_F_이차시작저가] = 0;
                }
            else                 {
                    if (((s->cur[YL_F_선행기울기Cnt] > 0) && (yl_lag(s, YL_F_선행기울기Cnt, 1) <= 0)))                         {
                            s->cur[YL_F_이차진행시작저가] = yl_min(s->cur[YL_F_이차비상승저가], b->low);
                            s->cur[YL_F_이차진행통합최고] = s->cur[YL_F_통합카운트];
                            s->cur[YL_F_이차진행고가] = b->high;
                        }
                    else if ((s->cur[YL_F_선행기울기Cnt] > 0))                         {
                            s->cur[YL_F_이차진행통합최고] = yl_max(s->cur[YL_F_이차진행통합최고], s->cur[YL_F_통합카운트]);
                            s->cur[YL_F_이차진행고가] = yl_max(s->cur[YL_F_이차진행고가], b->high);
                        }
                    if (((s->cur[YL_F_선행기울기Cnt] <= 0) && (yl_lag(s, YL_F_선행기울기Cnt, 1) > 0))) 
                        s->cur[YL_F_이차비상승저가] = b->low;
                    else if ((s->cur[YL_F_선행기울기Cnt] <= 0)) 
                        s->cur[YL_F_이차비상승저가] = yl_min(s->cur[YL_F_이차비상승저가], b->low);
                    if (((s->cur[YL_F_선행기울기Cnt] <= 0) && (yl_lag(s, YL_F_선행기울기Cnt, 1) >= s->cur[YL_F_이차상승기울기기준])))                         {
                            if ((s->cur[YL_F_이차진행통합최고] >= s->cur[YL_F_이차1차통합기준]))                                 {
                                    s->cur[YL_F_이차A] = yl_lag(s, YL_F_선행기울기Cnt, 1);
                                    s->cur[YL_F_이차B] = 0;
                                    s->cur[YL_F_이차대기] = 1;
                                    s->cur[YL_F_이차시작저가] = s->cur[YL_F_이차진행시작저가];
                                    s->cur[YL_F_이차고가] = s->cur[YL_F_이차진행고가];
                                    s->cur[YL_F_이차조정저가] = b->low;
                                }
                            else                                 {
                                    s->cur[YL_F_이차A] = 0;
                                    s->cur[YL_F_이차B] = 0;
                                    s->cur[YL_F_이차대기] = 0;
                                }
                        }
                    if (((s->cur[YL_F_이차대기] == 1) && (s->cur[YL_F_선행기울기Cnt] < 0))) 
                        s->cur[YL_F_이차B] = yl_max(s->cur[YL_F_이차B], (-s->cur[YL_F_선행기울기Cnt]));
                    if ((((s->cur[YL_F_이차대기] == 1) && (s->cur[YL_F_이차시작저가] > 0)) && (b->low < s->cur[YL_F_이차시작저가]))) 
                        s->cur[YL_F_이차대기] = 0;
                    if ((s->cur[YL_F_이차대기] == 1))                         {
                            s->cur[YL_F_이차조정저가] = yl_min(s->cur[YL_F_이차조정저가], b->low);
                            if ((b->high > s->cur[YL_F_이차고가])) 
                                s->cur[YL_F_이차고가] = b->high;
                        }
                    if (((((((((s->cur[YL_F_이차대기] == 1) && (s->cur[YL_F_이차A] > 0)) && (s->cur[YL_F_이차B] >= ((s->cur[YL_F_이차A] * s->cur[YL_F_이차조정기울기비율]) / 100))) && (s->cur[YL_F_선행기울기Cnt] >= 1)) && (s->cur[YL_F_선행기울기Cnt] <= s->cur[YL_F_이차진입창봉수])) && (s->cur[YL_F_통합카운트] >= s->cur[YL_F_이차통합카운트기준])) && (s->cur[YL_F_S삼선비율] < s->cur[YL_F_이차삼선비율한도])) && (s->cur[YL_F_스윙전체폭비] < s->cur[YL_F_이차스윙폭한도]))) 
                        s->cur[YL_F_이차상승신호] = 1;
                }
            if (((((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_이차A] >= 40)) && ((s->cur[YL_F_이차B] * 100) < (s->cur[YL_F_이차A] * 75))) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 30)))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                }
            if ((((((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_이차A] >= 30)) && (s->cur[YL_F_이차A] < 40)) && ((s->cur[YL_F_이차B] * 100) >= (s->cur[YL_F_이차A] * 75))) && (s->cur[YL_F_이차B] < s->cur[YL_F_이차A])))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                }
            if ((((((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_이차A] < 30)) && ((s->cur[YL_F_이차B] * 100) < (s->cur[YL_F_이차A] * 75))) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 30)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 60)))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                }
            if ((((((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_이차A] < 30)) && ((s->cur[YL_F_이차B] * 100) >= (s->cur[YL_F_이차A] * 75))) && (s->cur[YL_F_이차B] < s->cur[YL_F_이차A])) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 60)))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                }
            if ((((((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_이차A] < 30)) && (s->cur[YL_F_이차B] >= s->cur[YL_F_이차A])) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 120)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 150)))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                }
            if (((((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_이차A] >= 30)) && (s->cur[YL_F_이차A] < 40)) && ((s->cur[YL_F_이차B] * 100) < (s->cur[YL_F_이차A] * 75))))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                }
            if (((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_이차A] >= 40)))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                }
            if ((s->cur[YL_F_FX초기화] == 1))                 {
                    s->cur[YL_F_실청산봉] = (-999);
                    s->cur[YL_F_실세션진입수] = 0;
                    s->cur[YL_F_실직전손익] = 0;
                }
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_실세션손익] = 0;
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_실직전수익] = 0;
            if (((s->cur[YL_F_FX초기화] == 1) && (yl_lag(s, YL_F_실직전방향, 1) != 0)))                 {
                    s->cur[YL_F_전세션마지막방향] = yl_lag(s, YL_F_실직전방향, 1);
                    s->cur[YL_F_전세션마지막수익] = yl_lag(s, YL_F_실직전수익, 1);
                }
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_실직전방향] = 0;
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_실직전최대틱] = 0;
            if (((yl_lag(s, YL_F_MarketPosition, 1) != 0) && (b->market != yl_lag(s, YL_F_MarketPosition, 1)))) 
                s->cur[YL_F_실청산봉] = s->bar_n;
            if (((b->market != 0) && (b->market != yl_lag(s, YL_F_MarketPosition, 1)))) 
                s->cur[YL_F_실세션진입수] = (s->cur[YL_F_실세션진입수] + 1);
            if (((((((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_이차A] < 30)) && ((s->cur[YL_F_이차B] * 100) >= (s->cur[YL_F_이차A] * 75))) && (s->cur[YL_F_이차B] < s->cur[YL_F_이차A])) && (s->cur[YL_F_실청산봉] > 0)) && ((s->bar_n - s->cur[YL_F_실청산봉]) < 15)))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                }
            if (((((((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_이차A] < 30)) && ((s->cur[YL_F_이차B] * 100) >= (s->cur[YL_F_이차A] * 75))) && (s->cur[YL_F_이차B] < s->cur[YL_F_이차A])) && (s->cur[YL_F_실세션진입수] == 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 120)))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                }
            if (((((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_이차A] < 30)) && (s->cur[YL_F_이차B] >= s->cur[YL_F_이차A])) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 10)))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                }
            if (((((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_이차A] < 30)) && (s->cur[YL_F_이차B] >= s->cur[YL_F_이차A])) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 170)))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                }
            if ((((((s->cur[YL_F_이차상승신호] == 1) && ((s->cur[YL_F_이차B] * 100) >= (s->cur[YL_F_이차A] * 75))) && (s->cur[YL_F_이차B] < s->cur[YL_F_이차A])) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 90)) && (s->cur[YL_F_S삼선비율] <= 30)))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                }
            s->cur[YL_F_이차되돌림] = 0;
            if ((((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_이차고가] > s->cur[YL_F_이차시작저가])) && (s->cur[YL_F_이차시작저가] > 0))) 
                s->cur[YL_F_이차되돌림] = ((((s->cur[YL_F_이차고가] - s->cur[YL_F_이차조정저가])) / ((s->cur[YL_F_이차고가] - s->cur[YL_F_이차시작저가]))) * 100);
            if (((((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_이차A] < 30)) && ((s->cur[YL_F_이차B] * 100) < (s->cur[YL_F_이차A] * 75))) && (((((s->cur[YL_F_이차되돌림] >= 50) && (s->cur[YL_F_이차되돌림] < 61.8))) || (s->cur[YL_F_이차되돌림] >= 80)))))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                }
            if (((((((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_이차A] < 30)) && ((s->cur[YL_F_이차B] * 100) >= (s->cur[YL_F_이차A] * 75))) && (s->cur[YL_F_이차B] < s->cur[YL_F_이차A])) && (s->cur[YL_F_이차되돌림] >= 50)) && (s->cur[YL_F_이차되돌림] < 61.8)))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                }
            if ((((((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_실세션진입수] > 0)) && (s->cur[YL_F_실직전손익] > 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 90)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 110)))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                }
            if ((((((((((s->cur[YL_F_이차직전손실] == 1) && (s->cur[YL_F_이차상승매수진입] == 1)) && (s->cur[YL_F_이차상승신호] == 1)) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_가상시작] != 1)) && (((s->cur[YL_F_수익비늦은매수제외분] <= 0) || (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_수익비늦은매수제외분]))))) && (((s->cur[YL_F_이차허용끝분] <= 0) || ((((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= s->cur[YL_F_이차허용시작분]) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < s->cur[YL_F_이차허용끝분])))))))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                    s->cur[YL_F_이차직전손실] = 0;
                }
            if ((((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_실세션진입수] >= 3)) && (s->cur[YL_F_실세션손익] < 0)))                 {
                    s->cur[YL_F_이차상승신호] = 0;
                    s->cur[YL_F_이차대기] = 0;
                }
            s->cur[YL_F_이차되돌림] = 0;
            if ((((s->cur[YL_F_이차상승신호] == 1) && (s->cur[YL_F_이차고가] > s->cur[YL_F_이차시작저가])) && (s->cur[YL_F_이차시작저가] > 0))) 
                s->cur[YL_F_이차되돌림] = ((((s->cur[YL_F_이차고가] - s->cur[YL_F_이차조정저가])) / ((s->cur[YL_F_이차고가] - s->cur[YL_F_이차시작저가]))) * 100);
            if (((((((((s->cur[YL_F_이차상승매수진입] == 1) && (s->cur[YL_F_이차상승신호] == 1)) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_가상시작] != 1)) && (((s->cur[YL_F_수익비늦은매수제외분] <= 0) || (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_수익비늦은매수제외분]))))) && (((s->cur[YL_F_이차허용끝분] <= 0) || ((((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= s->cur[YL_F_이차허용시작분]) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < s->cur[YL_F_이차허용끝분])))))))                 {
                    s->cur[YL_F_주문코드] = ((43 * 10) + s->cur[YL_F_진입계약수]);
                    s->cur[YL_F_가상매수진입] = 1;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            s->cur[YL_F_하락신호] = 0;
            if ((s->cur[YL_F_FX초기화] == 1))                 {
                    s->cur[YL_F_하락A] = 0;
                    s->cur[YL_F_하락B] = 0;
                    s->cur[YL_F_하락대기] = 0;
                    s->cur[YL_F_하락비하락고가] = b->high;
                    s->cur[YL_F_하락진행시작고가] = 0;
                    s->cur[YL_F_하락진행통합최저] = 0;
                    s->cur[YL_F_하락시작고가] = 0;
                }
            else                 {
                    if (((s->cur[YL_F_선행기울기Cnt] < 0) && (yl_lag(s, YL_F_선행기울기Cnt, 1) >= 0)))                         {
                            s->cur[YL_F_하락진행시작고가] = yl_max(s->cur[YL_F_하락비하락고가], b->high);
                            s->cur[YL_F_하락진행통합최저] = s->cur[YL_F_통합카운트];
                            s->cur[YL_F_하락진행저가] = b->low;
                        }
                    else if ((s->cur[YL_F_선행기울기Cnt] < 0))                         {
                            s->cur[YL_F_하락진행통합최저] = yl_min(s->cur[YL_F_하락진행통합최저], s->cur[YL_F_통합카운트]);
                            s->cur[YL_F_하락진행저가] = yl_min(s->cur[YL_F_하락진행저가], b->low);
                        }
                    if (((s->cur[YL_F_선행기울기Cnt] >= 0) && (yl_lag(s, YL_F_선행기울기Cnt, 1) < 0))) 
                        s->cur[YL_F_하락비하락고가] = b->high;
                    else if ((s->cur[YL_F_선행기울기Cnt] >= 0)) 
                        s->cur[YL_F_하락비하락고가] = yl_max(s->cur[YL_F_하락비하락고가], b->high);
                    if (((s->cur[YL_F_선행기울기Cnt] >= 0) && ((-yl_lag(s, YL_F_선행기울기Cnt, 1)) >= s->cur[YL_F_하락기울기기준])))                         {
                            if ((s->cur[YL_F_하락진행통합최저] <= (-s->cur[YL_F_하락1차통합기준])))                                 {
                                    s->cur[YL_F_하락A] = (-yl_lag(s, YL_F_선행기울기Cnt, 1));
                                    s->cur[YL_F_하락B] = 0;
                                    s->cur[YL_F_하락대기] = 1;
                                    s->cur[YL_F_하락시작고가] = s->cur[YL_F_하락진행시작고가];
                                    s->cur[YL_F_하락저가] = s->cur[YL_F_하락진행저가];
                                    s->cur[YL_F_하락조정고가] = b->high;
                                }
                            else                                 {
                                    s->cur[YL_F_하락A] = 0;
                                    s->cur[YL_F_하락B] = 0;
                                    s->cur[YL_F_하락대기] = 0;
                                }
                        }
                    if (((s->cur[YL_F_하락대기] == 1) && (s->cur[YL_F_선행기울기Cnt] > 0))) 
                        s->cur[YL_F_하락B] = yl_max(s->cur[YL_F_하락B], s->cur[YL_F_선행기울기Cnt]);
                    if ((((s->cur[YL_F_하락대기] == 1) && (s->cur[YL_F_하락시작고가] > 0)) && (b->high > s->cur[YL_F_하락시작고가]))) 
                        s->cur[YL_F_하락대기] = 0;
                    if ((s->cur[YL_F_하락대기] == 1))                         {
                            s->cur[YL_F_하락조정고가] = yl_max(s->cur[YL_F_하락조정고가], b->high);
                            if ((b->low < s->cur[YL_F_하락저가])) 
                                s->cur[YL_F_하락저가] = b->low;
                        }
                    if (((((((((s->cur[YL_F_하락대기] == 1) && (s->cur[YL_F_하락A] > 0)) && (s->cur[YL_F_하락B] >= ((s->cur[YL_F_하락A] * s->cur[YL_F_이차조정기울기비율]) / 100))) && (s->cur[YL_F_선행기울기Cnt] <= (-1))) && (s->cur[YL_F_선행기울기Cnt] >= (-s->cur[YL_F_이차진입창봉수]))) && (s->cur[YL_F_통합카운트] <= (-s->cur[YL_F_이차통합카운트기준]))) && (s->cur[YL_F_S삼선비율] < s->cur[YL_F_이차삼선비율한도])) && (s->cur[YL_F_스윙전체폭비] < s->cur[YL_F_이차스윙폭한도]))) 
                        s->cur[YL_F_하락신호] = 1;
                }
            s->cur[YL_F_하락되돌림] = 0;
            if ((((s->cur[YL_F_하락신호] == 1) && (s->cur[YL_F_하락시작고가] > s->cur[YL_F_하락저가])) && (s->cur[YL_F_하락저가] > 0))) 
                s->cur[YL_F_하락되돌림] = ((((s->cur[YL_F_하락조정고가] - s->cur[YL_F_하락저가])) / ((s->cur[YL_F_하락시작고가] - s->cur[YL_F_하락저가]))) * 100);
            if (((s->cur[YL_F_하락신호] == 1) && (((s->cur[YL_F_하락A] >= 40) || (((s->cur[YL_F_하락A] >= 30) && (s->cur[YL_F_하락B] < s->cur[YL_F_하락A])))))))                 {
                    s->cur[YL_F_하락신호] = 0;
                    s->cur[YL_F_하락대기] = 0;
                }
            if (((((s->cur[YL_F_하락신호] == 1) && (s->cur[YL_F_하락A] < 30)) && (s->cur[YL_F_하락B] >= s->cur[YL_F_하락A])) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 10)))                 {
                    s->cur[YL_F_하락신호] = 0;
                    s->cur[YL_F_하락대기] = 0;
                }
            if (((((s->cur[YL_F_하락신호] == 1) && (s->cur[YL_F_하락본장초반금지분] > 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < s->cur[YL_F_하락본장초반금지분])))                 {
                    s->cur[YL_F_하락신호] = 0;
                    s->cur[YL_F_하락대기] = 0;
                }
            if (((s->cur[YL_F_하락신호] == 1) && (s->cur[YL_F_하락B] >= s->cur[YL_F_하락A])))                 {
                    s->cur[YL_F_하락신호] = 0;
                    s->cur[YL_F_하락대기] = 0;
                }
            if ((((((s->cur[YL_F_하락신호] == 1) && (s->cur[YL_F_하락A] < 30)) && ((s->cur[YL_F_하락B] * 100) < (s->cur[YL_F_하락A] * 75))) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 30)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 60)))                 {
                    s->cur[YL_F_하락신호] = 0;
                    s->cur[YL_F_하락대기] = 0;
                }
            if (((((((s->cur[YL_F_하락신호] == 1) && (s->cur[YL_F_하락A] < 30)) && ((s->cur[YL_F_하락B] * 100) < (s->cur[YL_F_하락A] * 75))) && (s->cur[YL_F_하락되돌림] >= 61.8)) && (s->cur[YL_F_하락되돌림] < 80)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 60)))                 {
                    s->cur[YL_F_하락신호] = 0;
                    s->cur[YL_F_하락대기] = 0;
                }
            if ((((((s->cur[YL_F_하락신호] == 1) && (s->cur[YL_F_하락A] < 30)) && ((s->cur[YL_F_하락B] * 100) >= (s->cur[YL_F_하락A] * 75))) && (s->cur[YL_F_하락B] < s->cur[YL_F_하락A])) && (s->cur[YL_F_하락되돌림] >= 80)))                 {
                    s->cur[YL_F_하락신호] = 0;
                    s->cur[YL_F_하락대기] = 0;
                }
            if (((((((s->cur[YL_F_하락신호] == 1) && (s->cur[YL_F_하락A] < 30)) && ((s->cur[YL_F_하락B] * 100) >= (s->cur[YL_F_하락A] * 75))) && (s->cur[YL_F_하락B] < s->cur[YL_F_하락A])) && (s->cur[YL_F_하락되돌림] >= 50)) && (s->cur[YL_F_하락되돌림] < 61.8)))                 {
                    s->cur[YL_F_하락신호] = 0;
                    s->cur[YL_F_하락대기] = 0;
                }
            if (((((((s->cur[YL_F_하락신호] == 1) && (s->cur[YL_F_하락A] < 30)) && ((s->cur[YL_F_하락B] * 100) >= (s->cur[YL_F_하락A] * 75))) && (s->cur[YL_F_하락B] < s->cur[YL_F_하락A])) && (s->cur[YL_F_하락되돌림] >= 61.8)) && (s->cur[YL_F_하락되돌림] < 80)))                 {
                    s->cur[YL_F_하락신호] = 0;
                    s->cur[YL_F_하락대기] = 0;
                }
            if (((((s->cur[YL_F_하락신호] == 1) && (s->cur[YL_F_하락A] < 30)) && ((s->cur[YL_F_하락B] * 100) < (s->cur[YL_F_하락A] * 75))) && (s->cur[YL_F_하락되돌림] >= 80)))                 {
                    s->cur[YL_F_하락신호] = 0;
                    s->cur[YL_F_하락대기] = 0;
                }
            if (((((((s->cur[YL_F_하락신호] == 1) && (s->cur[YL_F_하락A] < 30)) && ((s->cur[YL_F_하락B] * 100) >= (s->cur[YL_F_하락A] * 75))) && (s->cur[YL_F_하락B] < s->cur[YL_F_하락A])) && (s->cur[YL_F_실청산봉] > 0)) && ((s->bar_n - s->cur[YL_F_실청산봉]) < 15)))                 {
                    s->cur[YL_F_하락신호] = 0;
                    s->cur[YL_F_하락대기] = 0;
                }
            if (((((((((s->cur[YL_F_하락직전손실] == 1) && (s->cur[YL_F_하락매도진입] == 1)) && (s->cur[YL_F_하락신호] == 1)) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_가상시작] != (-1))) && (((s->cur[YL_F_수익비늦은매도제외분] <= 0) || (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_수익비늦은매도제외분]))))))                 {
                    s->cur[YL_F_하락신호] = 0;
                    s->cur[YL_F_하락대기] = 0;
                    s->cur[YL_F_하락직전손실] = 0;
                }
            if ((((((((s->cur[YL_F_하락매도진입] == 1) && (s->cur[YL_F_하락신호] == 1)) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_가상시작] != (-1))) && (((s->cur[YL_F_수익비늦은매도제외분] <= 0) || (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_수익비늦은매도제외분]))))))                 {
                    s->cur[YL_F_주문코드] = ((46 * 10) + s->cur[YL_F_진입계약수]);
                    s->cur[YL_F_가상매도진입] = 1;
                    s->cur[YL_F_가상매수진입] = 0;
                }
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_게이지매도횟수] = 0;
            s->cur[YL_F_게이지돌파매도] = 0;
            s->cur[YL_F_게이지폭값] = (s->cur[YL_F_핵심지난하락최고가] - s->cur[YL_F_핵심지난하락최저가]);
            if (((((s->cur[YL_F_게이지폭값] > 0) && (s->cur[YL_F_핵심지난하락최저가] > 0)) && (((s->cur[YL_F_파랑구조] == 1) || (((s->cur[YL_F_핵심지난하락_618] > 0) && (s->cur[YL_F_핵심지난하락_618] < s->cur[YL_F_핵심지난상승최저가])))))) && (b->close > s->cur[YL_F_핵심지난하락최저가]))) 
                s->cur[YL_F_게이지돌파매도] = yl_max(s->cur[YL_F_게이지돌파매도], (100 * ((1 - (((b->close - s->cur[YL_F_핵심지난하락최저가])) / ((s->cur[YL_F_게이지폭값] * 0.382)))))));
            if (((((s->cur[YL_F_게이지폭값] > 0) && (s->cur[YL_F_돌파매도조건] == 1)) && (s->cur[YL_F_핵심지난상승최저가] > 0)) && (b->close > s->cur[YL_F_핵심지난상승최저가]))) 
                s->cur[YL_F_게이지돌파매도] = yl_max(s->cur[YL_F_게이지돌파매도], (100 * ((1 - (((b->close - s->cur[YL_F_핵심지난상승최저가])) / ((s->cur[YL_F_게이지폭값] * 0.382)))))));
            s->cur[YL_F_게이지추세매도] = (((s->cur[YL_F_하락대기] == 1) && (s->cur[YL_F_하락A] > 0)) ? 1 : 0);
            s->cur[YL_F_게이지전환매도] = ((s->cur[YL_F_통합카운트] >= 10) ? 1 : 0);
            s->cur[YL_F_게이지돌파매수] = 0;
            s->cur[YL_F_게이지폭값] = (s->cur[YL_F_핵심지난상승최고가] - s->cur[YL_F_핵심지난상승최저가]);
            if (((((s->cur[YL_F_게이지폭값] > 0) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (((s->cur[YL_F_빨강구조] == 1) || (((s->cur[YL_F_핵심지난상승_382] > 0) && (s->cur[YL_F_핵심지난상승_382] > s->cur[YL_F_핵심지난하락최고가])))))) && (b->close < s->cur[YL_F_핵심지난상승최고가]))) 
                s->cur[YL_F_게이지돌파매수] = yl_max(s->cur[YL_F_게이지돌파매수], (100 * ((1 - (((s->cur[YL_F_핵심지난상승최고가] - b->close)) / ((s->cur[YL_F_게이지폭값] * 0.382)))))));
            if (((((s->cur[YL_F_게이지폭값] > 0) && (s->cur[YL_F_돌파매수조건] == 1)) && (s->cur[YL_F_핵심지난하락최고가] > 0)) && (b->close < s->cur[YL_F_핵심지난하락최고가]))) 
                s->cur[YL_F_게이지돌파매수] = yl_max(s->cur[YL_F_게이지돌파매수], (100 * ((1 - (((s->cur[YL_F_핵심지난하락최고가] - b->close)) / ((s->cur[YL_F_게이지폭값] * 0.382)))))));
            s->cur[YL_F_게이지매수있음] = 0;
            if (((s->cur[YL_F_압축지속봉수] > 0) && (s->cur[YL_F_압축위치구분] >= 0))) 
                s->cur[YL_F_게이지매수있음] = 1;
            if ((s->cur[YL_F_게이지돌파매수] > 0)) 
                s->cur[YL_F_게이지매수있음] = 1;
            if (((s->cur[YL_F_이차대기] == 1) && (s->cur[YL_F_이차A] > 0))) 
                s->cur[YL_F_게이지매수있음] = 1;
            if ((s->cur[YL_F_통합카운트] <= (-10))) 
                s->cur[YL_F_게이지매수있음] = 1;
            s->cur[YL_F_게이지매도신호] = 0;
            if (((((((((((((((s->cur[YL_F_게이지매도진입] == 1) && (s->cur[YL_F_게이지돌파매도] > 0)) && (s->cur[YL_F_게이지추세매도] == 1)) && (s->cur[YL_F_게이지전환매도] == 1)) && (s->cur[YL_F_선행기울기Cnt] <= (-1))) && (s->cur[YL_F_선행기울기Cnt] >= (-s->cur[YL_F_게이지매도초기봉수]))) && (s->cur[YL_F_선행위치Cnt] <= (-1))) && (s->cur[YL_F_선행위치Cnt] >= (-s->cur[YL_F_게이지매도초기봉수]))) && (s->cur[YL_F_DI방향] < 0)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_본장시가] > 0)) && (b->close < s->cur[YL_F_본장시가])) && (s->cur[YL_F_게이지매도횟수] == 0)) && (s->cur[YL_F_게이지매수있음] == 0))) 
                s->cur[YL_F_게이지매도신호] = 1;
            if ((((((s->cur[YL_F_게이지매도신호] == 1) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_가상시작] != (-1))) && (((s->cur[YL_F_수익비늦은매도제외분] <= 0) || (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_수익비늦은매도제외분]))))))                 {
                    s->cur[YL_F_주문코드] = ((49 * 10) + s->cur[YL_F_진입계약수]);
                    s->cur[YL_F_가상매도진입] = 1;
                    s->cur[YL_F_가상매수진입] = 0;
                }
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_시가이탈매도횟수] = 0;
            if (((s->cur[YL_F_하락대기] == 1) && (s->cur[YL_F_하락A] > 0))) 
                s->cur[YL_F_추세매도지속봉] = (s->cur[YL_F_추세매도지속봉] + 1);
            else 
                s->cur[YL_F_추세매도지속봉] = 0;
            s->cur[YL_F_시가이탈후보] = 0;
            if (((((((((((((((((((((((s->cur[YL_F_시가이탈매도진입] == 1) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_가상시작] != (-1))) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_본장시가] > 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= s->cur[YL_F_시가이탈경과분])) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 60)) && (s->cur[YL_F_실세션진입수] == 0)) && (s->cur[YL_F_시가이탈매도횟수] == 0)) && (s->cur[YL_F_통합카운트] <= (-1))) && (s->cur[YL_F_통합카운트] >= (-s->cur[YL_F_시가이탈초기봉수]))) && (s->cur[YL_F_선행기울기Cnt] <= (-1))) && (s->cur[YL_F_선행기울기Cnt] >= (-s->cur[YL_F_시가이탈초기봉수]))) && (b->close < s->cur[YL_F_본장시가])) && (yl_at(yl_hi(s, "C", 5), 1) >= s->cur[YL_F_본장시가])) && (((s->cur[YL_F_이차대기] == 0) || (s->cur[YL_F_이차A] <= 0)))) && (s->cur[YL_F_추세매도지속봉] < 60)) && (((((s->cur[YL_F_하락대기] == 0) || (s->cur[YL_F_하락A] <= 0)) || (s->cur[YL_F_게이지돌파매수] > 0)) || (s->cur[YL_F_게이지돌파매도] <= 0)))) && (((((s->cur[YL_F_전일본장고가] <= 0) || (yl_lag(s, YL_F_당일본장저가, 1) >= s->cur[YL_F_전일본장고가])) || (yl_lag(s, YL_F_당일본장저가, 1) < (s->cur[YL_F_전일본장고가] - 5))) || (b->low <= s->cur[YL_F_전일본장고가])))) && (((s->cur[YL_F_런던시가] <= 0) || (b->low <= s->cur[YL_F_런던시가])))) && (((s->cur[YL_F_수익비늦은매도제외분] <= 0) || (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_수익비늦은매도제외분])))))) 
                s->cur[YL_F_시가이탈후보] = 1;
            if (((((s->cur[YL_F_시가이탈후보] == 1) && (yl_lag(s, YL_F_봉길이평균, 1) > 0)) && (s->cur[YL_F_봉길이] >= (yl_lag(s, YL_F_봉길이평균, 1) * 3))) && ((yl_min(b->open, b->close) - b->low) >= (s->cur[YL_F_봉길이] * 0.3)))) 
                s->cur[YL_F_시가이탈매도횟수] = 1;
            else if ((s->cur[YL_F_시가이탈후보] == 1))                 {
                    s->cur[YL_F_주문코드] = ((52 * 10) + s->cur[YL_F_진입계약수]);
                    s->cur[YL_F_가상매도진입] = 1;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_시가이탈진단] = (((((s->cur[YL_F_하락대기] == 1) && (s->cur[YL_F_하락A] > 0)) ? 6 : 0) + ((s->cur[YL_F_게이지돌파매수] > 0) ? 2 : ((s->cur[YL_F_게이지돌파매도] > 0) ? 4 : 0))) + (((yl_lag(s, YL_F_당일본장고가, 1) - s->cur[YL_F_본장시가]) >= 10) ? 1 : 0));
                }
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_시가회복매수횟수] = 0;
            s->cur[YL_F_시가아래봉수] = 0;
            if (((s->cur[YL_F_본장중] == 1) && (s->cur[YL_F_본장시가] > 0)))                 {
                    for (s->cur[YL_F_회복k] = (1); s->cur[YL_F_회복k] <= (15); s->cur[YL_F_회복k] += 1)                         {
                            if ((yl_px(s, 'H', s->cur[YL_F_회복k]) < s->cur[YL_F_본장시가])) 
                                s->cur[YL_F_시가아래봉수] = (s->cur[YL_F_시가아래봉수] + 1);
                        }
                }
            if (((((((((((((((((((((((((((s->cur[YL_F_시가회복매수진입] == 1) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_가상시작] != 1)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_본장시가] > 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 60)) && (s->cur[YL_F_시가회복매수횟수] == 0)) && (b->close > s->cur[YL_F_본장시가])) && (yl_at(yl_lo(s, "C", 5), 1) <= s->cur[YL_F_본장시가])) && (s->cur[YL_F_선행위치Cnt] >= 1)) && (s->cur[YL_F_선행위치Cnt] < 12)) && (s->cur[YL_F_선행기울기Cnt] >= 1)) && (s->cur[YL_F_선행기울기Cnt] < 12)) && (s->cur[YL_F_통합카운트] >= 1)) && (s->cur[YL_F_통합카운트] <= 9)) && (s->cur[YL_F_통합상태] > 0)) && (s->cur[YL_F_통합색상] != yl_rgb(210, 210, 210))) && (s->cur[YL_F_스윙전체폭비] < 100)) && (s->cur[YL_F_본장진폭율] > 0)) && (s->cur[YL_F_본장진폭율] < 80)) && (s->cur[YL_F_본장거래량율] > 100)) && (s->cur[YL_F_직전통합카운트값] <= (-5))) && (((s->cur[YL_F_지속저장목표5] <= 0) || (b->close >= s->cur[YL_F_지속저장목표5])))) && (s->cur[YL_F_시가아래봉수] >= 3)) && (((s->cur[YL_F_수익비늦은매수제외분] <= 0) || (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_수익비늦은매수제외분]))))))                 {
                    s->cur[YL_F_주문코드] = ((55 * 10) + s->cur[YL_F_진입계약수]);
                    s->cur[YL_F_가상매수진입] = 1;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_흡수돌파횟수] = 0;
            if ((((((((((s->cur[YL_F_흡수돌파진입] == 1) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_흡수돌파횟수] == 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 30)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 120)) && (yl_lag(s, YL_F_흡수지속봉, 1) >= 5)) && (b->volume >= (yl_at(yl_avg(s, "V", 20), 1) * 1.5))))                 {
                    if ((((s->cur[YL_F_가상시작] != 1) && (yl_lag(s, YL_F_당일본장고가, 1) > 0)) && (b->close > yl_lag(s, YL_F_당일본장고가, 1))))                         {
                            s->cur[YL_F_주문코드] = ((61 * 10) + s->cur[YL_F_진입계약수]);
                            s->cur[YL_F_가상매수진입] = 1;
                            s->cur[YL_F_가상매도진입] = 0;
                        }
                    else if (((((s->cur[YL_F_흡수돌파매도사용] == 1) && (s->cur[YL_F_가상시작] != (-1))) && (yl_lag(s, YL_F_당일본장저가, 1) > 0)) && (b->close < yl_lag(s, YL_F_당일본장저가, 1))))                         {
                            s->cur[YL_F_주문코드] = ((64 * 10) + s->cur[YL_F_진입계약수]);
                            s->cur[YL_F_가상매도진입] = 1;
                            s->cur[YL_F_가상매수진입] = 0;
                        }
                }
            if ((s->cur[YL_F_FX초기화] == 1))                 {
                    s->cur[YL_F_확장반등횟수] = 0;
                    s->cur[YL_F_확장반등대기] = 0;
                }
            if (((s->cur[YL_F_본장중] == 1) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) == 30))) 
                s->cur[YL_F_확장반등대기] = ((((((s->cur[YL_F_본장진폭율] >= 100) && (s->cur[YL_F_본장거래량율] >= 100)) && (s->cur[YL_F_본장시가] > 0)) && (b->close < s->cur[YL_F_본장시가])) && (s->cur[YL_F_실세션진입수] == 0)) ? 1 : 0);
            if ((((s->cur[YL_F_본장중] == 0) || ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 60)) || (s->cur[YL_F_실세션진입수] > 0))) 
                s->cur[YL_F_확장반등대기] = 0;
            s->cur[YL_F_확장반등금지] = 0;
            if (((((((s->cur[YL_F_런던시가] > 0) && (s->cur[YL_F_본장시가] > 0)) && (s->cur[YL_F_런던시가] > s->cur[YL_F_본장시가])) && (b->high < s->cur[YL_F_본장시가])) && (s->cur[YL_F_통합상태] < 0)) && (s->cur[YL_F_통합색상] != yl_rgb(210, 210, 210)))) 
                s->cur[YL_F_확장반등금지] = 1;
            if (((s->cur[YL_F_전일본장저가] > 0) && (b->close < s->cur[YL_F_전일본장저가]))) 
                s->cur[YL_F_확장반등금지] = 1;
            if ((((((s->cur[YL_F_빨강구조] == 1) && (s->cur[YL_F_핵심지난상승최저가] > 0)) && (b->high < s->cur[YL_F_핵심지난상승최저가])) && (s->cur[YL_F_통합상태] < 0)) && (s->cur[YL_F_통합색상] != yl_rgb(210, 210, 210)))) 
                s->cur[YL_F_확장반등금지] = 1;
            if ((((s->cur[YL_F_이벤트방향] == 1) && (s->cur[YL_F_이벤트가] > 0)) && (b->close < s->cur[YL_F_이벤트가]))) 
                s->cur[YL_F_확장반등금지] = 1;
            if (((((((s->cur[YL_F_런던시가] > 0) && (s->cur[YL_F_본장시가] > 0)) && (s->cur[YL_F_런던시가] > s->cur[YL_F_본장시가])) && (b->high < s->cur[YL_F_본장시가])) && (s->cur[YL_F_하락대기] == 1)) && (s->cur[YL_F_하락A] > 0))) 
                s->cur[YL_F_확장반등금지] = 1;
            if (((s->cur[YL_F_런던시가] > 0) && (b->close < s->cur[YL_F_런던시가]))) 
                s->cur[YL_F_확장반등금지] = 1;
            if (((((((((((((s->cur[YL_F_확장반등진입] == 1) && (s->cur[YL_F_확장반등대기] == 1)) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_가상시작] != 1)) && (s->cur[YL_F_확장반등횟수] == 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 30)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 60)) && (s->cur[YL_F_선행위치Cnt] >= 1)) && (s->cur[YL_F_선행위치Cnt] < 12)) && (yl_lag(s, YL_F_선행위치Cnt, 1) <= 0)) && (s->cur[YL_F_확장반등금지] == 0)))                 {
                    s->cur[YL_F_주문코드] = ((67 * 10) + s->cur[YL_F_진입계약수]);
                    s->cur[YL_F_가상매수진입] = 1;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_전일저이탈횟수] = 0;
            if (((((((((((((((((((s->cur[YL_F_전일저이탈진입] == 1) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_가상시작] != (-1))) && (s->cur[YL_F_전일저이탈횟수] == 0)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_전일본장고가] > 0)) && (s->cur[YL_F_전일본장저가] > 0)) && (s->cur[YL_F_전전일본장고가] > 0)) && (s->cur[YL_F_전전일본장저가] > 0)) && ((s->cur[YL_F_전일본장고가] - s->cur[YL_F_전일본장저가]) <= (((s->cur[YL_F_전전일본장고가] - s->cur[YL_F_전전일본장저가])) * 0.7))) && (b->close < s->cur[YL_F_전일본장저가])) && (yl_px(s, 'C', 1) >= s->cur[YL_F_전일본장저가])) && (s->cur[YL_F_선행위치Cnt] > (-10))) && (s->cur[YL_F_선행위치Cnt] <= 0)) && (s->cur[YL_F_본장진폭율] > 0)) && (s->cur[YL_F_본장진폭율] <= 100)) && (s->cur[YL_F_통합상태] == (-100))))                 {
                    s->cur[YL_F_주문코드] = ((79 * 10) + s->cur[YL_F_진입계약수]);
                    s->cur[YL_F_가상매도진입] = 1;
                    s->cur[YL_F_가상매수진입] = 0;
                }
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_전일고돌파횟수] = 0;
            if (((((((((((((((((((((s->cur[YL_F_전일고돌파진입] == 1) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_가상시작] != 1)) && (s->cur[YL_F_전일고돌파횟수] == 0)) && (s->cur[YL_F_본장중] == 1)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 60)) && (s->cur[YL_F_실세션손익] <= 0)) && (s->cur[YL_F_전일본장고가] > 0)) && (s->cur[YL_F_전일본장저가] > 0)) && (s->cur[YL_F_전전일본장고가] > 0)) && (s->cur[YL_F_전전일본장저가] > 0)) && ((s->cur[YL_F_전일본장고가] - s->cur[YL_F_전일본장저가]) <= (((s->cur[YL_F_전전일본장고가] - s->cur[YL_F_전전일본장저가])) * 0.7))) && (b->close > s->cur[YL_F_전일본장고가])) && (yl_px(s, 'C', 1) <= s->cur[YL_F_전일본장고가])) && (s->cur[YL_F_선행위치Cnt] < 10)) && (s->cur[YL_F_선행위치Cnt] >= 0)) && (s->cur[YL_F_본장진폭율] > 0)) && (s->cur[YL_F_본장진폭율] <= 100)) && (s->cur[YL_F_통합상태] == 100)))                 {
                    s->cur[YL_F_주문코드] = ((82 * 10) + s->cur[YL_F_진입계약수]);
                    s->cur[YL_F_가상매수진입] = 1;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_초기하락매도횟수] = 0;
            if ((((((((((((((((((((((((s->cur[YL_F_초기하락매도진입] == 1) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_가상시작] != (-1))) && (s->cur[YL_F_초기하락매도횟수] == 0)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_선행위치Cnt] < 0)) && (s->cur[YL_F_선행위치Cnt] > (-6))) && (s->cur[YL_F_선행기울기Cnt] < 0)) && (s->cur[YL_F_선행기울기Cnt] > (-6))) && (s->cur[YL_F_통합상태] < 0)) && (s->cur[YL_F_통합색상] != yl_rgb(210, 210, 210))) && (s->cur[YL_F_본장진폭율] > 0)) && (s->cur[YL_F_본장진폭율] < 90)) && (s->cur[YL_F_본장시가] > 0)) && (b->close < s->cur[YL_F_본장시가])) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 30)) && ((((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 90) || ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 120)))) && ((((s->cur[YL_F_실세션진입수] == 0) || (s->cur[YL_F_실직전방향] != 1)) || (s->cur[YL_F_실직전수익] == 1)))) && (((((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 60) || ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 90)) || (s->cur[YL_F_실세션진입수] == 0)))) && (s->cur[YL_F_스윙전체폭비] < 70)) && (((s->cur[YL_F_이차대기] == 0) || (s->cur[YL_F_이차A] <= 0)))) && ((-s->cur[YL_F_통합카운트]) < 10)))                 {
                    s->cur[YL_F_초기하락금지] = 0;
                    if (((s->cur[YL_F_S두구간비율] > 90) && (s->cur[YL_F_S신호합계] <= 0))) 
                        s->cur[YL_F_초기하락금지] = 1;
                    if (((s->cur[YL_F_전일본장고가] > 0) && (b->close > s->cur[YL_F_전일본장고가]))) 
                        s->cur[YL_F_초기하락금지] = 1;
                    if (((((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 120) && (s->cur[YL_F_실세션손익] > 0)) && (s->cur[YL_F_실직전손익] > ((s->cur[YL_F_진입계약수] * 10) * b->price_scale)))) 
                        s->cur[YL_F_초기하락금지] = 1;
                    if ((((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 180) && (s->cur[YL_F_실세션손익] > 0))) 
                        s->cur[YL_F_초기하락금지] = 1;
                    if ((((s->cur[YL_F_전일본장저가] > 0) && (b->close >= s->cur[YL_F_전일본장저가])) && ((b->close - s->cur[YL_F_전일본장저가]) <= 5))) 
                        s->cur[YL_F_초기하락금지] = 1;
                    if (((s->cur[YL_F_압축폭비율] > 70) && (s->cur[YL_F_S두구간비율] > 90))) 
                        s->cur[YL_F_초기하락금지] = 1;
                    if ((s->cur[YL_F_초기하락금지] == 1)) 
                        s->cur[YL_F_초기하락매도횟수] = 1;
                    else                         {
                            s->cur[YL_F_주문코드] = ((85 * 10) + s->cur[YL_F_진입계약수]);
                            s->cur[YL_F_가상매도진입] = 1;
                            s->cur[YL_F_가상매수진입] = 0;
                        }
                }
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_초기상승매수횟수] = 0;
            if (((((((((((((((((((((((((s->cur[YL_F_초기상승매수진입] == 1) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_가상시작] != 1)) && (s->cur[YL_F_초기상승매수횟수] == 0)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_선행위치Cnt] > 0)) && (s->cur[YL_F_선행위치Cnt] < 6)) && (s->cur[YL_F_선행기울기Cnt] > 0)) && (s->cur[YL_F_선행기울기Cnt] < 6)) && (s->cur[YL_F_통합상태] > 0)) && (s->cur[YL_F_통합색상] != yl_rgb(210, 210, 210))) && (s->cur[YL_F_본장진폭율] > 0)) && (s->cur[YL_F_본장진폭율] < 90)) && (s->cur[YL_F_본장시가] > 0)) && (b->close > s->cur[YL_F_본장시가])) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 30)) && ((((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 90) || ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 120)))) && ((((s->cur[YL_F_실세션진입수] == 0) || (s->cur[YL_F_실직전방향] != (-1))) || (s->cur[YL_F_실직전수익] == 1)))) && (((((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 60) || ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 90)) || (s->cur[YL_F_실세션진입수] == 0)))) && (s->cur[YL_F_스윙전체폭비] < 70)) && (((s->cur[YL_F_실청산봉] <= 0) || ((s->bar_n - s->cur[YL_F_실청산봉]) >= 30)))) && (((s->cur[YL_F_하락대기] == 0) || (s->cur[YL_F_하락A] <= 0)))) && (s->cur[YL_F_통합카운트] < 10)))                 {
                    s->cur[YL_F_초기상승금지] = 0;
                    if (((s->cur[YL_F_S두구간비율] > 90) && (s->cur[YL_F_S신호합계] >= 0))) 
                        s->cur[YL_F_초기상승금지] = 1;
                    if (((s->cur[YL_F_전일본장저가] > 0) && (b->close < s->cur[YL_F_전일본장저가]))) 
                        s->cur[YL_F_초기상승금지] = 1;
                    if (((((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 120) && (s->cur[YL_F_실세션손익] > 0)) && (s->cur[YL_F_실직전손익] > ((s->cur[YL_F_진입계약수] * 10) * b->price_scale)))) 
                        s->cur[YL_F_초기상승금지] = 1;
                    if ((((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 180) && (s->cur[YL_F_실세션손익] > 0))) 
                        s->cur[YL_F_초기상승금지] = 1;
                    if (((s->cur[YL_F_전일본장고가] > 0) && ((s->cur[YL_F_전일본장고가] - b->close) <= 10))) 
                        s->cur[YL_F_초기상승금지] = 1;
                    if ((s->cur[YL_F_S삼선비율] >= 90)) 
                        s->cur[YL_F_초기상승금지] = 1;
                    if ((((s->cur[YL_F_빨강구조] == 1) && (s->cur[YL_F_핵심지난상승최고가] > 0)) && (b->low < s->cur[YL_F_핵심지난상승최고가]))) 
                        s->cur[YL_F_초기상승금지] = 1;
                    if (((s->cur[YL_F_압축폭비율] > 70) && (s->cur[YL_F_S두구간비율] > 90))) 
                        s->cur[YL_F_초기상승금지] = 1;
                    if ((s->cur[YL_F_초기상승금지] == 1)) 
                        s->cur[YL_F_초기상승매수횟수] = 1;
                    else                         {
                            s->cur[YL_F_주문코드] = ((88 * 10) + s->cur[YL_F_진입계약수]);
                            s->cur[YL_F_가상매수진입] = 1;
                            s->cur[YL_F_가상매도진입] = 0;
                        }
                }
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_무거래30횟수] = 0;
            s->cur[YL_F_무거래30종류] = 0;
            if ((((((s->cur[YL_F_본장중] == 1) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) == 30)) && (s->cur[YL_F_본장시가] > 0)) && (s->cur[YL_F_본장진폭율] > 0)) && (s->cur[YL_F_본장거래량율] > 0)))                 {
                    if ((((((((s->cur[YL_F_본장거래량율] >= 100) && (s->cur[YL_F_본장진폭율] >= 100)) && (b->close < s->cur[YL_F_본장시가])) && (s->cur[YL_F_서머타임] == 1)) && (((s->cur[YL_F_전세션마지막방향] != 1) || (s->cur[YL_F_전세션마지막수익] == 0)))) && (s->cur[YL_F_통합상태] != (-100))) && (((s->cur[YL_F_전일본장고가] <= 0) || (b->close <= s->cur[YL_F_전일본장고가]))))) 
                        s->cur[YL_F_무거래30종류] = 2;
                    if ((((((((s->cur[YL_F_본장거래량율] >= 100) && (s->cur[YL_F_본장진폭율] < 100)) && (b->close >= s->cur[YL_F_본장시가])) && (((s->cur[YL_F_전일본장저가] <= 0) || (b->close >= s->cur[YL_F_전일본장저가])))) && (((s->cur[YL_F_런던시가] <= 0) || (b->close >= s->cur[YL_F_런던시가])))) && (((s->cur[YL_F_압축폭비율] <= 80) || (s->cur[YL_F_스윙전체폭비] <= 90)))) && ((((s->cur[YL_F_전일본장고가] <= 0) || (s->cur[YL_F_지속저장목표5] <= s->cur[YL_F_전일본장고가])) || (b->close >= s->cur[YL_F_전일본장고가]))))) 
                        s->cur[YL_F_무거래30종류] = 3;
                }
            if ((((((((s->cur[YL_F_무거래30진입] == 1) && (s->cur[YL_F_무거래30종류] > 0)) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_가상시작] != 1)) && (s->cur[YL_F_무거래30횟수] == 0)) && (s->cur[YL_F_실세션진입수] == 0)))                 {
                    s->cur[YL_F_주문코드] = ((94 * 10) + s->cur[YL_F_진입계약수]);
                    s->cur[YL_F_가상매수진입] = 1;
                    s->cur[YL_F_가상매도진입] = 0;
                    s->cur[YL_F_가상진입종류30] = s->cur[YL_F_무거래30종류];
                }
            if ((s->cur[YL_F_FX초기화] == 1)) 
                s->cur[YL_F_시가반락매도횟수] = 0;
            s->cur[YL_F_시가위봉수] = 0;
            if (((s->cur[YL_F_본장중] == 1) && (s->cur[YL_F_본장시가] > 0)))                 {
                    for (s->cur[YL_F_회복k] = (1); s->cur[YL_F_회복k] <= (15); s->cur[YL_F_회복k] += 1)                         {
                            if ((yl_px(s, 'L', s->cur[YL_F_회복k]) > s->cur[YL_F_본장시가])) 
                                s->cur[YL_F_시가위봉수] = (s->cur[YL_F_시가위봉수] + 1);
                        }
                }
            if (((((((((((((((((((((((((((((s->cur[YL_F_시가반락매도진입] == 1) && (s->cur[YL_F_주문코드] == 0)) && (s->cur[YL_F_매매시간] == 1)) && (s->cur[YL_F_가상시작] != (-1))) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_본장시가] > 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 60)) && (s->cur[YL_F_시가반락매도횟수] == 0)) && (b->close < s->cur[YL_F_본장시가])) && (yl_at(yl_hi(s, "C", 5), 1) >= s->cur[YL_F_본장시가])) && (s->cur[YL_F_선행위치Cnt] <= (-1))) && (s->cur[YL_F_선행위치Cnt] > (-12))) && (s->cur[YL_F_선행기울기Cnt] <= (-1))) && (s->cur[YL_F_선행기울기Cnt] > (-12))) && (s->cur[YL_F_통합카운트] <= (-1))) && (s->cur[YL_F_통합카운트] >= (-9))) && (s->cur[YL_F_통합상태] < 0)) && (s->cur[YL_F_통합색상] != yl_rgb(210, 210, 210))) && (s->cur[YL_F_스윙전체폭비] < 100)) && (s->cur[YL_F_본장진폭율] > 0)) && (s->cur[YL_F_본장진폭율] < 80)) && (s->cur[YL_F_본장거래량율] > 100)) && (s->cur[YL_F_직전통합카운트값] >= 5)) && (((s->cur[YL_F_지속저장목표5] <= 0) || (b->close <= s->cur[YL_F_지속저장목표5])))) && (s->cur[YL_F_시가위봉수] >= 3)) && (((s->cur[YL_F_이차대기] == 0) || (s->cur[YL_F_이차A] <= 0)))) && (((s->cur[YL_F_전일본장고가] <= 0) || (b->low <= s->cur[YL_F_전일본장고가])))) && (((s->cur[YL_F_수익비늦은매도제외분] <= 0) || (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_수익비늦은매도제외분]))))))                 {
                    s->cur[YL_F_주문코드] = ((58 * 10) + s->cur[YL_F_진입계약수]);
                    s->cur[YL_F_가상매도진입] = 1;
                    s->cur[YL_F_가상매수진입] = 0;
                }
            if (((s->cur[YL_F_선행기울기Cnt] <= 0) && (yl_lag(s, YL_F_선행기울기Cnt, 1) > 0))) 
                s->cur[YL_F_직전상승기울기값] = yl_lag(s, YL_F_선행기울기Cnt, 1);
            if ((((s->cur[YL_F_본장중] == 1) && (s->cur[YL_F_당일본장고가] > 0)) && (b->high >= s->cur[YL_F_당일본장고가]))) 
                s->cur[YL_F_고가갱신봉] = s->bar_n;
            if ((((((((((s->cur[YL_F_돌파매수기울기과열] > 0) && (s->cur[YL_F_주문코드] > 0)) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 1)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 3)) && (s->cur[YL_F_본장중] == 1)) && (yl_max(s->cur[YL_F_선행기울기Cnt], s->cur[YL_F_직전상승기울기값]) >= s->cur[YL_F_돌파매수기울기과열])) && ((s->bar_n - s->cur[YL_F_고가갱신봉]) <= s->cur[YL_F_돌파매수고가갱신봉수])) && (b->close < s->cur[YL_F_당일본장고가])) && (b->high < s->cur[YL_F_당일본장고가])))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((s->cur[YL_F_선행기울기Cnt] >= 0) && (yl_lag(s, YL_F_선행기울기Cnt, 1) < 0))) 
                s->cur[YL_F_직전하락기울기값] = (-yl_lag(s, YL_F_선행기울기Cnt, 1));
            if ((((s->cur[YL_F_본장중] == 1) && (s->cur[YL_F_당일본장저가] > 0)) && (b->low <= s->cur[YL_F_당일본장저가]))) 
                s->cur[YL_F_저가갱신봉] = s->bar_n;
            if ((((((((((s->cur[YL_F_돌파매도기울기과열] > 0) && (s->cur[YL_F_주문코드] > 0)) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 4)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 6)) && (s->cur[YL_F_본장중] == 1)) && (yl_max((-s->cur[YL_F_선행기울기Cnt]), s->cur[YL_F_직전하락기울기값]) >= s->cur[YL_F_돌파매도기울기과열])) && ((s->bar_n - s->cur[YL_F_저가갱신봉]) <= s->cur[YL_F_돌파매수고가갱신봉수])) && (b->close > s->cur[YL_F_당일본장저가])) && (b->low > s->cur[YL_F_당일본장저가])))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if ((((((((((s->cur[YL_F_압축매도감싸기제외] == 1) && (s->cur[YL_F_주문코드] > 0)) && (trunc((s->cur[YL_F_주문코드] / 10)) == 10)) && (s->cur[YL_F_직전주요통합방향] == (-1))) && (s->cur[YL_F_런던시가] > 0)) && (b->low > s->cur[YL_F_런던시가])) && (s->cur[YL_F_파랑구조] == 1)) && (s->cur[YL_F_핵심지난하락최저가] > 0)) && (b->high > s->cur[YL_F_핵심지난하락최저가])))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((((s->cur[YL_F_통합과열매수금지카운트] > 0) && (s->cur[YL_F_주문코드] > 0)) && (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) < 3)) && (s->cur[YL_F_통합카운트] >= s->cur[YL_F_통합과열매수금지카운트])) && (s->cur[YL_F_선행위치Cnt] < 0)) && (s->cur[YL_F_압축폭비율] > s->cur[YL_F_통합과열매수금지폭비율])))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            if (((((s->cur[YL_F_본장전제외시작분] > 0) && (s->cur[YL_F_주문코드] > 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= (-s->cur[YL_F_본장전제외시작분]))) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < (-s->cur[YL_F_본장전제외끝분]))))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                }
            s->cur[YL_F_압축부족표시] = 0;
            if (((s->cur[YL_F_압축지속봉수] == 0) && (yl_lag(s, YL_F_압축지속봉수, 1) > 0))) 
                s->cur[YL_F_직전압축봉수] = yl_lag(s, YL_F_압축지속봉수, 1);
            if (((((((s->cur[YL_F_압축부족봉수] > 0) && (s->cur[YL_F_주문코드] > 0)) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 7)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 12)) && (yl_max(s->cur[YL_F_압축지속봉수], yl_lag(s, YL_F_압축지속봉수, 1)) < s->cur[YL_F_압축부족봉수])) && (s->cur[YL_F_S삼선비율] >= s->cur[YL_F_압축부족삼선비율]))) 
                s->cur[YL_F_압축부족표시] = 1;
            if (((((s->cur[YL_F_압축부족반대두구간한도] > 0) && (s->cur[YL_F_압축부족표시] == 1)) && (trunc((s->cur[YL_F_주문코드] / 10)) == 11)) && (s->cur[YL_F_S두구간비율] > s->cur[YL_F_압축부족반대두구간한도])))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                    s->cur[YL_F_압축부족표시] = 0;
                }
            if ((((s->cur[YL_F_압축부족표시] == 1) && (trunc((s->cur[YL_F_주문코드] / 10)) == 8)) && (((((s->cur[YL_F_압축부족매수반대본장전금지] == 1) && (s->cur[YL_F_본장기준봉분] < s->cur[YL_F_본장시작분]))) || (((s->cur[YL_F_압축부족매수반대위치한도] > 0) && (s->cur[YL_F_선행위치Cnt] > s->cur[YL_F_압축부족매수반대위치한도])))))))                 {
                    s->cur[YL_F_주문코드] = 0;
                    s->cur[YL_F_가상매수진입] = 0;
                    s->cur[YL_F_가상매도진입] = 0;
                    s->cur[YL_F_압축부족표시] = 0;
                }
            s->cur[YL_F_그림자진입] = 0;
            if (((((((((s->cur[YL_F_개장장대배수기준] > 0) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_본장기준봉분] >= s->cur[YL_F_본장시작분])) && (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] + s->cur[YL_F_개장장대표시분]))) && (s->cur[YL_F_개장최대배율] >= s->cur[YL_F_개장장대배수기준])) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 24)) && (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 3) == 1)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 11))) 
                s->cur[YL_F_그림자진입] = 1;
            s->cur[YL_F_짝그림자] = s->cur[YL_F_그림자진입];
            if ((((((s->cur[YL_F_수익비1계약제외] == 1) && (s->cur[YL_F_주문코드] > 0)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 24)) && (fmod(s->cur[YL_F_주문코드], 10) == 1)) && (((s->cur[YL_F_실세션진입수] > 0) || (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) < 3))))) 
                s->cur[YL_F_그림자진입] = 1;
            if (((((((s->cur[YL_F_수익비늦은매수제외분] > 0) && (s->cur[YL_F_주문코드] > 0)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 24)) && (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) < 3)) && (s->cur[YL_F_본장기준봉분] >= (s->cur[YL_F_본장시작분] + s->cur[YL_F_수익비늦은매수제외분]))) && (s->cur[YL_F_실세션진입수] > 0))) 
                s->cur[YL_F_그림자진입] = 1;
            if ((((((((s->cur[YL_F_수익비늦은매도제외분] > 0) && (s->cur[YL_F_주문코드] > 0)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 24)) && (fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) >= 3)) && (((trunc((s->cur[YL_F_주문코드] / 10)) < 4) || (trunc((s->cur[YL_F_주문코드] / 10)) > 6)))) && (s->cur[YL_F_본장기준봉분] >= (s->cur[YL_F_본장시작분] + s->cur[YL_F_수익비늦은매도제외분]))) && (s->cur[YL_F_실세션진입수] > 0))) 
                s->cur[YL_F_그림자진입] = 1;
            if (((((s->cur[YL_F_수익비늦은매수제외분] > 0) && (trunc((s->cur[YL_F_주문코드] / 10)) == 31)) && (s->cur[YL_F_본장기준봉분] >= (s->cur[YL_F_본장시작분] + s->cur[YL_F_수익비늦은매수제외분]))) && (s->cur[YL_F_실세션진입수] > 0))) 
                s->cur[YL_F_그림자진입] = 1;
            if (((((((((((s->cur[YL_F_주문코드] > 0) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 1)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 3)) && (s->cur[YL_F_실세션진입수] > 0)) && (s->cur[YL_F_실직전방향] == 1)) && (s->cur[YL_F_실직전손익] > 0)) && (s->cur[YL_F_실직전손익] < 5)) && (s->cur[YL_F_실청산봉] > 0)) && ((s->bar_n - s->cur[YL_F_실청산봉]) <= 30)) && (s->cur[YL_F_스윙전체폭비] > 90))) 
                s->cur[YL_F_그림자진입] = 1;
            if (((((((trunc((s->cur[YL_F_주문코드] / 10)) == 43) && (s->cur[YL_F_이차A] < 30)) && (s->cur[YL_F_이차B] >= s->cur[YL_F_이차A])) && (s->cur[YL_F_이차되돌림] >= 80)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 70)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 100))) 
                s->cur[YL_F_그림자진입] = 1;
            if ((((((((((s->cur[YL_F_주문코드] > 0) && (s->cur[YL_F_가상매수진입] == 1)) && (s->cur[YL_F_이벤트매수금지분] > 0)) && (s->cur[YL_F_이벤트거리pt] > 0)) && (s->cur[YL_F_이벤트방향] == 1)) && (s->cur[YL_F_이벤트가] > 0)) && ((s->cur[YL_F_이벤트가] - b->close) >= s->cur[YL_F_이벤트거리pt])) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < s->cur[YL_F_이벤트매수금지분]))) 
                s->cur[YL_F_그림자진입] = 1;
            if (((((((s->cur[YL_F_주문코드] > 0) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 1)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 3)) && (s->cur[YL_F_S삼선비율] > 90)) && (s->cur[YL_F_압축지속봉수] > 0)) && (s->cur[YL_F_압축지속봉수] <= 10))) 
                s->cur[YL_F_그림자진입] = 1;
            if (((((((s->cur[YL_F_주문코드] > 0) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 4)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 6)) && (s->cur[YL_F_S삼선비율] > 90)) && (s->cur[YL_F_압축지속봉수] > 0)) && (s->cur[YL_F_압축지속봉수] <= 10))) 
                s->cur[YL_F_그림자진입] = 1;
            if (((((s->cur[YL_F_주문코드] > 0) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 7)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 9)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 150))) 
                s->cur[YL_F_그림자진입] = 1;
            if ((((((((((((s->cur[YL_F_주문코드] > 0) && (s->cur[YL_F_가상매도진입] == 1)) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_본장시가] > 0)) && (b->low > s->cur[YL_F_본장시가])) && (s->cur[YL_F_통합카운트] <= (-10))) && (s->cur[YL_F_이차대기] == 1)) && (s->cur[YL_F_이차A] > 0)) && (s->cur[YL_F_하락대기] == 1)) && (s->cur[YL_F_하락A] > 0)) && (s->cur[YL_F_실세션진입수] == 0))) 
                s->cur[YL_F_그림자진입] = 1;
            if (((((((((((((s->cur[YL_F_주문코드] > 0) && (s->cur[YL_F_가상매수진입] == 1)) && ((((trunc((s->cur[YL_F_주문코드] / 10)) == 43) || (((trunc((s->cur[YL_F_주문코드] / 10)) >= 19) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 21)))) || (((trunc((s->cur[YL_F_주문코드] / 10)) >= 7) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 9)))))) && (s->cur[YL_F_본장중] == 1)) && (s->cur[YL_F_본장시가] > 0)) && (b->high < s->cur[YL_F_본장시가])) && (s->cur[YL_F_통합카운트] >= 10)) && (s->cur[YL_F_이차대기] == 1)) && (s->cur[YL_F_이차A] > 0)) && (s->cur[YL_F_하락대기] == 1)) && (s->cur[YL_F_하락A] > 0)) && (s->cur[YL_F_실세션진입수] == 0))) 
                s->cur[YL_F_그림자진입] = 1;
            if (((((((((s->cur[YL_F_주문코드] > 0) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 1)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 3)) && (s->cur[YL_F_본장중] == 1)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 15)) && (s->cur[YL_F_S두구간비율] > 90)) && (s->cur[YL_F_게이지돌파매수] <= 0)) && (yl_lag(s, YL_F_게이지돌파매수, 1) <= 0))) 
                s->cur[YL_F_그림자진입] = 1;
            if (((((((((s->cur[YL_F_주문코드] > 0) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 4)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 6)) && (s->cur[YL_F_본장중] == 1)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 15)) && (s->cur[YL_F_S두구간비율] > 90)) && (s->cur[YL_F_게이지돌파매도] <= 0)) && (yl_lag(s, YL_F_게이지돌파매도, 1) <= 0))) 
                s->cur[YL_F_그림자진입] = 1;
            if (((((((s->cur[YL_F_주문코드] > 0) && (s->cur[YL_F_가상매수진입] == 1)) && (s->cur[YL_F_통합카운트] >= 10)) && (s->cur[YL_F_S삼선비율] < 10)) && (s->cur[YL_F_전일본장고가] > 0)) && (b->close > s->cur[YL_F_전일본장고가]))) 
                s->cur[YL_F_그림자진입] = 1;
            if ((((((((s->cur[YL_F_주문코드] > 0) && ((((((trunc((s->cur[YL_F_주문코드] / 10)) >= 22) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 24))) || (trunc((s->cur[YL_F_주문코드] / 10)) == 46)) || (trunc((s->cur[YL_F_주문코드] / 10)) == 49)))) && (s->cur[YL_F_실세션진입수] > 0)) && (s->cur[YL_F_실직전방향] == 1)) && (s->cur[YL_F_실직전수익] == 1)) && (s->cur[YL_F_본장시가] > 0)) && (b->close > s->cur[YL_F_본장시가]))) 
                s->cur[YL_F_그림자진입] = 1;
            if ((((((((((s->cur[YL_F_주문코드] > 0) && (((trunc((s->cur[YL_F_주문코드] / 10)) == 43) || (((trunc((s->cur[YL_F_주문코드] / 10)) >= 19) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 21)))))) && (s->cur[YL_F_런던시가] > 0)) && (b->close < s->cur[YL_F_런던시가])) && (s->cur[YL_F_본장시가] > 0)) && (b->close < s->cur[YL_F_본장시가])) && (s->cur[YL_F_압축지속봉수] > 0)) && (s->cur[YL_F_압축지속봉수] <= 5)) && (s->cur[YL_F_실세션진입수] == 0))) 
                s->cur[YL_F_그림자진입] = 1;
            if ((((((((s->cur[YL_F_주문코드] > 0) && (trunc((s->cur[YL_F_주문코드] / 10)) >= 1)) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 3)) && (s->cur[YL_F_하락대기] == 1)) && (s->cur[YL_F_하락A] > 0)) && (s->cur[YL_F_통합카운트] >= 10)) && (s->cur[YL_F_본장중] == 1))) 
                s->cur[YL_F_그림자진입] = 1;
            if (((((s->cur[YL_F_주문코드] > 0) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= (-30))) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < (-1))) && ((((((trunc((s->cur[YL_F_주문코드] / 10)) >= 7) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 9))) || (((trunc((s->cur[YL_F_주문코드] / 10)) >= 13) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 15)))) || (trunc((s->cur[YL_F_주문코드] / 10)) == 37))))) 
                s->cur[YL_F_그림자진입] = 1;
            if ((((((s->cur[YL_F_주문코드] > 0) && (s->cur[YL_F_실세션진입수] > 0)) && (s->cur[YL_F_실직전수익] == 1)) && (s->cur[YL_F_이벤트가] > 0)) && ((((((s->cur[YL_F_가상매수진입] == 1) && (s->cur[YL_F_이벤트방향] == 1)) && (b->close < s->cur[YL_F_이벤트가]))) || ((((s->cur[YL_F_가상매도진입] == 1) && (s->cur[YL_F_이벤트방향] == (-1))) && (b->close > s->cur[YL_F_이벤트가]))))))) 
                s->cur[YL_F_그림자진입] = 1;
            if (((((((s->cur[YL_F_주문코드] > 0) && (s->cur[YL_F_실세션진입수] > 0)) && (s->cur[YL_F_실직전수익] == 0)) && (s->cur[YL_F_실청산봉] > 0)) && ((s->bar_n - s->cur[YL_F_실청산봉]) <= 15)) && (((((s->cur[YL_F_가상매수진입] == 1) && (s->cur[YL_F_실직전방향] == 1))) || (((s->cur[YL_F_가상매도진입] == 1) && (s->cur[YL_F_실직전방향] == (-1)))))))) 
                s->cur[YL_F_그림자진입] = 1;
            s->cur[YL_F_진단그림자] = ((((((((((s->cur[YL_F_주문코드] > 0) && (s->cur[YL_F_그림자진입] == 1)) && (s->cur[YL_F_짝그림자] == 0)) && (s->cur[YL_F_실세션진입수] == 0)) && (s->cur[YL_F_본장중] == 1)) && (((((trunc((s->cur[YL_F_주문코드] / 10)) >= 19) && (trunc((s->cur[YL_F_주문코드] / 10)) <= 21))) || (trunc((s->cur[YL_F_주문코드] / 10)) == 43)))) && (fmod(s->cur[YL_F_주문코드], 10) == 1)) && (s->cur[YL_F_런던시가] > 0)) && (b->close >= s->cur[YL_F_런던시가])) ? 1 : 0);
            if ((s->cur[YL_F_진단그림자] == 1)) 
                s->cur[YL_F_그림자진입] = s->cur[YL_F_짝그림자];
            if (((((((((((((s->cur[YL_F_주문코드] > 0) && (s->cur[YL_F_가상매도진입] == 1)) && (s->cur[YL_F_본장중] == 1)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= 0)) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) < 2)) && (s->cur[YL_F_이차대기] == 1)) && (s->cur[YL_F_이차A] > 0)) && (s->cur[YL_F_런던시가] > 0)) && (b->close > s->cur[YL_F_런던시가])) && (s->cur[YL_F_전일본장고가] > 0)) && (b->close > s->cur[YL_F_전일본장고가])) && (s->cur[YL_F_본장진폭율] >= 150))) 
                s->cur[YL_F_그림자진입] = 1;
            s->cur[YL_F_런던시가아래] = 0;
            if ((((((s->cur[YL_F_런던시가아래표시] == 1) && (s->cur[YL_F_주문코드] > 0)) && (s->cur[YL_F_본장중] == 0)) && (s->cur[YL_F_본장기준봉분] < s->cur[YL_F_본장시작분])) && (s->cur[YL_F_런던시가] > 0)))                 {
                    if (((fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) < 3) && (b->high < s->cur[YL_F_런던시가]))) 
                        s->cur[YL_F_런던시가아래] = 1;
                    if (((fmod((trunc((s->cur[YL_F_주문코드] / 10)) - 1), 6) >= 3) && (b->low > s->cur[YL_F_런던시가]))) 
                        s->cur[YL_F_런던시가아래] = 1;
                }
            s->cur[YL_F_진입명번호] = 0;
            if ((s->cur[YL_F_주문코드] > 0))                 {
                    s->cur[YL_F_주문번호] = trunc((s->cur[YL_F_주문코드] / 10));
                    s->cur[YL_F_주문수량] = (s->cur[YL_F_주문코드] - (s->cur[YL_F_주문번호] * 10));
                    if ((s->cur[YL_F_그림자진입] == 1)) 
                        s->cur[YL_F_주문번호] = 0;
                    if ((s->cur[YL_F_주문번호] == 25)) 
                        s->cur[YL_F_진입명번호] = 1;
                    if ((s->cur[YL_F_주문번호] == 28)) 
                        s->cur[YL_F_진입명번호] = 2;
                    if ((s->cur[YL_F_주문번호] == 31)) 
                        s->cur[YL_F_진입명번호] = 3;
                    if ((s->cur[YL_F_주문번호] == 34)) 
                        s->cur[YL_F_진입명번호] = 4;
                    if ((s->cur[YL_F_주문번호] == 37)) 
                        s->cur[YL_F_진입명번호] = 5;
                    if ((s->cur[YL_F_주문번호] == 40)) 
                        s->cur[YL_F_진입명번호] = 6;
                    if ((s->cur[YL_F_주문번호] == 43)) 
                        s->cur[YL_F_진입명번호] = 7;
                    if ((s->cur[YL_F_주문번호] == 46)) 
                        s->cur[YL_F_진입명번호] = 8;
                    if ((s->cur[YL_F_주문번호] == 49)) 
                        s->cur[YL_F_진입명번호] = 8;
                    if ((s->cur[YL_F_주문번호] == 52)) 
                        s->cur[YL_F_진입명번호] = 2;
                    if ((s->cur[YL_F_주문번호] == 55)) 
                        s->cur[YL_F_진입명번호] = 21;
                    if ((s->cur[YL_F_주문번호] == 58)) 
                        s->cur[YL_F_진입명번호] = 22;
                    if ((s->cur[YL_F_주문번호] == 61)) 
                        s->cur[YL_F_진입명번호] = 23;
                    if ((s->cur[YL_F_주문번호] == 64)) 
                        s->cur[YL_F_진입명번호] = 24;
                    if ((s->cur[YL_F_주문번호] == 67)) 
                        s->cur[YL_F_진입명번호] = 25;
                    if ((s->cur[YL_F_주문번호] == 79)) 
                        s->cur[YL_F_진입명번호] = 32;
                    if ((s->cur[YL_F_주문번호] == 82)) 
                        s->cur[YL_F_진입명번호] = 33;
                    if ((s->cur[YL_F_주문번호] == 85)) 
                        s->cur[YL_F_진입명번호] = 34;
                    if ((s->cur[YL_F_주문번호] == 88)) 
                        s->cur[YL_F_진입명번호] = 35;
                    if ((s->cur[YL_F_주문번호] == 94)) 
                        s->cur[YL_F_진입명번호] = 39;
                    if (((s->cur[YL_F_주문번호] == 7) && (s->cur[YL_F_압축부족표시] == 1))) 
                        s->cur[YL_F_진입명번호] = 9;
                    if (((s->cur[YL_F_주문번호] == 8) && (s->cur[YL_F_압축부족표시] == 1))) 
                        s->cur[YL_F_진입명번호] = 9;
                    if (((s->cur[YL_F_주문번호] == 9) && (s->cur[YL_F_압축부족표시] == 1))) 
                        s->cur[YL_F_진입명번호] = 9;
                    if (((s->cur[YL_F_주문번호] == 10) && (s->cur[YL_F_압축부족표시] == 1))) 
                        s->cur[YL_F_진입명번호] = 10;
                    if (((s->cur[YL_F_주문번호] == 11) && (s->cur[YL_F_압축부족표시] == 1))) 
                        s->cur[YL_F_진입명번호] = 10;
                    if (((s->cur[YL_F_주문번호] == 12) && (s->cur[YL_F_압축부족표시] == 1))) 
                        s->cur[YL_F_진입명번호] = 10;
                    if (((s->cur[YL_F_손절후표시] == 1) && (s->cur[YL_F_직전본장손절] == 1)))                         {
                            if ((s->cur[YL_F_주문번호] == 1)) 
                                s->cur[YL_F_진입명번호] = 11;
                            if ((s->cur[YL_F_주문번호] == 2)) 
                                s->cur[YL_F_진입명번호] = 11;
                            if ((s->cur[YL_F_주문번호] == 3)) 
                                s->cur[YL_F_진입명번호] = 11;
                            if ((s->cur[YL_F_주문번호] == 4)) 
                                s->cur[YL_F_진입명번호] = 12;
                            if ((s->cur[YL_F_주문번호] == 5)) 
                                s->cur[YL_F_진입명번호] = 12;
                            if ((s->cur[YL_F_주문번호] == 6)) 
                                s->cur[YL_F_진입명번호] = 12;
                            if (((s->cur[YL_F_주문번호] == 7) && (s->cur[YL_F_압축부족표시] == 0))) 
                                s->cur[YL_F_진입명번호] = 9;
                            if (((s->cur[YL_F_주문번호] == 8) && (s->cur[YL_F_압축부족표시] == 0))) 
                                s->cur[YL_F_진입명번호] = 9;
                            if (((s->cur[YL_F_주문번호] == 9) && (s->cur[YL_F_압축부족표시] == 0))) 
                                s->cur[YL_F_진입명번호] = 9;
                            if (((s->cur[YL_F_주문번호] == 10) && (s->cur[YL_F_압축부족표시] == 0))) 
                                s->cur[YL_F_진입명번호] = 10;
                            if (((s->cur[YL_F_주문번호] == 11) && (s->cur[YL_F_압축부족표시] == 0))) 
                                s->cur[YL_F_진입명번호] = 10;
                            if (((s->cur[YL_F_주문번호] == 12) && (s->cur[YL_F_압축부족표시] == 0))) 
                                s->cur[YL_F_진입명번호] = 10;
                            if ((s->cur[YL_F_주문번호] == 13)) 
                                s->cur[YL_F_진입명번호] = 13;
                            if ((s->cur[YL_F_주문번호] == 14)) 
                                s->cur[YL_F_진입명번호] = 13;
                            if ((s->cur[YL_F_주문번호] == 15)) 
                                s->cur[YL_F_진입명번호] = 13;
                            if ((s->cur[YL_F_주문번호] == 16)) 
                                s->cur[YL_F_진입명번호] = 14;
                            if ((s->cur[YL_F_주문번호] == 17)) 
                                s->cur[YL_F_진입명번호] = 14;
                            if ((s->cur[YL_F_주문번호] == 18)) 
                                s->cur[YL_F_진입명번호] = 14;
                            if ((s->cur[YL_F_주문번호] == 19)) 
                                s->cur[YL_F_진입명번호] = 15;
                            if ((s->cur[YL_F_주문번호] == 20)) 
                                s->cur[YL_F_진입명번호] = 15;
                            if ((s->cur[YL_F_주문번호] == 21)) 
                                s->cur[YL_F_진입명번호] = 15;
                            if ((s->cur[YL_F_주문번호] == 22)) 
                                s->cur[YL_F_진입명번호] = 16;
                            if ((s->cur[YL_F_주문번호] == 23)) 
                                s->cur[YL_F_진입명번호] = 16;
                            if ((s->cur[YL_F_주문번호] == 24)) 
                                s->cur[YL_F_진입명번호] = 16;
                        }
                    else                         {
                            if ((s->cur[YL_F_런던시가아래] == 0))                                 {
                                    if ((s->cur[YL_F_주문번호] == 1)) 
                                        s->cur[YL_F_진입명번호] = 11;
                                    if ((s->cur[YL_F_주문번호] == 2)) 
                                        s->cur[YL_F_진입명번호] = 11;
                                    if ((s->cur[YL_F_주문번호] == 3)) 
                                        s->cur[YL_F_진입명번호] = 11;
                                    if ((s->cur[YL_F_주문번호] == 4)) 
                                        s->cur[YL_F_진입명번호] = 12;
                                    if ((s->cur[YL_F_주문번호] == 5)) 
                                        s->cur[YL_F_진입명번호] = 12;
                                    if ((s->cur[YL_F_주문번호] == 6)) 
                                        s->cur[YL_F_진입명번호] = 12;
                                    if (((s->cur[YL_F_주문번호] == 7) && (s->cur[YL_F_압축부족표시] == 0))) 
                                        s->cur[YL_F_진입명번호] = 9;
                                    if (((s->cur[YL_F_주문번호] == 8) && (s->cur[YL_F_압축부족표시] == 0))) 
                                        s->cur[YL_F_진입명번호] = 9;
                                    if (((s->cur[YL_F_주문번호] == 9) && (s->cur[YL_F_압축부족표시] == 0))) 
                                        s->cur[YL_F_진입명번호] = 9;
                                    if (((s->cur[YL_F_주문번호] == 10) && (s->cur[YL_F_압축부족표시] == 0))) 
                                        s->cur[YL_F_진입명번호] = 10;
                                    if (((s->cur[YL_F_주문번호] == 11) && (s->cur[YL_F_압축부족표시] == 0))) 
                                        s->cur[YL_F_진입명번호] = 10;
                                    if (((s->cur[YL_F_주문번호] == 12) && (s->cur[YL_F_압축부족표시] == 0))) 
                                        s->cur[YL_F_진입명번호] = 10;
                                    if ((s->cur[YL_F_주문번호] == 13)) 
                                        s->cur[YL_F_진입명번호] = 13;
                                    if ((s->cur[YL_F_주문번호] == 14)) 
                                        s->cur[YL_F_진입명번호] = 13;
                                    if ((s->cur[YL_F_주문번호] == 15)) 
                                        s->cur[YL_F_진입명번호] = 13;
                                    if ((s->cur[YL_F_주문번호] == 16)) 
                                        s->cur[YL_F_진입명번호] = 14;
                                    if ((s->cur[YL_F_주문번호] == 17)) 
                                        s->cur[YL_F_진입명번호] = 14;
                                    if ((s->cur[YL_F_주문번호] == 18)) 
                                        s->cur[YL_F_진입명번호] = 14;
                                    if ((s->cur[YL_F_주문번호] == 19)) 
                                        s->cur[YL_F_진입명번호] = 15;
                                    if ((s->cur[YL_F_주문번호] == 20)) 
                                        s->cur[YL_F_진입명번호] = 15;
                                    if ((s->cur[YL_F_주문번호] == 21)) 
                                        s->cur[YL_F_진입명번호] = 15;
                                    if ((s->cur[YL_F_주문번호] == 22)) 
                                        s->cur[YL_F_진입명번호] = 16;
                                    if ((s->cur[YL_F_주문번호] == 23)) 
                                        s->cur[YL_F_진입명번호] = 16;
                                    if ((s->cur[YL_F_주문번호] == 24)) 
                                        s->cur[YL_F_진입명번호] = 16;
                                }
                            else                                 {
                                    if ((s->cur[YL_F_주문번호] == 1)) 
                                        s->cur[YL_F_진입명번호] = 11;
                                    if ((s->cur[YL_F_주문번호] == 2)) 
                                        s->cur[YL_F_진입명번호] = 11;
                                    if ((s->cur[YL_F_주문번호] == 3)) 
                                        s->cur[YL_F_진입명번호] = 11;
                                    if ((s->cur[YL_F_주문번호] == 4)) 
                                        s->cur[YL_F_진입명번호] = 12;
                                    if ((s->cur[YL_F_주문번호] == 5)) 
                                        s->cur[YL_F_진입명번호] = 12;
                                    if ((s->cur[YL_F_주문번호] == 6)) 
                                        s->cur[YL_F_진입명번호] = 12;
                                    if (((s->cur[YL_F_주문번호] == 7) && (s->cur[YL_F_압축부족표시] == 0))) 
                                        s->cur[YL_F_진입명번호] = 9;
                                    if (((s->cur[YL_F_주문번호] == 8) && (s->cur[YL_F_압축부족표시] == 0))) 
                                        s->cur[YL_F_진입명번호] = 9;
                                    if (((s->cur[YL_F_주문번호] == 9) && (s->cur[YL_F_압축부족표시] == 0))) 
                                        s->cur[YL_F_진입명번호] = 9;
                                    if (((s->cur[YL_F_주문번호] == 10) && (s->cur[YL_F_압축부족표시] == 0))) 
                                        s->cur[YL_F_진입명번호] = 10;
                                    if (((s->cur[YL_F_주문번호] == 11) && (s->cur[YL_F_압축부족표시] == 0))) 
                                        s->cur[YL_F_진입명번호] = 10;
                                    if (((s->cur[YL_F_주문번호] == 12) && (s->cur[YL_F_압축부족표시] == 0))) 
                                        s->cur[YL_F_진입명번호] = 10;
                                    if ((s->cur[YL_F_주문번호] == 13)) 
                                        s->cur[YL_F_진입명번호] = 13;
                                    if ((s->cur[YL_F_주문번호] == 14)) 
                                        s->cur[YL_F_진입명번호] = 13;
                                    if ((s->cur[YL_F_주문번호] == 15)) 
                                        s->cur[YL_F_진입명번호] = 13;
                                    if ((s->cur[YL_F_주문번호] == 16)) 
                                        s->cur[YL_F_진입명번호] = 14;
                                    if ((s->cur[YL_F_주문번호] == 17)) 
                                        s->cur[YL_F_진입명번호] = 14;
                                    if ((s->cur[YL_F_주문번호] == 18)) 
                                        s->cur[YL_F_진입명번호] = 14;
                                    if ((s->cur[YL_F_주문번호] == 19)) 
                                        s->cur[YL_F_진입명번호] = 15;
                                    if ((s->cur[YL_F_주문번호] == 20)) 
                                        s->cur[YL_F_진입명번호] = 15;
                                    if ((s->cur[YL_F_주문번호] == 21)) 
                                        s->cur[YL_F_진입명번호] = 15;
                                    if ((s->cur[YL_F_주문번호] == 22)) 
                                        s->cur[YL_F_진입명번호] = 16;
                                    if ((s->cur[YL_F_주문번호] == 23)) 
                                        s->cur[YL_F_진입명번호] = 16;
                                    if ((s->cur[YL_F_주문번호] == 24)) 
                                        s->cur[YL_F_진입명번호] = 16;
                                }
                        }
                }
            s->cur[YL_F_이벤트표시] = 0;
            if (((((s->cur[YL_F_진입명번호] > 0) && (fmod(s->cur[YL_F_진입명번호], 2) == 1)) && (s->cur[YL_F_이벤트방향] == 1)) && (b->close < s->cur[YL_F_이벤트가]))) 
                s->cur[YL_F_이벤트표시] = 1;
            if (((((s->cur[YL_F_진입명번호] > 0) && (fmod(s->cur[YL_F_진입명번호], 2) == 0)) && (s->cur[YL_F_이벤트방향] == (-1))) && (b->close > s->cur[YL_F_이벤트가]))) 
                s->cur[YL_F_이벤트표시] = 1;
            s->cur[YL_F_진입그룹] = 0;
            if ((s->cur[YL_F_진입명번호] == 9)) 
                s->cur[YL_F_진입그룹] = 1;
            if ((s->cur[YL_F_진입명번호] == 10)) 
                s->cur[YL_F_진입그룹] = 2;
            if ((s->cur[YL_F_진입명번호] == 11)) 
                s->cur[YL_F_진입그룹] = 3;
            if ((s->cur[YL_F_진입명번호] == 12)) 
                s->cur[YL_F_진입그룹] = 4;
            if (((s->cur[YL_F_진입명번호] == 7) || (s->cur[YL_F_진입명번호] == 15))) 
                s->cur[YL_F_진입그룹] = 5;
            if (((s->cur[YL_F_진입명번호] == 8) || (s->cur[YL_F_진입명번호] == 16))) 
                s->cur[YL_F_진입그룹] = 6;
            if (((((s->cur[YL_F_진입명번호] == 1) || (s->cur[YL_F_진입명번호] == 3)) || (s->cur[YL_F_진입명번호] == 5)) || (s->cur[YL_F_진입명번호] == 13))) 
                s->cur[YL_F_진입그룹] = 7;
            if (((((s->cur[YL_F_진입명번호] == 2) || (s->cur[YL_F_진입명번호] == 4)) || (s->cur[YL_F_진입명번호] == 6)) || (s->cur[YL_F_진입명번호] == 14))) 
                s->cur[YL_F_진입그룹] = 8;
            if ((s->cur[YL_F_진입명번호] == 21)) 
                s->cur[YL_F_진입그룹] = 11;
            if ((s->cur[YL_F_진입명번호] == 22)) 
                s->cur[YL_F_진입그룹] = 12;
            if ((s->cur[YL_F_진입명번호] == 23)) 
                s->cur[YL_F_진입그룹] = 13;
            if ((s->cur[YL_F_진입명번호] == 24)) 
                s->cur[YL_F_진입그룹] = 14;
            if ((s->cur[YL_F_진입명번호] == 25)) 
                s->cur[YL_F_진입그룹] = 15;
            if ((s->cur[YL_F_진입명번호] == 32)) 
                s->cur[YL_F_진입그룹] = 19;
            if ((s->cur[YL_F_진입명번호] == 33)) 
                s->cur[YL_F_진입그룹] = 20;
            if ((s->cur[YL_F_진입명번호] == 34)) 
                s->cur[YL_F_진입그룹] = 21;
            if ((s->cur[YL_F_진입명번호] == 35)) 
                s->cur[YL_F_진입그룹] = 22;
            if ((s->cur[YL_F_진입명번호] == 39)) 
                s->cur[YL_F_진입그룹] = 24;
            if (((s->cur[YL_F_진입그룹] == 1) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "압축매수");
            if (((s->cur[YL_F_진입그룹] == 2) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_SELL, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "압축매도");
            if (((s->cur[YL_F_진입그룹] == 3) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "돌파매수");
            if (((s->cur[YL_F_진입그룹] == 4) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_SELL, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "돌파매도");
            if (((s->cur[YL_F_진입그룹] == 5) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "추세매수");
            if (((s->cur[YL_F_진입그룹] == 6) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_SELL, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "추세매도");
            if (((s->cur[YL_F_진입그룹] == 7) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "전환매수");
            if (((s->cur[YL_F_진입그룹] == 8) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_SELL, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "전환매도");
            if (((s->cur[YL_F_진입그룹] == 1) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트고가아래_압축매수");
            if (((s->cur[YL_F_진입그룹] == 2) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_SELL, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트저가위_압축매도");
            if (((s->cur[YL_F_진입그룹] == 3) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트고가아래_돌파매수");
            if (((s->cur[YL_F_진입그룹] == 4) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_SELL, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트저가위_돌파매도");
            if (((s->cur[YL_F_진입그룹] == 5) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트고가아래_추세매수");
            if (((s->cur[YL_F_진입그룹] == 6) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_SELL, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트저가위_추세매도");
            if (((s->cur[YL_F_진입그룹] == 7) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트고가아래_전환매수");
            if (((s->cur[YL_F_진입그룹] == 8) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_SELL, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트저가위_전환매도");
            if (((s->cur[YL_F_진입그룹] == 11) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "시가회복매수");
            if (((s->cur[YL_F_진입그룹] == 11) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트고가아래_시가회복매수");
            if (((s->cur[YL_F_진입그룹] == 12) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_SELL, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "시가반락매도");
            if (((s->cur[YL_F_진입그룹] == 12) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_SELL, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트저가위_시가반락매도");
            if (((s->cur[YL_F_진입그룹] == 13) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "흡수돌파매수");
            if (((s->cur[YL_F_진입그룹] == 13) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트고가아래_흡수돌파매수");
            if (((s->cur[YL_F_진입그룹] == 14) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_SELL, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "흡수돌파매도");
            if (((s->cur[YL_F_진입그룹] == 14) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_SELL, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트저가위_흡수돌파매도");
            if (((s->cur[YL_F_진입그룹] == 15) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "무거래_확장반등매수");
            if (((s->cur[YL_F_진입그룹] == 15) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트고가아래_무거래_확장반등매수");
            if (((s->cur[YL_F_진입그룹] == 19) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_SELL, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "전일저이탈매도");
            if (((s->cur[YL_F_진입그룹] == 19) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_SELL, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트저가위_전일저이탈매도");
            if (((s->cur[YL_F_진입그룹] == 20) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "전일고돌파매수");
            if (((s->cur[YL_F_진입그룹] == 20) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트고가아래_전일고돌파매수");
            if (((s->cur[YL_F_진입그룹] == 21) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_SELL, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "초기하락매도");
            if (((s->cur[YL_F_진입그룹] == 21) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_SELL, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트저가위_초기하락매도");
            if (((s->cur[YL_F_진입그룹] == 22) && (s->cur[YL_F_이벤트표시] == 0))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "초기상승매수");
            if (((s->cur[YL_F_진입그룹] == 22) && (s->cur[YL_F_이벤트표시] == 1))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "이벤트고가아래_초기상승매수");
            if (((s->cur[YL_F_진입그룹] == 24) && (s->cur[YL_F_무거래30종류] == 1))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "무거래30_한산위매수");
            if (((s->cur[YL_F_진입그룹] == 24) && (s->cur[YL_F_무거래30종류] == 2))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "무거래30_확장아래매수");
            if (((s->cur[YL_F_진입그룹] == 24) && (s->cur[YL_F_무거래30종류] == 3))) 
                yl_order(s, TR_YL_BUY, TR_YL_ONCLOSE, 0, (int)(s->cur[YL_F_주문수량]), "무거래30_흡수위매수");
            if (((s->cur[YL_F_가상매수진입] == 1) || (s->cur[YL_F_가상매도진입] == 1)))                 {
                    if (((s->cur[YL_F_본장압축해제기록분리] == 0) || (((((((((((((((((((trunc((s->cur[YL_F_주문코드] / 10)) != 31) && (trunc((s->cur[YL_F_주문코드] / 10)) != 34)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 37)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 40)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 43)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 46)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 49)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 52)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 55)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 58)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 61)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 64)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 67)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 79)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 82)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 85)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 88)) && (trunc((s->cur[YL_F_주문코드] / 10)) != 94)))))                         {
                            s->cur[YL_F_세션진입수] = (s->cur[YL_F_세션진입수] + 1);
                            s->cur[YL_F_마지막진입방향] = ((s->cur[YL_F_가상매수진입] == 1) ? 1 : (-1));
                        }
                    else if ((trunc((s->cur[YL_F_주문코드] / 10)) == 37)) 
                        s->cur[YL_F_반전돌파횟수] = (s->cur[YL_F_반전돌파횟수] + 1);
                    else if ((trunc((s->cur[YL_F_주문코드] / 10)) == 43))                         {
                            s->cur[YL_F_이차상승횟수] = (s->cur[YL_F_이차상승횟수] + 1);
                            s->cur[YL_F_이차대기] = 0;
                        }
                    else if ((trunc((s->cur[YL_F_주문코드] / 10)) == 46))                         {
                            s->cur[YL_F_하락횟수] = (s->cur[YL_F_하락횟수] + 1);
                            s->cur[YL_F_하락대기] = 0;
                        }
                    else if ((trunc((s->cur[YL_F_주문코드] / 10)) == 49)) 
                        s->cur[YL_F_게이지매도횟수] = (s->cur[YL_F_게이지매도횟수] + 1);
                    else if ((trunc((s->cur[YL_F_주문코드] / 10)) == 52)) 
                        s->cur[YL_F_시가이탈매도횟수] = (s->cur[YL_F_시가이탈매도횟수] + 1);
                    else if ((trunc((s->cur[YL_F_주문코드] / 10)) == 55)) 
                        s->cur[YL_F_시가회복매수횟수] = (s->cur[YL_F_시가회복매수횟수] + 1);
                    else if ((trunc((s->cur[YL_F_주문코드] / 10)) == 58)) 
                        s->cur[YL_F_시가반락매도횟수] = (s->cur[YL_F_시가반락매도횟수] + 1);
                    else if (((trunc((s->cur[YL_F_주문코드] / 10)) == 61) || (trunc((s->cur[YL_F_주문코드] / 10)) == 64))) 
                        s->cur[YL_F_흡수돌파횟수] = (s->cur[YL_F_흡수돌파횟수] + 1);
                    else if ((trunc((s->cur[YL_F_주문코드] / 10)) == 67)) 
                        s->cur[YL_F_확장반등횟수] = (s->cur[YL_F_확장반등횟수] + 1);
                    else if ((trunc((s->cur[YL_F_주문코드] / 10)) == 79)) 
                        s->cur[YL_F_전일저이탈횟수] = (s->cur[YL_F_전일저이탈횟수] + 1);
                    else if ((trunc((s->cur[YL_F_주문코드] / 10)) == 82)) 
                        s->cur[YL_F_전일고돌파횟수] = (s->cur[YL_F_전일고돌파횟수] + 1);
                    else if ((trunc((s->cur[YL_F_주문코드] / 10)) == 85)) 
                        s->cur[YL_F_초기하락매도횟수] = (s->cur[YL_F_초기하락매도횟수] + 1);
                    else if ((trunc((s->cur[YL_F_주문코드] / 10)) == 88)) 
                        s->cur[YL_F_초기상승매수횟수] = (s->cur[YL_F_초기상승매수횟수] + 1);
                    else if ((trunc((s->cur[YL_F_주문코드] / 10)) == 94)) 
                        s->cur[YL_F_무거래30횟수] = (s->cur[YL_F_무거래30횟수] + 1);
                    else if ((trunc((s->cur[YL_F_주문코드] / 10)) == 40)) 
                        s->cur[YL_F_반전돌파매도횟수] = (s->cur[YL_F_반전돌파매도횟수] + 1);
                    else 
                        s->cur[YL_F_본장압축해제횟수] = (s->cur[YL_F_본장압축해제횟수] + 1);
                    s->cur[YL_F_가상포지션] = ((s->cur[YL_F_가상매수진입] == 1) ? 1 : (-1));
                    s->cur[YL_F_가상진입가] = b->close;
                    s->cur[YL_F_가상최고가] = b->close;
                    s->cur[YL_F_가상최저가] = b->close;
                    s->cur[YL_F_장상태최고가] = b->close;
                    s->cur[YL_F_장상태최저가] = b->close;
                    s->cur[YL_F_가상진입수량] = s->cur[YL_F_주문수량];
                    s->cur[YL_F_현재1차틱] = s->cur[YL_F_일차청산틱];
                    if ((s->cur[YL_F_유동일차] == 1))                         {
                            s->cur[YL_F_통합진행] = (s->cur[YL_F_통합상태] * s->cur[YL_F_가상포지션]);
                            if ((s->cur[YL_F_통합진행] >= 100)) 
                                s->cur[YL_F_현재1차틱] = s->cur[YL_F_일차강틱];
                            else if ((s->cur[YL_F_통합진행] >= 50)) 
                                s->cur[YL_F_현재1차틱] = s->cur[YL_F_일차중틱];
                            s->cur[YL_F_기울기진행] = (s->cur[YL_F_선행기울기Cnt] * s->cur[YL_F_가상포지션]);
                            if (((s->cur[YL_F_기울기진행] > 0) && (s->cur[YL_F_기울기진행] <= s->cur[YL_F_기울기초기봉]))) 
                                s->cur[YL_F_현재1차틱] = (s->cur[YL_F_현재1차틱] + s->cur[YL_F_기울기초기보너스틱]);
                            if (((s->cur[YL_F_기울기진행] <= 0) || (((s->cur[YL_F_기울기후기봉] > 0) && (s->cur[YL_F_기울기진행] > s->cur[YL_F_기울기후기봉]))))) 
                                s->cur[YL_F_현재1차틱] = s->cur[YL_F_일차청산틱];
                        }
                    if ((((s->cur[YL_F_늦은1차분] > 0) && (s->cur[YL_F_늦은1차틱] > 0)) && (s->cur[YL_F_본장기준봉분] >= (s->cur[YL_F_본장시작분] + s->cur[YL_F_늦은1차분])))) 
                        s->cur[YL_F_현재1차틱] = s->cur[YL_F_늦은1차틱];
                    s->cur[YL_F_직전정리진입] = 0;
                    s->cur[YL_F_가상진입봉] = s->bar_n;
                    s->cur[YL_F_일차도달] = 0;
                    s->cur[YL_F_일차도달틱] = 0;
                    s->cur[YL_F_진입본장경과] = (s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]);
                    s->cur[YL_F_재진입표시] = 0;
                    if ((((((s->cur[YL_F_실세션진입수] > 0) && (s->cur[YL_F_실청산봉] > 0)) && ((s->bar_n - s->cur[YL_F_실청산봉]) <= 40)) && (s->cur[YL_F_실직전최대틱] >= 50)) && (s->cur[YL_F_실직전방향] == ((s->cur[YL_F_가상매수진입] == 1) ? 1 : (-1))))) 
                        s->cur[YL_F_재진입표시] = 1;
                    s->cur[YL_F_장대연속발생] = 0;
                    s->cur[YL_F_가상진입번호] = trunc((s->cur[YL_F_주문코드] / 10));
                    s->cur[YL_F_보유중압축해제] = 0;
                    s->cur[YL_F_가상그림자] = s->cur[YL_F_그림자진입];
                    s->cur[YL_F_수익전환수] = 0;
                    s->cur[YL_F_직전봉평가손익] = 0;
                    if ((((s->cur[YL_F_직전수익1차틱] > 0) && (((s->cur[YL_F_직전수익1차수익필요] == 0) || (s->cur[YL_F_직전거래손익] > 0)))) && ((s->bar_n - s->cur[YL_F_직전청산봉]) <= s->cur[YL_F_직전수익1차봉수]))) 
                        s->cur[YL_F_직전정리진입] = 1;
                }
            if (((((((s->cur[YL_F_가상포지션] != 0) && (s->cur[YL_F_직전정리진입] == 1)) && (s->cur[YL_F_직전수익1차틱] > 0)) && (s->cur[YL_F_S삼선비율] >= s->cur[YL_F_직전수익1차삼선비율])) && ((s->bar_n - s->cur[YL_F_가상진입봉]) >= s->cur[YL_F_직전수익1차경과봉])) && (((s->cur[YL_F_현재1차틱] <= 0) || (s->cur[YL_F_현재1차틱] > s->cur[YL_F_직전수익1차틱]))))) 
                s->cur[YL_F_현재1차틱] = s->cur[YL_F_직전수익1차틱];
            if ((s->cur[YL_F_가상포지션] != 0))                 {
                    s->cur[YL_F_보유평가손익] = (((b->close - s->cur[YL_F_가상진입가])) * s->cur[YL_F_가상포지션]);
                    if ((((s->bar_n > s->cur[YL_F_가상진입봉]) && (s->cur[YL_F_보유평가손익] > 0)) && (s->cur[YL_F_직전봉평가손익] <= 0))) 
                        s->cur[YL_F_수익전환수] = (s->cur[YL_F_수익전환수] + 1);
                    s->cur[YL_F_직전봉평가손익] = s->cur[YL_F_보유평가손익];
                    if (((((s->cur[YL_F_수익전환기준] > 0) && (s->cur[YL_F_수익전환1차틱] > 0)) && (s->cur[YL_F_수익전환수] >= s->cur[YL_F_수익전환기준])) && (((s->cur[YL_F_현재1차틱] <= 0) || (s->cur[YL_F_현재1차틱] > s->cur[YL_F_수익전환1차틱]))))) 
                        s->cur[YL_F_현재1차틱] = s->cur[YL_F_수익전환1차틱];
                    if ((((((s->cur[YL_F_압축재개청산] == 1) && (s->cur[YL_F_가상포지션] == 1)) && (s->cur[YL_F_현재1차틱] > 0)) && (s->cur[YL_F_가상최고가] < (s->cur[YL_F_가상진입가] + (s->cur[YL_F_현재1차틱] * b->price_scale)))) && (trunc((((b->close - s->cur[YL_F_가상진입가])) / b->price_scale)) >= 1))) 
                        s->cur[YL_F_현재1차틱] = trunc((((b->close - s->cur[YL_F_가상진입가])) / b->price_scale));
                    if ((((((s->cur[YL_F_압축재개청산] == (-1)) && (s->cur[YL_F_가상포지션] == (-1))) && (s->cur[YL_F_현재1차틱] > 0)) && (s->cur[YL_F_가상최저가] > (s->cur[YL_F_가상진입가] - (s->cur[YL_F_현재1차틱] * b->price_scale)))) && (trunc((((s->cur[YL_F_가상진입가] - b->close)) / b->price_scale)) >= 1))) 
                        s->cur[YL_F_현재1차틱] = trunc((((s->cur[YL_F_가상진입가] - b->close)) / b->price_scale));
                    if ((((((((s->cur[YL_F_하락수익청산분] > 0) && (s->cur[YL_F_가상진입번호] == 46)) && (s->cur[YL_F_가상포지션] == (-1))) && ((s->bar_n - s->cur[YL_F_가상진입봉]) >= s->cur[YL_F_하락수익청산분])) && (s->cur[YL_F_현재1차틱] > 0)) && (s->cur[YL_F_가상최저가] > (s->cur[YL_F_가상진입가] - (s->cur[YL_F_현재1차틱] * b->price_scale)))) && (trunc((((s->cur[YL_F_가상진입가] - b->close)) / b->price_scale)) >= 1))) 
                        s->cur[YL_F_현재1차틱] = trunc((((s->cur[YL_F_가상진입가] - b->close)) / b->price_scale));
                    if ((((((((((s->cur[YL_F_이차미달청산분] > 0) && (s->cur[YL_F_이차미달기준틱] > 0)) && (s->cur[YL_F_가상진입번호] == 43)) && (s->cur[YL_F_가상포지션] == 1)) && ((s->bar_n - s->cur[YL_F_가상진입봉]) >= s->cur[YL_F_이차미달청산분])) && (s->cur[YL_F_현재1차틱] > 1)) && (s->cur[YL_F_가상최고가] < (s->cur[YL_F_가상진입가] + (s->cur[YL_F_현재1차틱] * b->price_scale)))) && (s->cur[YL_F_가상최고가] < (s->cur[YL_F_가상진입가] + (s->cur[YL_F_이차미달기준틱] * b->price_scale)))) && (b->close < s->cur[YL_F_가상진입가]))) 
                        s->cur[YL_F_현재1차틱] = 1;
                    if (((((((((s->cur[YL_F_진입본장경과] >= (-76)) && (s->cur[YL_F_진입본장경과] <= (-62))) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= (-56))) && (s->bar_n > s->cur[YL_F_가상진입봉])) && (s->cur[YL_F_현재1차틱] > 1)) && (s->cur[YL_F_가상포지션] == 1)) && (s->cur[YL_F_가상최고가] < (s->cur[YL_F_가상진입가] + (s->cur[YL_F_현재1차틱] * b->price_scale)))) && (yl_max(1, trunc((((b->close - s->cur[YL_F_가상진입가])) / b->price_scale))) < s->cur[YL_F_현재1차틱]))) 
                        s->cur[YL_F_현재1차틱] = yl_max(1, trunc((((b->close - s->cur[YL_F_가상진입가])) / b->price_scale)));
                    if (((((((((s->cur[YL_F_진입본장경과] >= (-76)) && (s->cur[YL_F_진입본장경과] <= (-62))) && ((s->cur[YL_F_본장기준봉분] - s->cur[YL_F_본장시작분]) >= (-56))) && (s->bar_n > s->cur[YL_F_가상진입봉])) && (s->cur[YL_F_현재1차틱] > 1)) && (s->cur[YL_F_가상포지션] == (-1))) && (s->cur[YL_F_가상최저가] > (s->cur[YL_F_가상진입가] - (s->cur[YL_F_현재1차틱] * b->price_scale)))) && (yl_max(1, trunc((((s->cur[YL_F_가상진입가] - b->close)) / b->price_scale))) < s->cur[YL_F_현재1차틱]))) 
                        s->cur[YL_F_현재1차틱] = yl_max(1, trunc((((s->cur[YL_F_가상진입가] - b->close)) / b->price_scale)));
                    if (((((s->cur[YL_F_진입후1차분] > 0) && (s->cur[YL_F_진입후1차틱] > 0)) && ((s->bar_n - s->cur[YL_F_가상진입봉]) >= s->cur[YL_F_진입후1차분])) && (s->cur[YL_F_현재1차틱] > s->cur[YL_F_진입후1차틱])))                         {
                            if (((s->cur[YL_F_가상포지션] == 1) && (s->cur[YL_F_가상최고가] < (s->cur[YL_F_가상진입가] + (s->cur[YL_F_현재1차틱] * b->price_scale))))) 
                                s->cur[YL_F_현재1차틱] = s->cur[YL_F_진입후1차틱];
                            if (((s->cur[YL_F_가상포지션] == (-1)) && (s->cur[YL_F_가상최저가] > (s->cur[YL_F_가상진입가] - (s->cur[YL_F_현재1차틱] * b->price_scale))))) 
                                s->cur[YL_F_현재1차틱] = s->cur[YL_F_진입후1차틱];
                        }
                    if (((((((((((s->cur[YL_F_통합위치1차경과봉] > 0) && (s->cur[YL_F_통합위치1차통합기준] > 0)) && (s->cur[YL_F_통합위치1차틱] > 0)) && (s->cur[YL_F_가상포지션] == 1)) && ((s->bar_n - s->cur[YL_F_가상진입봉]) >= s->cur[YL_F_통합위치1차경과봉])) && (s->cur[YL_F_통합카운트] > s->cur[YL_F_통합위치1차통합기준])) && (s->cur[YL_F_선행유효] == 1)) && (s->cur[YL_F_선행위치Cnt] < 0)) && (s->cur[YL_F_현재1차틱] > s->cur[YL_F_통합위치1차틱])) && (s->cur[YL_F_가상최고가] < (s->cur[YL_F_가상진입가] + (s->cur[YL_F_현재1차틱] * b->price_scale))))) 
                        s->cur[YL_F_현재1차틱] = s->cur[YL_F_통합위치1차틱];
                    if (((((((((s->cur[YL_F_저수익1차분] > 0) && (s->cur[YL_F_저수익기준틱] > 0)) && (s->cur[YL_F_저수익1차틱] > 0)) && (s->cur[YL_F_가상진입수량] == 2)) && ((s->bar_n - s->cur[YL_F_가상진입봉]) >= s->cur[YL_F_저수익1차분])) && (s->cur[YL_F_현재1차틱] > s->cur[YL_F_저수익1차틱])) && (s->cur[YL_F_본장중] == 0)) && (s->cur[YL_F_본장기준봉분] < (s->cur[YL_F_본장시작분] - s->cur[YL_F_저수익제외분]))))                         {
                            if (((s->cur[YL_F_가상포지션] == 1) && (s->cur[YL_F_가상최고가] < (s->cur[YL_F_가상진입가] + (s->cur[YL_F_저수익기준틱] * b->price_scale))))) 
                                s->cur[YL_F_현재1차틱] = s->cur[YL_F_저수익1차틱];
                            if (((s->cur[YL_F_가상포지션] == (-1)) && (s->cur[YL_F_가상최저가] > (s->cur[YL_F_가상진입가] - (s->cur[YL_F_저수익기준틱] * b->price_scale))))) 
                                s->cur[YL_F_현재1차틱] = s->cur[YL_F_저수익1차틱];
                        }
                }
            if (((((((s->cur[YL_F_수익보호스톱] == 1) && (s->cur[YL_F_수익보호기준틱] > 0)) && (s->cur[YL_F_수익보호선틱] > 0)) && (s->cur[YL_F_가상포지션] == 1)) && (b->market == 1)) && (s->cur[YL_F_가상최고가] >= (s->cur[YL_F_가상진입가] + (s->cur[YL_F_수익보호기준틱] * b->price_scale))))) 
                yl_order(s, TR_YL_EXIT_LONG, TR_YL_STOP, 0, (int)((s->cur[YL_F_가상진입가] + (s->cur[YL_F_수익보호선틱] * b->price_scale))), "수익보호스톱매수");
            if (((((((s->cur[YL_F_수익보호스톱] == 1) && (s->cur[YL_F_수익보호기준틱] > 0)) && (s->cur[YL_F_수익보호선틱] > 0)) && (s->cur[YL_F_가상포지션] == (-1))) && (b->market == (-1))) && (s->cur[YL_F_가상최저가] <= (s->cur[YL_F_가상진입가] - (s->cur[YL_F_수익보호기준틱] * b->price_scale))))) 
                yl_order(s, TR_YL_EXIT_SHORT, TR_YL_STOP, 0, (int)((s->cur[YL_F_가상진입가] - (s->cur[YL_F_수익보호선틱] * b->price_scale))), "수익보호스톱매도");
            if ((((((s->cur[YL_F_이차보존시작pt] > 0) && (s->cur[YL_F_가상포지션] == 1)) && (b->market == 1)) && (s->cur[YL_F_가상진입번호] == 43)) && ((s->cur[YL_F_가상최고가] - s->cur[YL_F_가상진입가]) >= s->cur[YL_F_이차보존시작pt]))) 
                yl_order(s, TR_YL_EXIT_LONG, TR_YL_STOP, 0, (int)((s->cur[YL_F_가상최고가] - s->cur[YL_F_이차추적pt])), "2차추적스톱매수");
            if ((((((s->cur[YL_F_이차보존시작pt] > 0) && (s->cur[YL_F_가상포지션] == (-1))) && (b->market == (-1))) && (s->cur[YL_F_가상진입번호] == 46)) && ((s->cur[YL_F_가상진입가] - s->cur[YL_F_가상최저가]) >= s->cur[YL_F_이차보존시작pt]))) 
                yl_order(s, TR_YL_EXIT_SHORT, TR_YL_STOP, 0, (int)((s->cur[YL_F_가상최저가] + s->cur[YL_F_이차추적pt])), "2차하락추적스톱매도");
            if ((((s->cur[YL_F_비상손절pt] > 0) && (s->cur[YL_F_가상포지션] == 1)) && (b->market == 1))) 
                yl_order(s, TR_YL_EXIT_LONG, TR_YL_STOP, 0, (int)((s->cur[YL_F_가상진입가] - s->cur[YL_F_비상손절pt])), "비상손절매수");
            if ((((s->cur[YL_F_비상손절pt] > 0) && (s->cur[YL_F_가상포지션] == (-1))) && (b->market == (-1)))) 
                yl_order(s, TR_YL_EXIT_SHORT, TR_YL_STOP, 0, (int)((s->cur[YL_F_가상진입가] + s->cur[YL_F_비상손절pt])), "비상손절매도");
            if ((((((s->cur[YL_F_일차후본전보장] == 1) && (s->cur[YL_F_일차도달] == 1)) && (s->cur[YL_F_일차도달틱] > 0)) && (s->cur[YL_F_가상포지션] == 1)) && (b->market == 1))) 
                yl_order(s, TR_YL_EXIT_LONG, TR_YL_STOP, 0, (int)((s->cur[YL_F_가상진입가] - (((s->cur[YL_F_일차도달틱] * b->price_scale) - s->cur[YL_F_일차후본전여유pt])))), "일차후본전매수");
            if ((((((s->cur[YL_F_일차후본전보장] == 1) && (s->cur[YL_F_일차도달] == 1)) && (s->cur[YL_F_일차도달틱] > 0)) && (s->cur[YL_F_가상포지션] == (-1))) && (b->market == (-1)))) 
                yl_order(s, TR_YL_EXIT_SHORT, TR_YL_STOP, 0, (int)((s->cur[YL_F_가상진입가] + (((s->cur[YL_F_일차도달틱] * b->price_scale) - s->cur[YL_F_일차후본전여유pt])))), "일차후본전매도");
            if (((s->cur[YL_F_가상포지션] == 1) && (((b->market == 1) || (s->cur[YL_F_가상매수진입] == 1)))))                 {
                    if ((s->cur[YL_F_현재1차틱] > 0)) 
                        yl_order(s, TR_YL_EXIT_LONG, TR_YL_LIMIT, 0, (int)((s->cur[YL_F_가상진입가] + (s->cur[YL_F_현재1차틱] * b->price_scale))), "1차청산매수");
                }
            if (((s->cur[YL_F_가상포지션] == (-1)) && (((b->market == (-1)) || (s->cur[YL_F_가상매도진입] == 1)))))                 {
                    if ((s->cur[YL_F_현재1차틱] > 0)) 
                        yl_order(s, TR_YL_EXIT_SHORT, TR_YL_LIMIT, 0, (int)((s->cur[YL_F_가상진입가] - (s->cur[YL_F_현재1차틱] * b->price_scale))), "1차청산매도");
                }
        }
}
