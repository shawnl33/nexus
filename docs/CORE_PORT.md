# 엔진 코어를 다른 대시보드에 이식하기

`src/core` 정적 라이브러리를 다른 화면의 C 호스트에 붙이는 절차다. 예스랭귀지 원본을 C로 옮기는 규약은 [PORTING.md](PORTING.md)에 있다.

코어는 봉을 받아 수식 상태와 주문 장부를 갱신한다. 시세 수신, 거래소 주문, 픽셀 그리기는 호스트가 한다. Nexus 대시보드(`web/`)는 이 숫자를 그리기만 하고 수식을 다시 계산하지 않는다.

## 1. 가져가는 파일

포함 경로를 `src`로 두고 헤더는 `core/...`로 포함한다. C 표준은 C17이다. 링크는 `libm` 하나다. MSVC에는 `libm`이 없다.

| 복사 | 내용 |
|---|---|
| `src/core/functions/` | 예스랭귀지 함수 |
| `src/core/indicators/` | 지표와 해외선물 진입 스코프 |
| `src/core/paintbar/` | 강조식. 캔들 색 |
| `src/core/signals/` | 위클리 페어 |
| `src/core/market/` | 봉, 링, 시계열 `yl_var` |
| `src/core/model/` | 시간, 단위, 종목 |
| `src/core/backtest/yl_fill.c`, `yl_fill.h` | 체결 장부 |
| `src/core/backtest/yl_link.c`, `yl_link.h` | 진입·짝 신호를 장부 주문으로 연결 |

`src/core/backtest/bar_fill.c`는 복사하지 않는다. SQLite 저장소 헤더를 포함하고, 이 저장소에서도 `core` 타겟이 아니라 `storage` 타겟에 들어 있다. `src/runtime/`, `src/adapters/`, `src/app/`, `src/cli/`, `web/`도 수식에 필요 없다.

수식 헤더가 서로 물려 있다. 파일 몇 개만 고르면 빌드가 깨진다. `CMakeLists.txt`의 `add_library(core ...)` 목록을 그대로 가져온다. `bar_fill.c`는 그 목록에 없다.

```cmake
add_library(core STATIC
    # CMakeLists.txt 의 core 소스 목록과 같게
)
target_include_directories(core PUBLIC ${NEXUS_SRC})  # .../src
target_compile_options(core PRIVATE -Wall -Wextra -Wpedantic)
if(NOT MSVC)
    target_link_libraries(core PUBLIC m)
endif()
target_link_libraries(your_host PRIVATE core)
```

이 저장소에서 라이브러리만 만들 때는 저장소 루트에서 다음을 실행한다.

```
cmake --preset default && cmake --build --preset default --target core
```

산출물은 `build/libcore.a`다.

## 2. 호스트가 갖는 것

종목마다 상태를 하나씩 둔다. `tr_fxos_t`와 `tr_fxmirae_t`는 크다. 스택에 두지 말고 `calloc`으로 둔다. 한 상태 객체를 두 스레드에서 동시에 `eval`하지 않는다.

호스트가 넣는 값은 완성된 1분봉이다. 코어는 NH, LS, Theta를 호출하지 않는다.

| 필드 | 의미 |
|---|---|
| `date` | 거래소 현지 `YYYYMMDD` |
| `time` | 거래소 현지 `HHMMSS` |
| `bar_open` | 봉 시작, UTC epoch 마이크로초 |
| OHLC, `volume` | 아래 가격 단위와 같은 단위 |
| `price_unit` | 1포인트가 입력에서 차지하는 크기. 포인트 가격이면 `1`, raw면 `100`. `0`은 `1`로 본다 |
| `is_new_bar` | 이 봉의 첫 평가이면 참 |

현지 시각은 `tr_civil_from_time_us(open_us, 540, &civil)`로 만든다. `540`은 KST(UTC+9)다. 연·월·일을 `YYYYMMDD`, 시·분·초를 `HHMMSS`로 접는다. 선언은 `core/model/civil_time.h`에 있다.

`is_new_bar`가 참이거나 아직 봉 스냅샷이 없으면 `tr_fxos_eval`은 봉 시작 상태를 저장한다. 같은 봉을 다시 계산할 때는 그 스냅샷으로 되돌린 뒤 한 번만 다시 계산한다. 진행 중 봉의 갱신마다 `is_new_bar`를 참로 두면 60봉 금지, 4봉 1차 축소, `uni_count >= 30` 압축매수 삭제가 봉이 닫히기 전에 발동한다. 이미 받은 완성 1분봉을 순서대로 replay할 때는 봉마다 참 한 번이 맞다.

## 3. 가격 단위

`tr_fxos_init`의 `price_scale`은 입력 가격과 같은 단위의 1틱이다. 입력 OHLC, `price_unit`, 장부 `tick`을 그 단위에 맞춘다. 포인트 가격에 `price_scale` 100을 넣으면 15틱이 1500포인트가 된다.

포인트 가격을 넣는 호스트 (ES 7734.00):

| 값 | ES, NQ | RTY |
|---|---|---|
| OHLC | 표시 가격 | 표시 가격 |
| `tr_fxos_init`의 `price_scale` | `0.25` | `0.10` |
| `price_unit` | `1` | `1` |
| `tr_yl_book_init`의 `tick` | `0.25` | `0.10` |

Nexus 실시간 경로는 가격을 표시값 × 100으로 저장한다 (ES 7734.00 → 773400). 그 경로의 1틱은 ES·NQ `25`, RTY `10`이고 `price_unit`은 `100`이다. `src/runtime/engine.c`의 `tick_scale`과 `oin.price_unit = 100`이 그 조합이다. 새 대시보드가 표시 가격을 갖고 있으면 포인트 표를 쓴다.

알 수 없는 해외선물의 Nexus 백테스트 틱은 `0.05`다. 국내 선물의 raw 틱 기본은 `5`(0.05포인트 × 100)다.

## 4. 해외선물 진입 신호

호출 순서는 `src/app/backtest_cmd.c`의 `fx_sig_v6` 루프와 같다. 미래곡선을 먼저 돌려야 5·15·30분 합성선이 진입 입력에 들어간다.

```c
#include "core/backtest/yl_link.h"
#include "core/indicators/fx_mirae_v1.h"
#include "core/indicators/fx_os_scope.h"

tr_fxos_t *scope = calloc(1, sizeof(*scope));
tr_fxmirae_t *fx = calloc(1, sizeof(*fx));
tr_fxmirae_config_t mcfg;
tr_yl_book_t book;
double tick = 0.25; /* ES, NQ. RTY 는 0.10 */

tr_fxmirae_default_config(&mcfg, tick);
tr_fxos_init(scope, tick);
tr_fxmirae_init(fx, &mcfg);
scope->cfg.order_qty = 1;          /* 0 이면 장부 기본 수량 */
tr_yl_book_init(&book, 1, tick);

for (각 완성 1분봉, 오래된 봉이 앞) {
    tr_fxmirae_input_t min;
    tr_fxos_input_t in;
    memset(&min, 0, sizeof(min));
    memset(&in, 0, sizeof(in));

    min.bar.date = ymd;
    min.bar.time = hhmmss;
    min.bar.bar_open = open_us;
    min.bar.high = high;
    min.bar.low = low;
    min.bar.close = close;
    min.bar.volume = volume;
    if (직전 봉이 있으면) {
        min.has_prev = true;
        min.prev_date = prev_ymd;
        min.prev_time = prev_hhmmss;
        min.prev_h = prev_high;
        min.prev_l = prev_low;
        min.prev_c = prev_close;
        min.prev_v = prev_volume;
    }
    tr_fxmirae_eval(fx, &min);

    in.date = ymd;
    in.time = hhmmss;
    in.bar_open = open_us;
    in.open = open;
    in.high = high;
    in.low = low;
    in.close = close;
    in.volume = volume;
    in.price_unit = 1.0;
    in.is_new_bar = true;
    tr_fxos_set_synth(&in, &fx->syn5, &fx->syn15, &fx->syn30);

    tr_yl_fxsig_bar(&book, scope, &in);
}
tr_yl_book_finish(&book);
```

화면만 필요하고 주문 표가 없으면 `tr_yl_fxsig_bar` 대신 `tr_fxos_eval(scope, &in)`을 호출하고 `scope->out`을 읽는다. 실시간 차트 경로는 장부를 쓰지 않고 이쪽이다.

`tr_yl_fxsig_bar`는 장부 포지션을 `scope->mkt`, `contracts`, `avg_entry`에 넣은 뒤 식을 돌린다. 그 값이 예스트레이더의 `I_MarketPosition`, `I_CurrentContracts`, `I_AvgEntryPrice`다. 수익관리는 이 시스템 포지션을 읽고, 증권사 잔고를 읽지 않는다.

주문 이름은 `tr_fxos_order_name`이 만든다. 종류는 1 후보, 2 돌파, 3 선행, 5 전환, 4 조정이다. 청산 이름은 `파랑청산`, `빨강청산`, `시간청산매수`, `시간청산매도`다.

## 5. 짝 신호와 제로데이

짝은 같은 스코프 옆에 `tr_fxpair_t`를 둔다. `tr_fxpair_init`은 구조체를 0으로 지운다. `calloc`으로 같은 결과가 된다.

```c
tr_fxpair_t *pair = calloc(1, sizeof(*pair));
tr_fxpair_init(pair);
pair->v68 = 1; /* 제로데이(짝 V68)일 때만. 짝 V45 는 0 */
tr_yl_fxpair_bar(&book, scope, pair, &in);
```

`tr_yl_fxpair_bar`는 `tr_fxos_eval` 다음에 `tr_fxpair_on_bar`를 호출한다. `has_market`은 0으로 남는다. 실시간 차트에 증권사 포지션이 없는 것과 같다. 색 청산과 시간 청산은 가상포지션을 따른다.

주문은 종가 진입 하나, 대기 중인 1차 지정가 하나, 남은 수량에 대한 색 또는 시간 청산이다. 1차 틱 수가 줄면 기존 1차를 갈아 끼우고 쌓지 않는다. 1차가 한 번 체결되면 `limit_filled`가 더 이상의 1차를 막아서, 한 터치에 잔량 전체가 청산되지 않는다. 청산 수량이 0이면 청산 주문을 넣지 않는다. 거래 표 순서는 진입, 1차 체결, 잔량의 색 또는 시간 청산이다.

`v68 == 1`이면 1차 이름이 `1차청산매수` / `1차청산매도`다. 0이면 `1차목표청산매수` / `1차목표청산매도`다.

## 6. 위클리 페어

헤더는 `core/signals/weekly_pair_v1.h`다. 다리는 세 개다.

| 인자 | 예스트레이더 | 가격 |
|---|---|---|
| `self` | Data1, 차트의 옵션 | 주문 체결가 |
| `opp` | Data3, 반대 행사가 | 50:50 명목 합산만. 체결 없음 |
| `fut` | Data2, 선물 1분봉 | 조건만. 체결 없음 |

`side`는 `TR_WPAIR_LONG`(1, 양매수) 또는 `TR_WPAIR_SHORT`(-1, 양매도)다.

```c
tr_wpair_t st;
tr_wpair_config_t cfg;
tr_wpair_default_config(&cfg);
tr_wpair_init(&st, TR_WPAIR_LONG, &cfg);
tr_yl_book_init(&book, 1, 0.05); /* 옵션 호가 단위 */
tr_wpair_fill(&st, self, nself, opp, nopp, fut, nfut, NULL, &book);
```

봉 배열은 오래된 봉이 앞이다. 상대·선물 시각이 자기 봉과 다르면 그 다리는 없는 것으로 둔다. `score_cfg`가 `NULL`이면 점수 틱은 `0.05`다. `book`이 `NULL`이면 -1을 반환한다.

체결은 `AtMarket`이다. 신호 봉이 낸 주문은 다음 1분봉 시가에 체결되고, 그 다음 식을 돈다. 마지막 봉의 주문은 미체결로 남는다. 같은 봉을 다시 평가하면 체결 직후·식 이전으로 되돌린 뒤 주문을 다시 낸다.

## 7. 체결 장부

선언은 `core/backtest/yl_fill.h`다.

| 종류 | 상수 | 체결 |
|---|---|---|
| 종가 | `TR_YL_ONCLOSE` (0) | 신호를 낸 봉의 종가. 그 봉에서 포지션이 바뀐 뒤 같은 봉의 이후 계산이 그 포지션을 본다 |
| 시장가 | `TR_YL_MARKET` (1) | 다음 봉 시가 |
| 스톱 | `TR_YL_STOP` (2) | 다음 봉. 시가가 이미 가격을 지났으면 시가, 아니면 고저가 닿을 때 주문가 |
| 지정가 | `TR_YL_LIMIT` (3) | 스톱과 같은 시점 규칙 |

방향은 `TR_YL_BUY` 1, `TR_YL_SELL` -1, `TR_YL_EXIT_LONG` 2, `TR_YL_EXIT_SHORT` -2다. 수량 0은 진입에서 기본 수량, 청산에서 잔량 전부다. 손익은 `(체결가 - 평균진입) / tick × 청산수량 × 보유방향`이다. 수수료와 슬리피지는 없다. `tick`이 0이면 손익 칸은 비어 있다.

`tr_yl_fxsig_bar`와 `tr_yl_fxpair_bar`는 장부 봉의 시각을 `bar_open / 1000000` 초로 넣고, 그 봉의 시가 칸에 종가를 넣는다. 종가 체결이 이 봉의 종가를 쓰게 하려는 연결이다. 호스트가 장부를 직접 열 때는 `tr_yl_bar_t.open`에 그 봉의 실제 시가를 넣는다.

거래 행은 `TR_YL_TRADES`(64)개까지다. 그 다음 체결은 포지션은 바꾸지만 행을 추가하지 못한다. 긴 구간은 봉마다 `tr_yl_book_trade`로 새 행을 밖으로 복사한다. 미체결로 남길 때는 루프 끝에 `tr_yl_book_finish`를 호출한다.

## 8. 화면이 읽을 값

그리기는 호스트가 한다. 색은 `0xRRGGBB`다.

진입 스코프는 `scope->out` (`tr_fxos_out_t`)이다. 막대 색은 `judge`와 `judge_rgb`다. 주문 화살표는 `sig_dir`, `sig_exit`, `sig_kind`, `sig_grade`다. 수익관리 숫자는 `pnl_open`, `pnl_mfe`, `pnl_mae`, `pnl_closed`다. 표시 여부는 `pnl_*_on`이다.

미래곡선 V1은 `fx->plots[0 .. 23]`이다. `[0]`이 Plot1이다. `on`이 거짓이면 그 봉에서는 그리지 않는다. 값·색·굵기는 `value`, `rgb`, `width`다. 단계색은 `fx->stage_rgb`다.

구조체를 통째로 복사하면 안의 `yl_var`가 원본 버퍼를 그대로 가리킨다. 복사 뒤에는 `tr_fxos_relink`, `tr_fxmirae_relink`를 호출한다. 종목마다 상태를 따로 `init`하면 이 호출은 필요 없다.

## 9. 붙인 뒤 확인

1. ES 1분봉 하루를 포인트 가격으로 넣고 `price_scale` `0.25`, `price_unit` `1`로 돌린다. 15틱 거리가 15포인트인지 본다.
2. 진행 중 봉을 여러 번 넣을 때 `is_new_bar`는 첫 호출만 참이다. 봉이 바뀌면 다시 참이다.
3. `tr_yl_fxsig_bar`의 거래 시각은 초다. `bar_open` 마이크로초를 그대로 찍으면 시각이 어긋난다.
4. 이 저장소의 장부 테스트는 `tests/test_yl_fill.c`다. 코어를 붙인 뒤 그 테스트를 빌드해 체결 규칙을 확인한다.

코어는 주문을 거래소로 보내지 않는다. 호스트가 장부 행을 보고 따로 전송한다.
